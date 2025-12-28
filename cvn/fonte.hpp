// fonte.hpp
// Módulo de fontes de vídeo para biblioteca CVN

#ifndef CVN_FONTE_HPP
#define CVN_FONTE_HPP

#include <windows.h>
#include <string>
#include <vector>
#include <functional>

namespace cvn {

// Tipos de fonte
enum class TipoFonte {
    NENHUM,
    CAMERA,
    TELA,
    REGIAO
};

// Tipos de filtro
enum class TipoFiltro {
    NENHUM,
    CINZA,
    INVERTER,
    BRILHO,
    CONTRASTE,
    LIMIAR,
    BLUR,
    BORDAS,
    SEPIA,
    SATURACAO,
    FLIP_H,
    FLIP_V,
    ROTACIONAR,
    REDIMENSIONAR,
    LIMIAR_FILTRO
};

// Configuração de filtro
struct ConfigFiltro {
    TipoFiltro tipo = TipoFiltro::NENHUM;
    float valor1 = 0;
    float valor2 = 0;
};

// Estrutura de fonte de vídeo
struct Fonte {
    TipoFonte tipo = TipoFonte::NENHUM;
    int indice = 0;           // índice da câmera ou monitor
    int x = 0, y = 0;         // posição da região
    int largura = 0;          // largura da região
    int altura = 0;           // altura da região
    std::vector<ConfigFiltro> filtros;
    bool valida = false;
    
    Fonte() = default;
    
    // Cria fonte de câmera
    static Fonte criar_camera(int cam_indice) {
        Fonte f;
        f.tipo = TipoFonte::CAMERA;
        f.indice = cam_indice;
        f.valida = true;
        return f;
    }
    
    // Cria fonte de tela
    static Fonte criar_tela(int monitor_indice) {
        Fonte f;
        f.tipo = TipoFonte::TELA;
        f.indice = monitor_indice;
        f.valida = true;
        return f;
    }
    
    // Cria fonte de região
    static Fonte criar_regiao(int monitor_indice, int rx, int ry, int rlargura, int raltura) {
        Fonte f;
        f.tipo = TipoFonte::REGIAO;
        f.indice = monitor_indice;
        f.x = rx;
        f.y = ry;
        f.largura = rlargura;
        f.altura = raltura;
        f.valida = true;
        return f;
    }
    
    // Adiciona filtro
    Fonte com_filtro(TipoFiltro tipo, float v1 = 0, float v2 = 0) const {
        Fonte nova = *this;
        ConfigFiltro cf;
        cf.tipo = tipo;
        cf.valor1 = v1;
        cf.valor2 = v2;
        nova.filtros.push_back(cf);
        return nova;
    }
};

// Info do monitor
struct MonitorInfo {
    int indice;
    int x, y;
    int largura, altura;
    bool primario;
};

// Callback para enumeração de monitores
static std::vector<MonitorInfo> g_monitores;

static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
    MONITORINFOEX mi;
    mi.cbSize = sizeof(MONITORINFOEX);
    GetMonitorInfo(hMonitor, &mi);
    
    MonitorInfo info;
    info.indice = (int)g_monitores.size();
    info.x = mi.rcMonitor.left;
    info.y = mi.rcMonitor.top;
    info.largura = mi.rcMonitor.right - mi.rcMonitor.left;
    info.altura = mi.rcMonitor.bottom - mi.rcMonitor.top;
    info.primario = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    
    g_monitores.push_back(info);
    return TRUE;
}

class GerenciadorFontes {
private:
    std::vector<Fonte*> fontes;
    
public:
    static GerenciadorFontes& instancia() {
        static GerenciadorFontes inst;
        return inst;
    }
    
    // Lista monitores
    std::string listar_telas() {
        g_monitores.clear();
        EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, 0);
        
        std::string resultado;
        for (const auto& m : g_monitores) {
            if (!resultado.empty()) resultado += ",";
            resultado += std::to_string(m.indice) + ":" + 
                        std::to_string(m.largura) + "x" + 
                        std::to_string(m.altura);
        }
        return resultado;
    }
    
    // Obtém info do monitor
    bool obter_monitor(int indice, int& x, int& y, int& largura, int& altura) {
        g_monitores.clear();
        EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, 0);
        
        if (indice < 0 || indice >= (int)g_monitores.size()) {
            // Retorna tela virtual completa se índice inválido
            x = GetSystemMetrics(SM_XVIRTUALSCREEN);
            y = GetSystemMetrics(SM_YVIRTUALSCREEN);
            largura = GetSystemMetrics(SM_CXVIRTUALSCREEN);
            altura = GetSystemMetrics(SM_CYVIRTUALSCREEN);
            return false;
        }
        
        x = g_monitores[indice].x;
        y = g_monitores[indice].y;
        largura = g_monitores[indice].largura;
        altura = g_monitores[indice].altura;
        return true;
    }
    
    // Cria fonte e retorna ID
    int criar(const Fonte& fonte) {
        Fonte* f = new Fonte(fonte);
        int id = (int)fontes.size();
        fontes.push_back(f);
        return id;
    }
    
    // Obtém fonte por ID
    Fonte* obter(int id) {
        if (id < 0 || id >= (int)fontes.size()) return nullptr;
        return fontes[id];
    }
    
    // Clona fonte com filtro adicional
    int clonar_com_filtro(int id, TipoFiltro tipo, float v1 = 0, float v2 = 0) {
        Fonte* f = obter(id);
        if (!f) return -1;
        
        Fonte nova = f->com_filtro(tipo, v1, v2);
        return criar(nova);
    }
    
    // Libera fonte
    void liberar(int id) {
        if (id < 0 || id >= (int)fontes.size()) return;
        if (fontes[id]) {
            delete fontes[id];
            fontes[id] = nullptr;
        }
    }
    
    // Libera todas
    void liberar_todas() {
        for (auto f : fontes) {
            if (f) delete f;
        }
        fontes.clear();
    }
    
    ~GerenciadorFontes() {
        liberar_todas();
    }
};

} // namespace cvn

#endif // CVN_FONTE_HPP