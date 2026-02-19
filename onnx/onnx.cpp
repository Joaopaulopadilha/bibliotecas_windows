// onnx.cpp
// Biblioteca JPLang para deteccao de objetos usando ONNX Runtime + OpenCV
//
// Compilar Windows:
//   g++ -shared -o bibliotecas/onnx/onnx.jpd bibliotecas/onnx/onnx.cpp "-Ibibliotecas/onnx/include" "-Ibibliotecas/onnx/onnxruntime-win-x64-1.24.1/include" "-Lbibliotecas/onnx/lib" "-Lbibliotecas/onnx/onnxruntime-win-x64-1.24.1/lib" bibliotecas/onnx/onnxruntime-win-x64-1.24.1/lib/onnxruntime.dll -lopencv_core455 -lopencv_imgcodecs455 -lopencv_imgproc455 -lopencv_highgui455 -lopencv_videoio455 -lgdi32 -O3
//
// Compilar Linux:
//   g++ -shared -fPIC -o bibliotecas/onnx/libonnx.jpd bibliotecas/onnx/onnx.cpp -Ibibliotecas/onnx/include -Ibibliotecas/onnx/onnxruntime-linux-x64-1.24.1/include -Lbibliotecas/onnx/lib -Lbibliotecas/onnx/onnxruntime-linux-x64-1.24.1/lib -lopencv_core -lopencv_imgcodecs -lopencv_imgproc -lopencv_highgui -lopencv_videoio -lonnxruntime -O3

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

// ONNX Runtime C API (path resolvido via -I no comando de compilacao)
#include "onnxruntime_c_api.h"

// stb_image para carregar imagens (header-only)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#include "stb_image.h"

// stb_image_write para salvar imagens (header-only)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// OpenCV para camera, janela e captura de tela
#include <opencv2/opencv.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/videoio.hpp>

// === WINDOWS ===
#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
#endif

// === EXPORT ===
#if defined(_WIN32) || defined(_WIN64)
    #define JP_EXPORT extern "C" __declspec(dllexport)
#else
    #define JP_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// === TIPOS (nao alterar) ===
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

// === HELPERS ===
static JPValor jp_int(int64_t v) {
    JPValor r;
    r.tipo = JP_TIPO_INT;
    r.valor.inteiro = v;
    return r;
}

static JPValor jp_double(double v) {
    JPValor r;
    r.tipo = JP_TIPO_DOUBLE;
    r.valor.decimal = v;
    return r;
}

static JPValor jp_string(const char* s) {
    JPValor r;
    r.tipo = JP_TIPO_STRING;
    size_t len = strlen(s);
    r.valor.texto = (char*)malloc(len + 1);
    if (r.valor.texto) memcpy(r.valor.texto, s, len + 1);
    return r;
}

static JPValor jp_bool(int v) {
    JPValor r;
    r.tipo = JP_TIPO_BOOL;
    r.valor.booleano = v;
    return r;
}

static JPValor jp_nulo() {
    JPValor r;
    r.tipo = JP_TIPO_NULO;
    r.valor.inteiro = 0;
    return r;
}

static int64_t get_int(JPValor* args, int idx) {
    if (args[idx].tipo == JP_TIPO_INT) return args[idx].valor.inteiro;
    if (args[idx].tipo == JP_TIPO_DOUBLE) return (int64_t)args[idx].valor.decimal;
    return 0;
}

static double get_double(JPValor* args, int idx) {
    if (args[idx].tipo == JP_TIPO_DOUBLE) return args[idx].valor.decimal;
    if (args[idx].tipo == JP_TIPO_INT) return (double)args[idx].valor.inteiro;
    return 0.0;
}

static const char* get_string(JPValor* args, int idx) {
    if (args[idx].tipo == JP_TIPO_STRING && args[idx].valor.texto) {
        return args[idx].valor.texto;
    }
    return "";
}

static int get_bool(JPValor* args, int idx) {
    if (args[idx].tipo == JP_TIPO_BOOL) return args[idx].valor.booleano;
    if (args[idx].tipo == JP_TIPO_INT) return args[idx].valor.inteiro != 0;
    return 0;
}

// ============================================================
// ESTADO GLOBAL DA BIBLIOTECA
// ============================================================

struct Deteccao {
    int classe_id;
    float confianca;
    float x, y, w, h;  // bounding box (pixels da imagem original)
};

struct Sessao {
    OrtSession* session;
    OrtSessionOptions* session_options;
    int64_t input_w;
    int64_t input_h;
    int64_t num_classes;
    int64_t num_deteccoes;
    bool ativo;
};

static const OrtApi* g_ort = nullptr;
static OrtEnv* g_env = nullptr;
static bool g_inicializado = false;

static const int MAX_SESSOES = 16;
static Sessao g_sessoes[MAX_SESSOES];

static std::vector<Deteccao> g_resultados;
static float g_confianca_min = 0.5f;
static float g_nms_threshold = 0.45f;

// ============================================================
// FUNCOES INTERNAS
// ============================================================

static bool inicializar_ort() {
    if (g_inicializado) return true;

    g_ort = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    if (!g_ort) return false;

    OrtStatus* status = g_ort->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "jplang_onnx", &g_env);
    if (status) {
        g_ort->ReleaseStatus(status);
        return false;
    }

    for (int i = 0; i < MAX_SESSOES; i++) {
        g_sessoes[i].session = nullptr;
        g_sessoes[i].session_options = nullptr;
        g_sessoes[i].ativo = false;
    }

    g_inicializado = true;
    return true;
}

static int encontrar_sessao_livre() {
    for (int i = 0; i < MAX_SESSOES; i++) {
        if (!g_sessoes[i].ativo) return i;
    }
    return -1;
}

// Pre-processar imagem: carrega, redimensiona e normaliza para tensor NCHW float [1,3,H,W]
static float* preprocessar_imagem(const char* caminho, int target_w, int target_h, int* img_w_orig, int* img_h_orig) {
    int w, h, canais;
    unsigned char* img = stbi_load(caminho, &w, &h, &canais, 3);  // forcar RGB
    if (!img) return nullptr;

    *img_w_orig = w;
    *img_h_orig = h;

    // Alocar tensor NCHW: [1, 3, target_h, target_w]
    size_t tensor_size = 1 * 3 * target_h * target_w;
    float* tensor = (float*)malloc(tensor_size * sizeof(float));
    if (!tensor) {
        stbi_image_free(img);
        return nullptr;
    }

    // Redimensionar com interpolacao bilinear e normalizar (0-255 -> 0.0-1.0)
    float scale_x = (float)w / target_w;
    float scale_y = (float)h / target_h;

    for (int c = 0; c < 3; c++) {
        for (int y = 0; y < target_h; y++) {
            for (int x = 0; x < target_w; x++) {
                float src_x = x * scale_x;
                float src_y = y * scale_y;

                int x0 = (int)src_x;
                int y0 = (int)src_y;
                int x1 = std::min(x0 + 1, w - 1);
                int y1 = std::min(y0 + 1, h - 1);

                float fx = src_x - x0;
                float fy = src_y - y0;

                // Interpolacao bilinear
                float v00 = img[(y0 * w + x0) * 3 + c];
                float v10 = img[(y0 * w + x1) * 3 + c];
                float v01 = img[(y1 * w + x0) * 3 + c];
                float v11 = img[(y1 * w + x1) * 3 + c];

                float val = v00 * (1 - fx) * (1 - fy) + v10 * fx * (1 - fy) +
                            v01 * (1 - fx) * fy + v11 * fx * fy;

                tensor[c * target_h * target_w + y * target_w + x] = val / 255.0f;
            }
        }
    }

    stbi_image_free(img);
    return tensor;
}

// NMS (Non-Maximum Suppression) para filtrar deteccoes sobrepostas
static float iou(const Deteccao& a, const Deteccao& b) {
    float ax1 = a.x, ay1 = a.y, ax2 = a.x + a.w, ay2 = a.y + a.h;
    float bx1 = b.x, by1 = b.y, bx2 = b.x + b.w, by2 = b.y + b.h;

    float inter_x1 = std::max(ax1, bx1);
    float inter_y1 = std::max(ay1, by1);
    float inter_x2 = std::min(ax2, bx2);
    float inter_y2 = std::min(ay2, by2);

    float inter_area = std::max(0.0f, inter_x2 - inter_x1) * std::max(0.0f, inter_y2 - inter_y1);
    float union_area = a.w * a.h + b.w * b.h - inter_area;

    return (union_area > 0) ? inter_area / union_area : 0.0f;
}

static void nms(std::vector<Deteccao>& dets, float threshold) {
    std::sort(dets.begin(), dets.end(), [](const Deteccao& a, const Deteccao& b) {
        return a.confianca > b.confianca;
    });

    std::vector<bool> suprimido(dets.size(), false);
    std::vector<Deteccao> resultado;

    for (size_t i = 0; i < dets.size(); i++) {
        if (suprimido[i]) continue;
        resultado.push_back(dets[i]);
        for (size_t j = i + 1; j < dets.size(); j++) {
            if (suprimido[j]) continue;
            if (dets[i].classe_id == dets[j].classe_id && iou(dets[i], dets[j]) > threshold) {
                suprimido[j] = true;
            }
        }
    }

    dets = resultado;
}

// Pos-processar saida YOLOv8: tensor [1, 84, 8400] -> lista de deteccoes
// Formato: 84 = 4 (cx, cy, w, h) + 80 (scores das classes)
static void posprocessar_yolov8(const float* output, int num_classes, int num_dets,
                                  int img_w, int img_h, int input_w, int input_h) {
    g_resultados.clear();

    float scale_x = (float)img_w / input_w;
    float scale_y = (float)img_h / input_h;

    int total_campos = 4 + num_classes;  // 4 coords + N classes

    for (int d = 0; d < num_dets; d++) {
        // YOLOv8 saida transposta: [1, 84, 8400]
        // output[campo * num_dets + d]
        float cx = output[0 * num_dets + d];
        float cy = output[1 * num_dets + d];
        float bw = output[2 * num_dets + d];
        float bh = output[3 * num_dets + d];

        // Encontrar classe com maior score
        int melhor_classe = 0;
        float melhor_score = 0.0f;
        for (int c = 0; c < num_classes; c++) {
            float score = output[(4 + c) * num_dets + d];
            if (score > melhor_score) {
                melhor_score = score;
                melhor_classe = c;
            }
        }

        if (melhor_score < g_confianca_min) continue;

        Deteccao det;
        det.classe_id = melhor_classe;
        det.confianca = melhor_score;

        // Converter de centro para canto e escalar para imagem original
        det.x = (cx - bw / 2.0f) * scale_x;
        det.y = (cy - bh / 2.0f) * scale_y;
        det.w = bw * scale_x;
        det.h = bh * scale_y;

        // Clamp
        det.x = std::max(0.0f, det.x);
        det.y = std::max(0.0f, det.y);
        if (det.x + det.w > img_w) det.w = img_w - det.x;
        if (det.y + det.h > img_h) det.h = img_h - det.y;

        g_resultados.push_back(det);
    }

    nms(g_resultados, g_nms_threshold);
}

// ============================================================
// FUNCOES EXPORTADAS
// ============================================================

// onnx_carregar(caminho_modelo)
// Carrega um modelo .onnx e retorna o ID da sessao (0+), ou -1 se falhou
JP_EXPORT JPValor onnx_carregar(JPValor* args, int numArgs) {
    if (numArgs < 1) return jp_int(-1);

    if (!inicializar_ort()) return jp_int(-1);

    int id = encontrar_sessao_livre();
    if (id < 0) return jp_int(-1);

    const char* caminho = get_string(args, 0);
    Sessao& s = g_sessoes[id];

    OrtStatus* status = nullptr;

    // Criar opcoes de sessao
    status = g_ort->CreateSessionOptions(&s.session_options);
    if (status) {
        g_ort->ReleaseStatus(status);
        return jp_int(-1);
    }

    g_ort->SetIntraOpNumThreads(s.session_options, 4);
    g_ort->SetSessionGraphOptimizationLevel(s.session_options, ORT_ENABLE_ALL);

    // Criar sessao
#if defined(_WIN32) || defined(_WIN64)
    // Windows: converter para wchar_t
    int len = MultiByteToWideChar(CP_UTF8, 0, caminho, -1, NULL, 0);
    wchar_t* wcaminho = (wchar_t*)malloc(len * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, caminho, -1, wcaminho, len);
    status = g_ort->CreateSession(g_env, wcaminho, s.session_options, &s.session);
    free(wcaminho);
#else
    status = g_ort->CreateSession(g_env, caminho, s.session_options, &s.session);
#endif

    if (status) {
        g_ort->ReleaseStatus(status);
        g_ort->ReleaseSessionOptions(s.session_options);
        s.session_options = nullptr;
        return jp_int(-1);
    }

    // Obter dimensoes do input (assumindo [1, 3, H, W])
    OrtTypeInfo* input_type_info = nullptr;
    status = g_ort->SessionGetInputTypeInfo(s.session, 0, &input_type_info);
    if (status) {
        g_ort->ReleaseStatus(status);
        s.input_h = 640;
        s.input_w = 640;
    } else {
        const OrtTensorTypeAndShapeInfo* tensor_info = nullptr;
        g_ort->CastTypeInfoToTensorInfo(input_type_info, &tensor_info);

        size_t num_dims = 0;
        g_ort->GetDimensionsCount(tensor_info, &num_dims);

        std::vector<int64_t> dims(num_dims);
        g_ort->GetDimensions(tensor_info, dims.data(), num_dims);

        // [1, 3, H, W]
        s.input_h = (num_dims >= 3 && dims[2] > 0) ? dims[2] : 640;
        s.input_w = (num_dims >= 4 && dims[3] > 0) ? dims[3] : 640;

        g_ort->ReleaseTypeInfo(input_type_info);
    }

    // Obter dimensoes do output para saber num_classes e num_deteccoes
    OrtTypeInfo* output_type_info = nullptr;
    status = g_ort->SessionGetOutputTypeInfo(s.session, 0, &output_type_info);
    if (status) {
        g_ort->ReleaseStatus(status);
        s.num_classes = 80;
        s.num_deteccoes = 8400;
    } else {
        const OrtTensorTypeAndShapeInfo* tensor_info = nullptr;
        g_ort->CastTypeInfoToTensorInfo(output_type_info, &tensor_info);

        size_t num_dims = 0;
        g_ort->GetDimensionsCount(tensor_info, &num_dims);

        std::vector<int64_t> dims(num_dims);
        g_ort->GetDimensions(tensor_info, dims.data(), num_dims);

        // YOLOv8: [1, 84, 8400]
        if (num_dims >= 3) {
            s.num_classes = (dims[1] > 4) ? dims[1] - 4 : 80;
            s.num_deteccoes = (dims[2] > 0) ? dims[2] : 8400;
        } else {
            s.num_classes = 80;
            s.num_deteccoes = 8400;
        }

        g_ort->ReleaseTypeInfo(output_type_info);
    }

    s.ativo = true;
    return jp_int(id);
}

// onnx_detectar(id_sessao, caminho_imagem)
// Roda inferencia e retorna a quantidade de deteccoes encontradas
JP_EXPORT JPValor onnx_detectar(JPValor* args, int numArgs) {
    if (numArgs < 2) return jp_int(-1);
    if (!g_inicializado) return jp_int(-1);

    int id = (int)get_int(args, 0);
    const char* caminho_img = get_string(args, 1);

    if (id < 0 || id >= MAX_SESSOES || !g_sessoes[id].ativo) return jp_int(-1);

    Sessao& s = g_sessoes[id];
    g_resultados.clear();

    // Pre-processar imagem
    int img_w, img_h;
    float* input_tensor = preprocessar_imagem(caminho_img, (int)s.input_w, (int)s.input_h, &img_w, &img_h);
    if (!input_tensor) return jp_int(-1);

    // Criar tensor de input
    OrtMemoryInfo* mem_info = nullptr;
    OrtStatus* status = g_ort->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &mem_info);
    if (status) {
        g_ort->ReleaseStatus(status);
        free(input_tensor);
        return jp_int(-1);
    }

    int64_t input_shape[] = {1, 3, s.input_h, s.input_w};
    size_t input_size = 1 * 3 * s.input_h * s.input_w * sizeof(float);

    OrtValue* input_ort = nullptr;
    status = g_ort->CreateTensorWithDataAsOrtValue(
        mem_info, input_tensor, input_size,
        input_shape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
        &input_ort
    );
    g_ort->ReleaseMemoryInfo(mem_info);

    if (status) {
        g_ort->ReleaseStatus(status);
        free(input_tensor);
        return jp_int(-1);
    }

    // Obter nomes de input/output
    OrtAllocator* allocator = nullptr;
    g_ort->GetAllocatorWithDefaultOptions(&allocator);

    char* input_name = nullptr;
    g_ort->SessionGetInputName(s.session, 0, allocator, &input_name);

    char* output_name = nullptr;
    g_ort->SessionGetOutputName(s.session, 0, allocator, &output_name);

    const char* input_names[] = {input_name};
    const char* output_names[] = {output_name};

    // Rodar inferencia
    OrtValue* output_ort = nullptr;
    status = g_ort->Run(s.session, nullptr, input_names, (const OrtValue* const*)&input_ort, 1, output_names, 1, &output_ort);

    // Liberar nomes
    if (input_name) g_ort->AllocatorFree(allocator, input_name);
    if (output_name) g_ort->AllocatorFree(allocator, output_name);

    g_ort->ReleaseValue(input_ort);
    free(input_tensor);

    if (status) {
        g_ort->ReleaseStatus(status);
        return jp_int(-1);
    }

    // Ler saida
    float* output_data = nullptr;
    g_ort->GetTensorMutableData(output_ort, (void**)&output_data);

    if (output_data) {
        posprocessar_yolov8(output_data, (int)s.num_classes, (int)s.num_deteccoes,
                            img_w, img_h, (int)s.input_w, (int)s.input_h);
    }

    g_ort->ReleaseValue(output_ort);

    return jp_int((int64_t)g_resultados.size());
}

// onnx_classe(indice)
// Retorna o ID da classe da deteccao
JP_EXPORT JPValor onnx_classe(JPValor* args, int numArgs) {
    if (numArgs < 1) return jp_int(-1);
    int idx = (int)get_int(args, 0);
    if (idx < 0 || idx >= (int)g_resultados.size()) return jp_int(-1);
    return jp_int(g_resultados[idx].classe_id);
}

// onnx_confianca(indice)
// Retorna a confianca da deteccao (0.0 a 1.0)
JP_EXPORT JPValor onnx_confianca(JPValor* args, int numArgs) {
    if (numArgs < 1) return jp_double(0.0);
    int idx = (int)get_int(args, 0);
    if (idx < 0 || idx >= (int)g_resultados.size()) return jp_double(0.0);
    return jp_double((double)g_resultados[idx].confianca);
}

// onnx_x(indice)
// Retorna X do bounding box
JP_EXPORT JPValor onnx_x(JPValor* args, int numArgs) {
    if (numArgs < 1) return jp_double(0.0);
    int idx = (int)get_int(args, 0);
    if (idx < 0 || idx >= (int)g_resultados.size()) return jp_double(0.0);
    return jp_double((double)g_resultados[idx].x);
}

// onnx_y(indice)
// Retorna Y do bounding box
JP_EXPORT JPValor onnx_y(JPValor* args, int numArgs) {
    if (numArgs < 1) return jp_double(0.0);
    int idx = (int)get_int(args, 0);
    if (idx < 0 || idx >= (int)g_resultados.size()) return jp_double(0.0);
    return jp_double((double)g_resultados[idx].y);
}

// onnx_w(indice)
// Retorna largura do bounding box
JP_EXPORT JPValor onnx_w(JPValor* args, int numArgs) {
    if (numArgs < 1) return jp_double(0.0);
    int idx = (int)get_int(args, 0);
    if (idx < 0 || idx >= (int)g_resultados.size()) return jp_double(0.0);
    return jp_double((double)g_resultados[idx].w);
}

// onnx_h(indice)
// Retorna altura do bounding box
JP_EXPORT JPValor onnx_h(JPValor* args, int numArgs) {
    if (numArgs < 1) return jp_double(0.0);
    int idx = (int)get_int(args, 0);
    if (idx < 0 || idx >= (int)g_resultados.size()) return jp_double(0.0);
    return jp_double((double)g_resultados[idx].h);
}

// onnx_confianca_minima(valor)
// Define o threshold minimo de confianca para deteccoes
JP_EXPORT JPValor onnx_confianca_minima(JPValor* args, int numArgs) {
    if (numArgs < 1) return jp_double((double)g_confianca_min);
    g_confianca_min = (float)get_double(args, 0);
    return jp_double((double)g_confianca_min);
}

// onnx_nms(valor)
// Define o threshold de NMS (Non-Maximum Suppression)
JP_EXPORT JPValor onnx_nms(JPValor* args, int numArgs) {
    if (numArgs < 1) return jp_double((double)g_nms_threshold);
    g_nms_threshold = (float)get_double(args, 0);
    return jp_double((double)g_nms_threshold);
}

// onnx_desenhar(caminho_imagem_original, caminho_saida, espessura)
// Desenha bounding boxes nas deteccoes atuais e salva uma copia da imagem
// Retorna 1 se salvou com sucesso, 0 se falhou
JP_EXPORT JPValor onnx_desenhar(JPValor* args, int numArgs) {
    if (numArgs < 2) return jp_bool(0);

    const char* caminho_entrada = get_string(args, 0);
    const char* caminho_saida = get_string(args, 1);
    int espessura = (numArgs >= 3) ? (int)get_int(args, 2) : 2;
    if (espessura < 1) espessura = 1;

    int w, h, canais;
    unsigned char* img = stbi_load(caminho_entrada, &w, &h, &canais, 3);
    if (!img) return jp_bool(0);

    // Cores para diferentes classes (R, G, B)
    static const unsigned char cores[][3] = {
        {255, 0, 0},     {0, 255, 0},     {0, 0, 255},
        {255, 255, 0},   {255, 0, 255},   {0, 255, 255},
        {255, 128, 0},   {128, 0, 255},   {0, 255, 128},
        {255, 64, 64},   {64, 255, 64},   {64, 64, 255},
        {255, 192, 0},   {192, 0, 255},   {0, 192, 255},
        {128, 255, 0},   {255, 0, 128},   {0, 128, 255},
        {200, 200, 0},   {200, 0, 200}
    };
    int num_cores = 20;

    for (size_t d = 0; d < g_resultados.size(); d++) {
        const Deteccao& det = g_resultados[d];

        int x1 = std::max(0, (int)det.x);
        int y1 = std::max(0, (int)det.y);
        int x2 = std::min(w - 1, (int)(det.x + det.w));
        int y2 = std::min(h - 1, (int)(det.y + det.h));

        const unsigned char* cor = cores[det.classe_id % num_cores];

        // Desenhar retangulo com espessura
        for (int t = 0; t < espessura; t++) {
            int bx1 = std::max(0, x1 - t);
            int by1 = std::max(0, y1 - t);
            int bx2 = std::min(w - 1, x2 + t);
            int by2 = std::min(h - 1, y2 + t);

            // Linhas horizontais (topo e base)
            for (int x = bx1; x <= bx2; x++) {
                int idx_top = (by1 * w + x) * 3;
                int idx_bot = (by2 * w + x) * 3;
                img[idx_top] = cor[0]; img[idx_top+1] = cor[1]; img[idx_top+2] = cor[2];
                img[idx_bot] = cor[0]; img[idx_bot+1] = cor[1]; img[idx_bot+2] = cor[2];
            }
            // Linhas verticais (esquerda e direita)
            for (int y = by1; y <= by2; y++) {
                int idx_esq = (y * w + bx1) * 3;
                int idx_dir = (y * w + bx2) * 3;
                img[idx_esq] = cor[0]; img[idx_esq+1] = cor[1]; img[idx_esq+2] = cor[2];
                img[idx_dir] = cor[0]; img[idx_dir+1] = cor[1]; img[idx_dir+2] = cor[2];
            }
        }
    }

    // Salvar imagem
    int ok = 0;
    std::string saida(caminho_saida);
    if (saida.size() >= 4 && saida.substr(saida.size() - 4) == ".png") {
        ok = stbi_write_png(caminho_saida, w, h, 3, img, w * 3);
    } else if (saida.size() >= 4 && saida.substr(saida.size() - 4) == ".bmp") {
        ok = stbi_write_bmp(caminho_saida, w, h, 3, img);
    } else {
        ok = stbi_write_jpg(caminho_saida, w, h, 3, img, 90);
    }

    stbi_image_free(img);
    return jp_bool(ok ? 1 : 0);
}

// onnx_existe(caminho_arquivo)
// Verifica se um arquivo existe no disco, retorna 1 (sim) ou 0 (nao)
JP_EXPORT JPValor onnx_existe(JPValor* args, int numArgs) {
    if (numArgs < 1) return jp_bool(0);
    const char* caminho = get_string(args, 0);
    FILE* f = fopen(caminho, "rb");
    if (f) {
        fclose(f);
        return jp_bool(1);
    }
    return jp_bool(0);
}

// onnx_liberar(id_sessao)
// Libera um modelo da memoria
JP_EXPORT JPValor onnx_liberar(JPValor* args, int numArgs) {
    if (numArgs < 1) return jp_bool(0);

    int id = (int)get_int(args, 0);
    if (id < 0 || id >= MAX_SESSOES || !g_sessoes[id].ativo) return jp_bool(0);

    Sessao& s = g_sessoes[id];

    if (s.session) {
        g_ort->ReleaseSession(s.session);
        s.session = nullptr;
    }
    if (s.session_options) {
        g_ort->ReleaseSessionOptions(s.session_options);
        s.session_options = nullptr;
    }
    s.ativo = false;

    return jp_bool(1);
}

// onnx_liberar_tudo()
// Libera todas as sessoes e o ambiente ONNX
JP_EXPORT JPValor onnx_liberar_tudo(JPValor* args, int numArgs) {
    for (int i = 0; i < MAX_SESSOES; i++) {
        if (g_sessoes[i].ativo) {
            if (g_sessoes[i].session) g_ort->ReleaseSession(g_sessoes[i].session);
            if (g_sessoes[i].session_options) g_ort->ReleaseSessionOptions(g_sessoes[i].session_options);
            g_sessoes[i].session = nullptr;
            g_sessoes[i].session_options = nullptr;
            g_sessoes[i].ativo = false;
        }
    }

    if (g_env) {
        g_ort->ReleaseEnv(g_env);
        g_env = nullptr;
    }

    g_inicializado = false;
    g_resultados.clear();

    return jp_nulo();
}

// ============================================================
// FUNCAO INTERNA: detectar a partir de cv::Mat
// ============================================================

static int detectar_de_mat(int id_sessao, const cv::Mat& frame) {
    if (!g_inicializado) return -1;
    if (id_sessao < 0 || id_sessao >= MAX_SESSOES || !g_sessoes[id_sessao].ativo) return -1;

    Sessao& s = g_sessoes[id_sessao];
    g_resultados.clear();

    int img_w = frame.cols;
    int img_h = frame.rows;
    int tw = (int)s.input_w;
    int th = (int)s.input_h;

    // Redimensionar e converter para tensor NCHW float [1,3,H,W]
    cv::Mat resized;
    cv::resize(frame, resized, cv::Size(tw, th));
    cv::Mat rgb;
    if (resized.channels() == 4)
        cv::cvtColor(resized, rgb, cv::COLOR_BGRA2RGB);
    else if (resized.channels() == 3)
        cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);
    else
        rgb = resized;

    size_t tensor_size = 1 * 3 * th * tw;
    float* input_tensor = (float*)malloc(tensor_size * sizeof(float));
    if (!input_tensor) return -1;

    for (int c = 0; c < 3; c++) {
        for (int y = 0; y < th; y++) {
            for (int x = 0; x < tw; x++) {
                input_tensor[c * th * tw + y * tw + x] = rgb.at<cv::Vec3b>(y, x)[c] / 255.0f;
            }
        }
    }

    // Criar tensor de input
    OrtMemoryInfo* mem_info = nullptr;
    OrtStatus* status = g_ort->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &mem_info);
    if (status) {
        g_ort->ReleaseStatus(status);
        free(input_tensor);
        return -1;
    }

    int64_t input_shape[] = {1, 3, s.input_h, s.input_w};
    size_t input_size = tensor_size * sizeof(float);

    OrtValue* input_ort = nullptr;
    status = g_ort->CreateTensorWithDataAsOrtValue(
        mem_info, input_tensor, input_size,
        input_shape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
        &input_ort
    );
    g_ort->ReleaseMemoryInfo(mem_info);

    if (status) {
        g_ort->ReleaseStatus(status);
        free(input_tensor);
        return -1;
    }

    OrtAllocator* allocator = nullptr;
    g_ort->GetAllocatorWithDefaultOptions(&allocator);

    char* input_name = nullptr;
    g_ort->SessionGetInputName(s.session, 0, allocator, &input_name);
    char* output_name = nullptr;
    g_ort->SessionGetOutputName(s.session, 0, allocator, &output_name);

    const char* input_names[] = {input_name};
    const char* output_names[] = {output_name};

    OrtValue* output_ort = nullptr;
    status = g_ort->Run(s.session, nullptr, input_names, (const OrtValue* const*)&input_ort, 1, output_names, 1, &output_ort);

    if (input_name) g_ort->AllocatorFree(allocator, input_name);
    if (output_name) g_ort->AllocatorFree(allocator, output_name);
    g_ort->ReleaseValue(input_ort);
    free(input_tensor);

    if (status) {
        g_ort->ReleaseStatus(status);
        return -1;
    }

    float* output_data = nullptr;
    g_ort->GetTensorMutableData(output_ort, (void**)&output_data);

    if (output_data) {
        posprocessar_yolov8(output_data, (int)s.num_classes, (int)s.num_deteccoes,
                            img_w, img_h, (int)s.input_w, (int)s.input_h);
    }

    g_ort->ReleaseValue(output_ort);
    return (int)g_resultados.size();
}

// ============================================================
// CAPTURA: camera, janela, tela (em arquivo separado)
// ============================================================
#include "captura.hpp"