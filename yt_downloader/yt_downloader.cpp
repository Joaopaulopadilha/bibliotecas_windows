// yt_downloader.cpp
// Biblioteca de download de videos do YouTube para JPLang via yt-dlp (Python)
//
// Compilar:
//   Windows: g++ -shared -o bibliotecas/yt_downloader/yt_downloader.jpd bibliotecas/yt_downloader/yt_downloader.cpp -O3 -static -std=c++17
//   Linux:   g++ -shared -fPIC -o bibliotecas/yt_downloader/libyt_downloader.jpd bibliotecas/yt_downloader/yt_downloader.cpp -O3 -std=c++17

#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <cstdio>

// =============================================================================
// DETECCAO DE PLATAFORMA E MACROS DE EXPORT
// =============================================================================
#if defined(_WIN32) || defined(_WIN64)
    #define JP_WINDOWS 1
    #define JP_EXPORT extern "C" __declspec(dllexport)
#else
    #define JP_WINDOWS 0
    #define JP_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// =============================================================================
// TIPOS C PUROS (interface com JPLang)
// =============================================================================
typedef enum {
    JP_TIPO_NULO = 0,
    JP_TIPO_INT = 1,
    JP_TIPO_DOUBLE = 2,
    JP_TIPO_STRING = 3,
    JP_TIPO_BOOL = 4
} JPTipo;

typedef struct {
    JPTipo tipo;
    union {
        int64_t inteiro;
        double decimal;
        char* texto;
        int booleano;
    } valor;
} JPValor;

// =============================================================================
// HELPERS
// =============================================================================
static JPValor jp_int(int v) {
    JPValor r;
    r.tipo = JP_TIPO_INT;
    r.valor.inteiro = v;
    return r;
}

static JPValor jp_bool(int v) {
    JPValor r;
    r.tipo = JP_TIPO_BOOL;
    r.valor.booleano = v ? 1 : 0;
    return r;
}

static JPValor jp_string(const std::string& s) {
    JPValor r;
    r.tipo = JP_TIPO_STRING;
    r.valor.texto = (char*)malloc(s.size() + 1);
    if (r.valor.texto) {
        memcpy(r.valor.texto, s.c_str(), s.size() + 1);
    }
    return r;
}

static std::string get_string(JPValor* args, int idx) {
    if (args[idx].tipo == JP_TIPO_STRING && args[idx].valor.texto) {
        return std::string(args[idx].valor.texto);
    }
    return "";
}

// =============================================================================
// PYBRIDGE
// =============================================================================
#include "../../src/pybridge.hpp"

static int g_yt_inicializado = 0;
static char g_browser_cookies[64] = {0};
static int g_tem_deno = 0;

// Escapa aspas simples pra usar em strings Python
static std::string escape_py(const std::string& s) {
    std::string r;
    r.reserve(s.size() + 10);
    for (char c : s) {
        if (c == '\'') r += "\\'";
        else if (c == '\\') r += "/";
        else r += c;
    }
    return r;
}

// Detecta qual navegador tem cookies do YouTube (prioriza o que tem login)
static void yt_detectar_browser() {
    if (g_browser_cookies[0]) return;

    int ret = py_exec(
        "import os, sys\n"
        "_yt_browser = ''\n"
        "\n"
        "def _yt_check_youtube_cookies(browser_name):\n"
        "    '''Tenta extrair cookies e verifica se tem sessao do YouTube'''\n"
        "    try:\n"
        "        from yt_dlp.cookies import extract_cookies_from_browser\n"
        "        jar = extract_cookies_from_browser(browser_name)\n"
        "        for cookie in jar:\n"
        "            if 'youtube.com' in cookie.domain and cookie.name in ('SID', 'SSID', 'LOGIN_INFO'):\n"
        "                return True\n"
        "    except Exception:\n"
        "        pass\n"
        "    return False\n"
        "\n"
        "if sys.platform == 'win32':\n"
        "    browsers = ['edge', 'chrome', 'firefox', 'brave']\n"
        "    paths = {\n"
        "        'edge': os.path.expandvars(r'%LOCALAPPDATA%\\Microsoft\\Edge\\User Data'),\n"
        "        'chrome': os.path.expandvars(r'%LOCALAPPDATA%\\Google\\Chrome\\User Data'),\n"
        "        'firefox': os.path.expandvars(r'%APPDATA%\\Mozilla\\Firefox\\Profiles'),\n"
        "        'brave': os.path.expandvars(r'%LOCALAPPDATA%\\BraveSoftware\\Brave-Browser\\User Data'),\n"
        "    }\n"
        "else:\n"
        "    browsers = ['chrome', 'chromium', 'firefox', 'brave']\n"
        "    paths = {\n"
        "        'chrome': os.path.expanduser('~/.config/google-chrome'),\n"
        "        'chromium': os.path.expanduser('~/.config/chromium'),\n"
        "        'firefox': os.path.expanduser('~/.mozilla/firefox'),\n"
        "        'brave': os.path.expanduser('~/.config/BraveSoftware/Brave-Browser'),\n"
        "    }\n"
        "\n"
        "# Primeiro: tenta achar um navegador com login no YouTube\n"
        "for b in browsers:\n"
        "    if b in paths and os.path.exists(paths[b]):\n"
        "        if _yt_check_youtube_cookies(b):\n"
        "            _yt_browser = b\n"
        "            break\n"
        "\n"
        "# Fallback: usa o primeiro navegador disponivel (mesmo sem login)\n"
        "if not _yt_browser:\n"
        "    for b in browsers:\n"
        "        if b in paths and os.path.exists(paths[b]):\n"
        "            _yt_browser = b\n"
        "            break\n"
        "\n"
        "with open('_yt_temp_browser.txt', 'w') as f:\n"
        "    f.write(_yt_browser)\n"
    );

    if (ret == 0) {
        FILE* f = fopen("_yt_temp_browser.txt", "r");
        if (f) {
            if (fgets(g_browser_cookies, sizeof(g_browser_cookies), f)) {
                size_t len = strlen(g_browser_cookies);
                if (len > 0 && g_browser_cookies[len-1] == '\n') g_browser_cookies[len-1] = '\0';
            }
            fclose(f);
        }
        remove("_yt_temp_browser.txt");
    }

    if (g_browser_cookies[0]) {
        printf("[yt] Cookies: usando navegador '%s'\n", g_browser_cookies);
    } else {
        printf("[yt] Aviso: nenhum navegador detectado para cookies\n");
    }
}

// Detecta se deno esta instalado (necessario pro yt-dlp 2025+)
static void yt_detectar_deno() {
    int ret = py_exec(
        "import shutil\n"
        "_yt_tem_deno = '1' if shutil.which('deno') else '0'\n"
        "with open('_yt_temp_deno.txt', 'w') as f:\n"
        "    f.write(_yt_tem_deno)\n"
    );

    if (ret == 0) {
        FILE* f = fopen("_yt_temp_deno.txt", "r");
        if (f) {
            char buf[8] = {0};
            if (fgets(buf, sizeof(buf), f)) {
                g_tem_deno = (buf[0] == '1') ? 1 : 0;
            }
            fclose(f);
        }
        remove("_yt_temp_deno.txt");
    }

    if (g_tem_deno) {
        printf("[yt] Runtime JS: deno encontrado\n");
    } else {
        printf("[yt] Aviso: 'deno' nao encontrado. Alguns videos podem falhar.\n");
        #if JP_WINDOWS
            printf("[yt] Instale deno: https://deno.land/#installation\n");
        #else
            printf("[yt] Instale com: curl -fsSL https://deno.land/install.sh | sh\n");
        #endif
    }
}

static int yt_setup() {
    if (g_yt_inicializado) return 1;

    #if !JP_WINDOWS
    PyDependencia deps[] = {
        {"ffmpeg",  "ffmpeg",        "ffmpeg",        "ffmpeg"},
    };
    int faltando = py_check_deps("yt", deps, 1);
    if (faltando > 0) {
        return 0;
    }
    #endif

    // Inicializa Python via pybridge
    if (!py_init("yt_downloader")) {
        printf("[yt] Erro: falha ao inicializar Python\n");
        #if !JP_WINDOWS
        const char* pm = py_detect_pkg_manager();
        printf("[yt] Verifique se o pacote de desenvolvimento do Python esta instalado:\n");
        if (strcmp(pm, "apt") == 0)
            printf("  sudo apt install python3-dev\n");
        else if (strcmp(pm, "dnf") == 0)
            printf("  sudo dnf install python3-devel\n");
        else if (strcmp(pm, "pacman") == 0)
            printf("  sudo pacman -S python\n");
        else
            printf("  Instale o pacote python3-dev da sua distro\n");
        #endif
        return 0;
    }

    // Adiciona paths ao sys.path
    py_exec(
        "import sys, os, site\n"
        "user_site = site.getusersitepackages()\n"
        "if os.path.exists(user_site) and user_site not in sys.path:\n"
        "    sys.path.insert(0, user_site)\n"
        "for base in ['bibliotecas/yt_downloader', 'runtime', '.']:\n"
        "    pkg = os.path.join(base, 'python_packages')\n"
        "    if os.path.exists(pkg) and pkg not in sys.path:\n"
        "        sys.path.append(pkg)\n"
        "        break\n"
    );

    // Verifica se yt-dlp esta disponivel
    if (!py_check_pip_module("yt", "yt_dlp", "yt-dlp")) {
        return 0;
    }

    // Detecta navegador com login e deno
    yt_detectar_browser();
    yt_detectar_deno();

    g_yt_inicializado = 1;
    printf("[yt] Biblioteca inicializada com sucesso\n");
    return 1;
}

// Helper: monta opcoes extras (cookies + remote_components)
static std::string yt_opcoes_extras() {
    std::string opts;
    if (g_browser_cookies[0]) {
        opts += "    'cookiesfrombrowser': ('" + std::string(g_browser_cookies) + "',),\n";
    }
    if (g_tem_deno) {
        opts += "    'remote_components': ['ejs:github'],\n";
    }
    return opts;
}

// =============================================================================
// FUNCOES EXPORTADAS
// =============================================================================

// yt_baixar(url, pasta)
// Baixa o video na melhor qualidade
// Retorna: 1 = sucesso, 0 = erro
JP_EXPORT JPValor yt_baixar(JPValor* args, int numArgs) {
    if (numArgs < 2) return jp_bool(0);
    if (!yt_setup()) return jp_bool(0);

    std::string url = escape_py(get_string(args, 0));
    std::string pasta = escape_py(get_string(args, 1));
    std::string extras = yt_opcoes_extras();

    char cmd[8192];
    snprintf(cmd, sizeof(cmd),
        "import yt_dlp, os\n"
        "os.makedirs('%s', exist_ok=True)\n"
        "opts = {\n"
        "    'outtmpl': '%s/%%(title)s.%%(ext)s',\n"
        "    'format': 'bestvideo+bestaudio/best',\n"
        "    'quiet': False,\n"
        "    'no_warnings': True,\n"
        "%s"
        "}\n"
        "try:\n"
        "    with yt_dlp.YoutubeDL(opts) as ydl:\n"
        "        ydl.download(['%s'])\n"
        "    _yt_result = 1\n"
        "except Exception as e:\n"
        "    print(f'[yt_downloader] Erro: {e}')\n"
        "    _yt_result = 0\n",
        pasta.c_str(), pasta.c_str(), extras.c_str(), url.c_str()
    );

    int ret = py_exec(cmd);
    return jp_bool(ret == 0 ? 1 : 0);
}

// yt_baixar_audio(url, pasta)
// Baixa apenas o audio e converte pra mp3
// Retorna: 1 = sucesso, 0 = erro
JP_EXPORT JPValor yt_baixar_audio(JPValor* args, int numArgs) {
    if (numArgs < 2) return jp_bool(0);
    if (!yt_setup()) return jp_bool(0);

    std::string url = escape_py(get_string(args, 0));
    std::string pasta = escape_py(get_string(args, 1));
    std::string extras = yt_opcoes_extras();

    char cmd[8192];
    snprintf(cmd, sizeof(cmd),
        "import yt_dlp, os\n"
        "os.makedirs('%s', exist_ok=True)\n"
        "opts = {\n"
        "    'outtmpl': '%s/%%(title)s.%%(ext)s',\n"
        "    'format': 'bestaudio/best',\n"
        "    'postprocessors': [{\n"
        "        'key': 'FFmpegExtractAudio',\n"
        "        'preferredcodec': 'mp3',\n"
        "        'preferredquality': '192',\n"
        "    }],\n"
        "    'quiet': False,\n"
        "    'no_warnings': True,\n"
        "%s"
        "}\n"
        "try:\n"
        "    with yt_dlp.YoutubeDL(opts) as ydl:\n"
        "        ydl.download(['%s'])\n"
        "    _yt_result = 1\n"
        "except Exception as e:\n"
        "    print(f'[yt_downloader] Erro: {e}')\n"
        "    _yt_result = 0\n",
        pasta.c_str(), pasta.c_str(), extras.c_str(), url.c_str()
    );

    int ret = py_exec(cmd);
    return jp_bool(ret == 0 ? 1 : 0);
}

// yt_info(url)
// Retorna o titulo do video
JP_EXPORT JPValor yt_info(JPValor* args, int numArgs) {
    if (numArgs < 1) return jp_string("");
    if (!yt_setup()) return jp_string("");

    std::string url = escape_py(get_string(args, 0));
    std::string extras = yt_opcoes_extras();

    char cmd[8192];
    snprintf(cmd, sizeof(cmd),
        "import yt_dlp\n"
        "opts = {\n"
        "    'quiet': True,\n"
        "    'no_warnings': True,\n"
        "    'skip_download': True,\n"
        "%s"
        "}\n"
        "try:\n"
        "    with yt_dlp.YoutubeDL(opts) as ydl:\n"
        "        info = ydl.extract_info('%s', download=False)\n"
        "        _yt_titulo = info.get('title', 'Desconhecido')\n"
        "except Exception as e:\n"
        "    _yt_titulo = f'Erro: {e}'\n"
        "with open('_yt_temp_info.txt', 'w', encoding='utf-8') as f:\n"
        "    f.write(_yt_titulo)\n",
        extras.c_str(), url.c_str()
    );

    int ret = py_exec(cmd);
    if (ret != 0) return jp_string("Erro ao obter info");

    FILE* f = fopen("_yt_temp_info.txt", "r");
    if (!f) return jp_string("Erro ao ler resultado");

    char buf[2048] = {0};
    if (fgets(buf, sizeof(buf), f)) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
    }
    fclose(f);
    remove("_yt_temp_info.txt");

    return jp_string(buf);
}