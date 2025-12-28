// cvn.cpp
// Biblioteca de visão computacional para JPLang (Win32)

#include <windows.h>
#include <string>
#include <vector>
#include <variant>
#include <memory>

#include "imagem.hpp"
#include "janela.hpp"
#include "filtros.hpp"
#include "captura.hpp"
#include "camera.hpp"
#include "fonte.hpp"

// Forward declaration
struct Instancia;
using Var = std::variant<std::string, int, double, bool, std::shared_ptr<Instancia>>;

// Macro para exportar funções
#define EXPORTAR extern "C" __declspec(dllexport)

// === HELPERS ===
static int get_int(const Var& v) {
    if (auto* i = std::get_if<int>(&v)) return *i;
    if (auto* d = std::get_if<double>(&v)) return (int)*d;
    return 0;
}

static float get_float(const Var& v) {
    if (auto* d = std::get_if<double>(&v)) return (float)*d;
    if (auto* i = std::get_if<int>(&v)) return (float)*i;
    return 0.0f;
}

static std::string get_str(const Var& v) {
    if (auto* s = std::get_if<std::string>(&v)) return *s;
    if (auto* i = std::get_if<int>(&v)) return std::to_string(*i);
    if (auto* d = std::get_if<double>(&v)) return std::to_string(*d);
    if (auto* b = std::get_if<bool>(&v)) return *b ? "true" : "false";
    return "";
}

// === APLICA FILTROS EM IMAGEM ===
static unsigned char* aplicar_filtros(unsigned char* dados, int& largura, int& altura, int canais, 
                            const std::vector<cvn::ConfigFiltro>& filtros) {
    unsigned char* resultado = dados;
    
    for (const auto& f : filtros) {
        switch (f.tipo) {
            case cvn::TipoFiltro::CINZA:
                cvn::Filtros::cinza(resultado, largura, altura, canais);
                break;
            case cvn::TipoFiltro::INVERTER:
                cvn::Filtros::inverter(resultado, largura, altura, canais);
                break;
            case cvn::TipoFiltro::BRILHO:
                cvn::Filtros::brilho(resultado, largura, altura, canais, (int)f.valor1);
                break;
            case cvn::TipoFiltro::CONTRASTE:
                cvn::Filtros::contraste(resultado, largura, altura, canais, f.valor1);
                break;
            case cvn::TipoFiltro::LIMIAR:
                cvn::Filtros::limiar(resultado, largura, altura, canais, (int)f.valor1);
                break;
            case cvn::TipoFiltro::BLUR:
                cvn::Filtros::blur(resultado, largura, altura, canais, (int)f.valor1);
                break;
            case cvn::TipoFiltro::BORDAS:
                cvn::Filtros::bordas(resultado, largura, altura, canais);
                break;
            case cvn::TipoFiltro::SEPIA:
                cvn::Filtros::sepia(resultado, largura, altura, canais);
                break;
            case cvn::TipoFiltro::SATURACAO:
                cvn::Filtros::saturacao(resultado, largura, altura, canais, f.valor1);
                break;
            case cvn::TipoFiltro::FLIP_H:
                cvn::Filtros::flip_h(resultado, largura, altura, canais);
                break;
            case cvn::TipoFiltro::FLIP_V:
                cvn::Filtros::flip_v(resultado, largura, altura, canais);
                break;
            case cvn::TipoFiltro::REDIMENSIONAR: {
                int nova_largura = (int)f.valor1;
                int nova_altura = (int)f.valor2;
                unsigned char* novo = (unsigned char*)malloc(nova_largura * nova_altura * canais);
                if (novo) {
                    stbir_resize_uint8_linear(
                        resultado, largura, altura, largura * canais,
                        novo, nova_largura, nova_altura, nova_largura * canais,
                        STBIR_RGBA
                    );
                    if (resultado != dados) free(resultado);
                    resultado = novo;
                    largura = nova_largura;
                    altura = nova_altura;
                }
                break;
            }
            default:
                break;
        }
    }
    return resultado;
}

// === CAPTURA FRAME DA FONTE ===
static unsigned char* capturar_frame(cvn::Fonte* fonte, int& largura, int& altura) {
    if (!fonte || !fonte->valida) return nullptr;
    
    unsigned char* dados = nullptr;
    
    switch (fonte->tipo) {
        case cvn::TipoFonte::CAMERA: {
            dados = cvn::GerenciadorCameras::instancia().ler(fonte->indice, largura, altura);
            break;
        }
        case cvn::TipoFonte::TELA: {
            int mx, my, ml, ma;
            cvn::GerenciadorFontes::instancia().obter_monitor(fonte->indice, mx, my, ml, ma);
            dados = cvn::Captura::regiao(mx, my, ml, ma);
            largura = ml;
            altura = ma;
            break;
        }
        case cvn::TipoFonte::REGIAO: {
            int mx, my, ml, ma;
            cvn::GerenciadorFontes::instancia().obter_monitor(fonte->indice, mx, my, ml, ma);
            dados = cvn::Captura::regiao(mx + fonte->x, my + fonte->y, fonte->largura, fonte->altura);
            largura = fonte->largura;
            altura = fonte->altura;
            break;
        }
        default:
            return nullptr;
    }
    
    // Aplica filtros se houver
    if (dados && !fonte->filtros.empty()) {
        unsigned char* resultado = aplicar_filtros(dados, largura, altura, 4, fonte->filtros);
        if (resultado != dados) {
            free(dados);
            dados = resultado;
        }
    }
    
    return dados;
}

// === FUNÇÕES AUXILIARES ===
static int clonar_imagem(int id) {
    cvn::Imagem* img = cvn::GerenciadorImagens::instancia().obter(id);
    if (!img || !img->valida) return -1;
    return cvn::GerenciadorImagens::instancia().criar(img->dados, img->largura, img->altura, img->canais);
}

// === EXPORTS BÁSICOS ===

EXPORTAR Var cvn_ler(const std::vector<Var>& args) {
    if (args.empty()) return -1;
    std::string caminho = get_str(args[0]);
    return cvn::GerenciadorImagens::instancia().carregar(caminho);
}

EXPORTAR Var cvn_tamanho(const std::vector<Var>& args) {
    if (args.empty()) return std::string("0,0");
    int img_id = get_int(args[0]);
    return cvn::GerenciadorImagens::instancia().tamanho(img_id);
}

EXPORTAR Var cvn_redimensionar(const std::vector<Var>& args) {
    if (args.size() < 3) return -1;
    int img_id = get_int(args[0]);
    int largura = get_int(args[1]);
    int altura = get_int(args[2]);
    return cvn::GerenciadorImagens::instancia().redimensionar(img_id, largura, altura);
}

EXPORTAR Var cvn_salvar(const std::vector<Var>& args) {
    if (args.size() < 2) return false;
    int img_id = get_int(args[0]);
    std::string caminho = get_str(args[1]);
    return cvn::GerenciadorImagens::instancia().salvar(img_id, caminho);
}

EXPORTAR Var cvn_exibir(const std::vector<Var>& args) {
    if (args.size() < 2) return false;
    std::string titulo = get_str(args[0]);
    int img_id = get_int(args[1]);
    cvn::Imagem* img = cvn::GerenciadorImagens::instancia().obter(img_id);
    if (!img || !img->valida) return false;
    return cvn::GerenciadorJanelas::instancia().atualizar(titulo, img->dados, img->largura, img->altura);
}

EXPORTAR Var cvn_esperar(const std::vector<Var>& args) {
    int ms = 0;
    if (!args.empty()) ms = get_int(args[0]);
    return cvn::GerenciadorJanelas::instancia().esperar(ms);
}

EXPORTAR Var cvn_liberar(const std::vector<Var>& args) {
    if (args.empty()) return false;
    int img_id = get_int(args[0]);
    cvn::GerenciadorImagens::instancia().liberar(img_id);
    return true;
}

EXPORTAR Var cvn_fechar(const std::vector<Var>& args) {
    if (args.empty()) return false;
    std::string titulo = get_str(args[0]);
    return cvn::GerenciadorJanelas::instancia().fechar(titulo);
}

EXPORTAR Var cvn_fechar_todas(const std::vector<Var>& args) {
    cvn::GerenciadorJanelas::instancia().fechar_todas();
    return true;
}

// === FONTES ===

// Lista monitores disponíveis
EXPORTAR Var cvn_listar_telas(const std::vector<Var>& args) {
    return cvn::GerenciadorFontes::instancia().listar_telas();
}

// Cria fonte de câmera
EXPORTAR Var cvn_camera(const std::vector<Var>& args) {
    int indice = 0;
    if (!args.empty()) indice = get_int(args[0]);
    
    // Abre a câmera
    int cam_id = cvn::GerenciadorCameras::instancia().abrir(indice);
    if (cam_id < 0) return -1;
    
    // Cria fonte
    cvn::Fonte fonte = cvn::Fonte::criar_camera(cam_id);
    return cvn::GerenciadorFontes::instancia().criar(fonte);
}

// Cria fonte de tela
EXPORTAR Var cvn_tela(const std::vector<Var>& args) {
    int indice = 0;
    if (!args.empty()) indice = get_int(args[0]);
    
    cvn::Fonte fonte = cvn::Fonte::criar_tela(indice);
    return cvn::GerenciadorFontes::instancia().criar(fonte);
}

// Cria fonte de região
EXPORTAR Var cvn_regiao(const std::vector<Var>& args) {
    if (args.size() < 5) return -1;
    int monitor = get_int(args[0]);
    int x = get_int(args[1]);
    int y = get_int(args[2]);
    int largura = get_int(args[3]);
    int altura = get_int(args[4]);
    
    cvn::Fonte fonte = cvn::Fonte::criar_regiao(monitor, x, y, largura, altura);
    return cvn::GerenciadorFontes::instancia().criar(fonte);
}

// Lista câmeras
EXPORTAR Var cvn_listar_cameras(const std::vector<Var>& args) {
    return cvn::GerenciadorCameras::instancia().listar();
}

// Reproduz fonte continuamente
EXPORTAR Var cvn_reproduzir(const std::vector<Var>& args) {
    if (args.size() < 3) return false;
    
    std::string titulo = get_str(args[0]);
    int fonte_id = get_int(args[1]);
    int ms = get_int(args[2]);
    
    cvn::Fonte* fonte = cvn::GerenciadorFontes::instancia().obter(fonte_id);
    if (!fonte || !fonte->valida) return false;
    
    // Espera primeiro frame da câmera (pode demorar um pouco)
    int tentativas = 0;
    int largura = 0, altura = 0;
    unsigned char* dados = nullptr;
    
    while (!dados && tentativas < 100) {
        dados = capturar_frame(fonte, largura, altura);
        if (!dados) {
            Sleep(50);
            tentativas++;
        }
    }
    
    if (!dados) return false;
    
    // Cria janela com primeiro frame
    cvn::GerenciadorJanelas::instancia().atualizar(titulo, dados, largura, altura);
    free(dados);
    
    // Loop de reprodução
    while (true) {
        // Processa mensagens e verifica se deve continuar
        if (!cvn::GerenciadorJanelas::instancia().processar_mensagens(titulo)) {
            break;
        }
        
        dados = capturar_frame(fonte, largura, altura);
        if (dados) {
            cvn::GerenciadorJanelas::instancia().atualizar(titulo, dados, largura, altura);
            free(dados);
        }
        
        Sleep(ms);
    }
    
    // Fecha câmera se for fonte de câmera
    if (fonte->tipo == cvn::TipoFonte::CAMERA) {
        cvn::GerenciadorCameras::instancia().fechar(fonte->indice);
    }
    
    return true;
}

// Fecha câmera
EXPORTAR Var cvn_camera_fechar(const std::vector<Var>& args) {
    if (args.empty()) return false;
    int fonte_id = get_int(args[0]);
    
    cvn::Fonte* fonte = cvn::GerenciadorFontes::instancia().obter(fonte_id);
    if (fonte && fonte->tipo == cvn::TipoFonte::CAMERA) {
        cvn::GerenciadorCameras::instancia().fechar(fonte->indice);
    }
    cvn::GerenciadorFontes::instancia().liberar(fonte_id);
    return true;
}

// === FILTROS EM FONTES ===

EXPORTAR Var cvn_fonte_cinza(const std::vector<Var>& args) {
    if (args.empty()) return -1;
    return cvn::GerenciadorFontes::instancia().clonar_com_filtro(get_int(args[0]), cvn::TipoFiltro::CINZA);
}

EXPORTAR Var cvn_fonte_inverter(const std::vector<Var>& args) {
    if (args.empty()) return -1;
    return cvn::GerenciadorFontes::instancia().clonar_com_filtro(get_int(args[0]), cvn::TipoFiltro::INVERTER);
}

EXPORTAR Var cvn_fonte_brilho(const std::vector<Var>& args) {
    if (args.size() < 2) return -1;
    return cvn::GerenciadorFontes::instancia().clonar_com_filtro(get_int(args[0]), cvn::TipoFiltro::BRILHO, get_float(args[1]));
}

EXPORTAR Var cvn_fonte_contraste(const std::vector<Var>& args) {
    if (args.size() < 2) return -1;
    return cvn::GerenciadorFontes::instancia().clonar_com_filtro(get_int(args[0]), cvn::TipoFiltro::CONTRASTE, get_float(args[1]));
}

EXPORTAR Var cvn_fonte_blur(const std::vector<Var>& args) {
    if (args.size() < 2) return -1;
    return cvn::GerenciadorFontes::instancia().clonar_com_filtro(get_int(args[0]), cvn::TipoFiltro::BLUR, get_float(args[1]));
}

EXPORTAR Var cvn_fonte_bordas(const std::vector<Var>& args) {
    if (args.empty()) return -1;
    return cvn::GerenciadorFontes::instancia().clonar_com_filtro(get_int(args[0]), cvn::TipoFiltro::BORDAS);
}

EXPORTAR Var cvn_fonte_sepia(const std::vector<Var>& args) {
    if (args.empty()) return -1;
    return cvn::GerenciadorFontes::instancia().clonar_com_filtro(get_int(args[0]), cvn::TipoFiltro::SEPIA);
}

EXPORTAR Var cvn_fonte_saturacao(const std::vector<Var>& args) {
    if (args.size() < 2) return -1;
    return cvn::GerenciadorFontes::instancia().clonar_com_filtro(get_int(args[0]), cvn::TipoFiltro::SATURACAO, get_float(args[1]));
}

EXPORTAR Var cvn_fonte_flip_h(const std::vector<Var>& args) {
    if (args.empty()) return -1;
    return cvn::GerenciadorFontes::instancia().clonar_com_filtro(get_int(args[0]), cvn::TipoFiltro::FLIP_H);
}

EXPORTAR Var cvn_fonte_flip_v(const std::vector<Var>& args) {
    if (args.empty()) return -1;
    return cvn::GerenciadorFontes::instancia().clonar_com_filtro(get_int(args[0]), cvn::TipoFiltro::FLIP_V);
}

EXPORTAR Var cvn_fonte_redimensionar(const std::vector<Var>& args) {
    if (args.size() < 3) return -1;
    return cvn::GerenciadorFontes::instancia().clonar_com_filtro(get_int(args[0]), cvn::TipoFiltro::REDIMENSIONAR, get_float(args[1]), get_float(args[2]));
}

// === FILTROS EM IMAGENS (mantidos para compatibilidade) ===

EXPORTAR Var cvn_cinza(const std::vector<Var>& args) {
    if (args.empty()) return -1;
    int novo_id = clonar_imagem(get_int(args[0]));
    if (novo_id == -1) return -1;
    cvn::Imagem* img = cvn::GerenciadorImagens::instancia().obter(novo_id);
    cvn::Filtros::cinza(img->dados, img->largura, img->altura, img->canais);
    return novo_id;
}

EXPORTAR Var cvn_inverter(const std::vector<Var>& args) {
    if (args.empty()) return -1;
    int novo_id = clonar_imagem(get_int(args[0]));
    if (novo_id == -1) return -1;
    cvn::Imagem* img = cvn::GerenciadorImagens::instancia().obter(novo_id);
    cvn::Filtros::inverter(img->dados, img->largura, img->altura, img->canais);
    return novo_id;
}

EXPORTAR Var cvn_brilho(const std::vector<Var>& args) {
    if (args.size() < 2) return -1;
    int novo_id = clonar_imagem(get_int(args[0]));
    if (novo_id == -1) return -1;
    cvn::Imagem* img = cvn::GerenciadorImagens::instancia().obter(novo_id);
    cvn::Filtros::brilho(img->dados, img->largura, img->altura, img->canais, get_int(args[1]));
    return novo_id;
}

EXPORTAR Var cvn_contraste(const std::vector<Var>& args) {
    if (args.size() < 2) return -1;
    int novo_id = clonar_imagem(get_int(args[0]));
    if (novo_id == -1) return -1;
    cvn::Imagem* img = cvn::GerenciadorImagens::instancia().obter(novo_id);
    cvn::Filtros::contraste(img->dados, img->largura, img->altura, img->canais, get_float(args[1]));
    return novo_id;
}

EXPORTAR Var cvn_limiar(const std::vector<Var>& args) {
    if (args.size() < 2) return -1;
    int novo_id = clonar_imagem(get_int(args[0]));
    if (novo_id == -1) return -1;
    cvn::Imagem* img = cvn::GerenciadorImagens::instancia().obter(novo_id);
    cvn::Filtros::limiar(img->dados, img->largura, img->altura, img->canais, get_int(args[1]));
    return novo_id;
}

EXPORTAR Var cvn_blur(const std::vector<Var>& args) {
    if (args.size() < 2) return -1;
    int novo_id = clonar_imagem(get_int(args[0]));
    if (novo_id == -1) return -1;
    cvn::Imagem* img = cvn::GerenciadorImagens::instancia().obter(novo_id);
    cvn::Filtros::blur(img->dados, img->largura, img->altura, img->canais, get_int(args[1]));
    return novo_id;
}

EXPORTAR Var cvn_bordas(const std::vector<Var>& args) {
    if (args.empty()) return -1;
    int novo_id = clonar_imagem(get_int(args[0]));
    if (novo_id == -1) return -1;
    cvn::Imagem* img = cvn::GerenciadorImagens::instancia().obter(novo_id);
    cvn::Filtros::bordas(img->dados, img->largura, img->altura, img->canais);
    return novo_id;
}

EXPORTAR Var cvn_sepia(const std::vector<Var>& args) {
    if (args.empty()) return -1;
    int novo_id = clonar_imagem(get_int(args[0]));
    if (novo_id == -1) return -1;
    cvn::Imagem* img = cvn::GerenciadorImagens::instancia().obter(novo_id);
    cvn::Filtros::sepia(img->dados, img->largura, img->altura, img->canais);
    return novo_id;
}

EXPORTAR Var cvn_saturacao(const std::vector<Var>& args) {
    if (args.size() < 2) return -1;
    int novo_id = clonar_imagem(get_int(args[0]));
    if (novo_id == -1) return -1;
    cvn::Imagem* img = cvn::GerenciadorImagens::instancia().obter(novo_id);
    cvn::Filtros::saturacao(img->dados, img->largura, img->altura, img->canais, get_float(args[1]));
    return novo_id;
}

EXPORTAR Var cvn_flip_h(const std::vector<Var>& args) {
    if (args.empty()) return -1;
    int novo_id = clonar_imagem(get_int(args[0]));
    if (novo_id == -1) return -1;
    cvn::Imagem* img = cvn::GerenciadorImagens::instancia().obter(novo_id);
    cvn::Filtros::flip_h(img->dados, img->largura, img->altura, img->canais);
    return novo_id;
}

EXPORTAR Var cvn_flip_v(const std::vector<Var>& args) {
    if (args.empty()) return -1;
    int novo_id = clonar_imagem(get_int(args[0]));
    if (novo_id == -1) return -1;
    cvn::Imagem* img = cvn::GerenciadorImagens::instancia().obter(novo_id);
    cvn::Filtros::flip_v(img->dados, img->largura, img->altura, img->canais);
    return novo_id;
}

EXPORTAR Var cvn_rotacionar(const std::vector<Var>& args) {
    if (args.size() < 2) return -1;
    int img_id = get_int(args[0]);
    int graus = get_int(args[1]);
    cvn::Imagem* img = cvn::GerenciadorImagens::instancia().obter(img_id);
    if (!img || !img->valida) return -1;
    int largura = img->largura;
    int altura = img->altura;
    unsigned char* dados_novos = cvn::Filtros::rotacionar(img->dados, largura, altura, img->canais, graus);
    if (!dados_novos) return -1;
    int novo_id = cvn::GerenciadorImagens::instancia().criar(dados_novos, largura, altura, img->canais);
    free(dados_novos);
    return novo_id;
}

// Funções legadas de captura (mantidas para compatibilidade)
EXPORTAR Var cvn_capturar_tela(const std::vector<Var>& args) {
    int largura, altura;
    unsigned char* dados = cvn::Captura::tela(largura, altura);
    if (!dados) return -1;
    int id = cvn::GerenciadorImagens::instancia().criar(dados, largura, altura, 4);
    free(dados);
    return id;
}

EXPORTAR Var cvn_capturar_regiao(const std::vector<Var>& args) {
    if (args.size() < 4) return -1;
    int x = get_int(args[0]);
    int y = get_int(args[1]);
    int largura = get_int(args[2]);
    int altura = get_int(args[3]);
    unsigned char* dados = cvn::Captura::regiao(x, y, largura, altura);
    if (!dados) return -1;
    int id = cvn::GerenciadorImagens::instancia().criar(dados, largura, altura, 4);
    free(dados);
    return id;
}

EXPORTAR Var cvn_capturar_janela(const std::vector<Var>& args) {
    if (args.empty()) return -1;
    std::string titulo = get_str(args[0]);
    int largura, altura;
    unsigned char* dados = cvn::Captura::janela(titulo, largura, altura);
    if (!dados) return -1;
    int id = cvn::GerenciadorImagens::instancia().criar(dados, largura, altura, 4);
    free(dados);
    return id;
}