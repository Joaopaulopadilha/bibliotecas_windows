// yt.cpp
// Biblioteca de download de videos do YouTube para JPLang via yt_worker.exe
// Nao depende de pybridge - chama o executavel Python empacotado via processo
//
// Compilar:
//   Windows: g++ -shared -o bibliotecas/yt/yt.jpd bibliotecas/yt/yt.cpp -O3 -static -std=c++17
//   Linux:   g++ -shared -fPIC -o bibliotecas/yt/libyt.jpd bibliotecas/yt/yt.cpp -O3 -std=c++17

#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <array>

// =============================================================================
// DETECCAO DE PLATAFORMA E MACROS DE EXPORT
// =============================================================================
#if defined(_WIN32) || defined(_WIN64)
    #define JP_WINDOWS 1
    #define JP_EXPORT extern "C" __declspec(dllexport)
    #define WORKER_NAME "yt.exe"
#else
    #define JP_WINDOWS 0
    #define JP_EXPORT extern "C" __attribute__((visibility("default")))
    #define WORKER_NAME "yt"
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

static JPValor jp_int(int64_t v) {
    JPValor r;
    r.tipo = JP_TIPO_INT;
    r.valor.inteiro = v;
    return r;
}

static std::string get_string(JPValor* args, int idx) {
    if (args[idx].tipo == JP_TIPO_STRING && args[idx].valor.texto) {
        return std::string(args[idx].valor.texto);
    }
    return "";
}

// =============================================================================
// LOCALIZACAO DO WORKER
// =============================================================================
static std::string g_worker_path;

// Procura o yt_worker.exe em varios caminhos possiveis
static std::string find_worker() {
    if (!g_worker_path.empty()) return g_worker_path;

    const char* tentativas[] = {
        "bibliotecas/yt/" WORKER_NAME,
        "runtime/" WORKER_NAME,
        WORKER_NAME,
        "../" WORKER_NAME,
        NULL
    };

    for (int i = 0; tentativas[i]; i++) {
        FILE* f = fopen(tentativas[i], "rb");
        if (f) {
            fclose(f);
            g_worker_path = tentativas[i];
            return g_worker_path;
        }
    }

    printf("[yt] Erro: %s nao encontrado\n", WORKER_NAME);
    return "";
}

// =============================================================================
// EXECUCAO DO WORKER
// =============================================================================

// Normaliza barras do caminho pra plataforma
static std::string fix_path(const std::string& p) {
    std::string r = p;
    #if JP_WINDOWS
        for (auto& c : r) { if (c == '/') c = '\\'; }
    #else
        for (auto& c : r) { if (c == '\\') c = '/'; }
    #endif
    return r;
}

// Executa o worker e captura stdout
static std::string exec_worker(const std::string& args) {
    std::string worker = fix_path(find_worker());
    if (worker.empty()) return "";

    // No Windows, cmd.exe precisa de aspas duplas envolvendo tudo
    #if JP_WINDOWS
        std::string cmd = "\"\"" + worker + "\" " + args + " 2>&1\"";
        FILE* pipe = _popen(cmd.c_str(), "r");
    #else
        std::string cmd = "\"" + worker + "\" " + args + " 2>&1";
        FILE* pipe = popen(cmd.c_str(), "r");
    #endif

    if (!pipe) {
        printf("[yt] Erro: falha ao executar worker\n");
        return "";
    }

    std::string resultado;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        resultado += buffer;
    }

    #if JP_WINDOWS
        _pclose(pipe);
    #else
        pclose(pipe);
    #endif

    return resultado;
}

// Executa o worker sem capturar stdout (deixa imprimir direto)
static int exec_worker_passthrough(const std::string& args) {
    std::string worker = fix_path(find_worker());
    if (worker.empty()) return -1;

    // No Windows, aspas duplas envolvendo tudo pro cmd.exe
    #if JP_WINDOWS
        std::string cmd = "\"\"" + worker + "\" " + args + "\"";
    #else
        std::string cmd = "\"" + worker + "\" " + args;
    #endif

    return system(cmd.c_str());
}

// Escapa uma string pra uso seguro em argumento de linha de comando
static std::string escape_arg(const std::string& s) {
    std::string r = "\"";
    for (char c : s) {
        if (c == '"') r += "\\\"";
        else if (c == '\\') r += "\\\\";
        else r += c;
    }
    r += "\"";
    return r;
}

// =============================================================================
// PARSER JSON MINIMO (sem dependencia externa)
// =============================================================================

// Extrai valor de uma chave em JSON simples (1 nivel)
static std::string json_get(const std::string& json, const std::string& chave) {
    std::string busca = "\"" + chave + "\"";
    size_t pos = json.find(busca);
    if (pos == std::string::npos) return "";

    pos = json.find(':', pos + busca.size());
    if (pos == std::string::npos) return "";
    pos++;

    // Pula espacos
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

    if (pos >= json.size()) return "";

    // String
    if (json[pos] == '"') {
        pos++;
        std::string val;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                pos++;
                val += json[pos];
            } else {
                val += json[pos];
            }
            pos++;
        }
        return val;
    }

    // Numero ou bool
    std::string val;
    while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ' ') {
        val += json[pos];
        pos++;
    }
    return val;
}

// Encontra a ultima linha que comeca com { (o JSON de resultado)
static std::string find_json_line(const std::string& output) {
    std::string ultima_json;
    size_t pos = 0;
    while (pos < output.size()) {
        size_t nl = output.find('\n', pos);
        std::string linha = (nl == std::string::npos) 
            ? output.substr(pos) 
            : output.substr(pos, nl - pos);
        
        // Remove \r
        if (!linha.empty() && linha.back() == '\r') linha.pop_back();
        
        // Pula espacos
        size_t start = linha.find_first_not_of(" \t");
        if (start != std::string::npos && linha[start] == '{') {
            ultima_json = linha;
        }
        
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    return ultima_json;
}

// =============================================================================
// FUNCOES EXPORTADAS
// =============================================================================

// yt_info(url)
// Retorna o titulo do video
JP_EXPORT JPValor yt_info(JPValor* args, int numArgs) {
    if (numArgs < 1) return jp_string("");

    std::string url = get_string(args, 0);
    std::string saida = exec_worker("info " + escape_arg(url));
    std::string json = find_json_line(saida);
    
    if (json.empty()) return jp_string("Erro: sem resposta do worker");

    std::string erro = json_get(json, "erro");
    if (!erro.empty()) return jp_string("Erro: " + erro);

    return jp_string(json_get(json, "titulo"));
}

// yt_canal(url)
// Retorna o nome do canal
JP_EXPORT JPValor yt_canal(JPValor* args, int numArgs) {
    if (numArgs < 1) return jp_string("");

    std::string url = get_string(args, 0);
    std::string saida = exec_worker("info " + escape_arg(url));
    std::string json = find_json_line(saida);
    
    if (json.empty()) return jp_string("");
    return jp_string(json_get(json, "canal"));
}

// yt_duracao(url)
// Retorna a duracao em segundos
JP_EXPORT JPValor yt_duracao(JPValor* args, int numArgs) {
    if (numArgs < 1) return jp_int(0);

    std::string url = get_string(args, 0);
    std::string saida = exec_worker("info " + escape_arg(url));
    std::string json = find_json_line(saida);
    
    if (json.empty()) return jp_int(0);
    
    std::string dur = json_get(json, "duracao");
    return jp_int(dur.empty() ? 0 : std::stoll(dur));
}

// yt_baixar(url, pasta)
// Baixa o video na melhor qualidade
// Retorna: 1 = sucesso, 0 = erro
JP_EXPORT JPValor yt_baixar(JPValor* args, int numArgs) {
    if (numArgs < 2) return jp_bool(0);

    std::string url = get_string(args, 0);
    std::string pasta = get_string(args, 1);

    int ret = exec_worker_passthrough("download " + escape_arg(url) + " " + escape_arg(pasta));
    return jp_bool(ret == 0 ? 1 : 0);
}

// yt_baixar_audio(url, pasta)
// Baixa apenas o audio e converte pra mp3
// Retorna: 1 = sucesso, 0 = erro
JP_EXPORT JPValor yt_baixar_audio(JPValor* args, int numArgs) {
    if (numArgs < 2) return jp_bool(0);

    std::string url = get_string(args, 0);
    std::string pasta = get_string(args, 1);

    int ret = exec_worker_passthrough("audio " + escape_arg(url) + " " + escape_arg(pasta));
    return jp_bool(ret == 0 ? 1 : 0);
}