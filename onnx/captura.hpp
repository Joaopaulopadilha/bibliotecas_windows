// captura.hpp
// Funcoes de camera, janela e captura de tela para biblioteca ONNX JPLang

#ifndef ONNX_CAPTURA_HPP
#define ONNX_CAPTURA_HPP

#include <opencv2/opencv.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/videoio.hpp>
#include <cstdio>
#include <csignal>

#if !defined(_WIN32) && !defined(_WIN64)
#include <unistd.h>
#endif

// ============================================================
// ESTADO GLOBAL: CAMERA E EXIBICAO
// ============================================================

static const int MAX_CAMERAS = 8;
static cv::VideoCapture* g_cameras[MAX_CAMERAS] = {nullptr};
static cv::Mat g_ultimo_frame;
static int g_ultima_tecla = -1;

// ============================================================
// FUNCAO INTERNA: desenhar bounding boxes em cv::Mat
// ============================================================

static void desenhar_em_mat(cv::Mat& img) {
    static const cv::Scalar cores[] = {
        {0,0,255},     {0,255,0},     {255,0,0},
        {0,255,255},   {255,0,255},   {255,255,0},
        {0,128,255},   {255,0,128},   {128,255,0},
        {64,64,255},   {64,255,64},   {255,64,64},
        {0,192,255},   {255,0,192},   {192,255,0},
        {0,255,128},   {128,0,255},   {255,128,0},
        {0,200,200},   {200,0,200}
    };
    int num_cores = 20;

    for (size_t d = 0; d < g_resultados.size(); d++) {
        const Deteccao& det = g_resultados[d];
        cv::Rect box((int)det.x, (int)det.y, (int)det.w, (int)det.h);
        cv::Scalar cor = cores[det.classe_id % num_cores];
        cv::rectangle(img, box, cor, 2);

        char label[64];
        snprintf(label, sizeof(label), "%d: %.0f%%", det.classe_id, det.confianca * 100);
        int baseline = 0;
        cv::Size textSize = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseline);
        cv::Point textOrg((int)det.x, (int)det.y - 5);
        if (textOrg.y < textSize.height) textOrg.y = (int)det.y + (int)det.h + textSize.height + 5;
        cv::rectangle(img, cv::Point(textOrg.x, textOrg.y - textSize.height - 2),
                      cv::Point(textOrg.x + textSize.width, textOrg.y + baseline + 2), cor, cv::FILLED);
        cv::putText(img, label, textOrg, cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255,255,255), 1);
    }
}

// ============================================================
// CLEANUP
// ============================================================

static void cleanup_global() {
    for (int i = 0; i < MAX_CAMERAS; i++) {
        if (g_cameras[i]) {
            g_cameras[i]->release();
            delete g_cameras[i];
            g_cameras[i] = nullptr;
        }
    }
    cv::destroyAllWindows();
}

#if defined(_WIN32) || defined(_WIN64)
static BOOL WINAPI console_handler(DWORD signal) {
    cleanup_global();
    return FALSE;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
    if (fdwReason == DLL_PROCESS_DETACH) {
        cleanup_global();
    } else if (fdwReason == DLL_PROCESS_ATTACH) {
        SetConsoleCtrlHandler(console_handler, TRUE);
    }
    return TRUE;
}
#else
static void signal_handler(int sig) {
    cleanup_global();
    _exit(1);
}

__attribute__((constructor)) static void init_signal() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
}

__attribute__((destructor)) static void fini_cleanup() {
    cleanup_global();
}
#endif

// ============================================================
// FUNCOES EXPORTADAS: CAMERA
// ============================================================

JP_EXPORT JPValor onnx_camera_abrir(JPValor* args, int numArgs) {
    int idx = (numArgs >= 1) ? (int)get_int(args, 0) : 0;

    for (int i = 0; i < MAX_CAMERAS; i++) {
        if (g_cameras[i] == nullptr) {
            g_cameras[i] = new cv::VideoCapture(idx);
            if (!g_cameras[i]->isOpened()) {
                delete g_cameras[i];
                g_cameras[i] = nullptr;
                return jp_int(-1);
            }
            return jp_int(i);
        }
    }
    return jp_int(-1);
}

JP_EXPORT JPValor onnx_camera_capturar(JPValor* args, int numArgs) {
    if (numArgs < 2) return jp_bool(0);
    int id = (int)get_int(args, 0);
    const char* caminho = get_string(args, 1);

    if (id < 0 || id >= MAX_CAMERAS || !g_cameras[id]) return jp_bool(0);

    cv::Mat frame;
    if (!g_cameras[id]->read(frame) || frame.empty()) return jp_bool(0);

    g_ultimo_frame = frame.clone();
    return jp_bool(cv::imwrite(caminho, frame) ? 1 : 0);
}

JP_EXPORT JPValor onnx_camera_frame(JPValor* args, int numArgs) {
    if (numArgs < 1) return jp_bool(0);
    int id = (int)get_int(args, 0);

    if (id < 0 || id >= MAX_CAMERAS || !g_cameras[id]) return jp_bool(0);

    cv::Mat frame;
    if (!g_cameras[id]->read(frame) || frame.empty()) return jp_bool(0);

    g_ultimo_frame = frame.clone();
    return jp_bool(1);
}

JP_EXPORT JPValor onnx_camera_detectar(JPValor* args, int numArgs) {
    if (numArgs < 2) return jp_int(-1);
    int cam_id = (int)get_int(args, 0);
    int modelo_id = (int)get_int(args, 1);

    if (cam_id < 0 || cam_id >= MAX_CAMERAS || !g_cameras[cam_id]) return jp_int(-1);

    cv::Mat frame;
    if (!g_cameras[cam_id]->read(frame) || frame.empty()) return jp_int(-1);

    g_ultimo_frame = frame.clone();
    return jp_int(detectar_de_mat(modelo_id, frame));
}

JP_EXPORT JPValor onnx_camera_fechar(JPValor* args, int numArgs) {
    if (numArgs < 1) return jp_bool(0);
    int id = (int)get_int(args, 0);

    if (id < 0 || id >= MAX_CAMERAS || !g_cameras[id]) return jp_bool(0);

    g_cameras[id]->release();
    delete g_cameras[id];
    g_cameras[id] = nullptr;
    return jp_bool(1);
}

// ============================================================
// FUNCOES EXPORTADAS: JANELA E EXIBICAO
// ============================================================

// Flag para saber se a janela ja foi exibida
static bool g_janela_ativa = false;
static std::string g_titulo_janela;

JP_EXPORT JPValor onnx_exibir(JPValor* args, int numArgs) {
    if (numArgs < 1) return jp_bool(0);
    const char* titulo = get_string(args, 0);

    if (g_ultimo_frame.empty()) return jp_bool(0);

    cv::Mat exibicao = g_ultimo_frame.clone();
    if (!g_resultados.empty()) {
        desenhar_em_mat(exibicao);
    }

    // Detecta se a janela foi fechada pelo X (so apos ja ter sido exibida)
    if (g_janela_ativa) {
        try {
            double v = cv::getWindowProperty(titulo, cv::WND_PROP_AUTOSIZE);
            if (v < 0) {
                cleanup_global();
                _exit(0);
            }
        } catch (...) {
            cleanup_global();
            _exit(0);
        }
    }

    cv::namedWindow(titulo, cv::WINDOW_AUTOSIZE);
    cv::imshow(titulo, exibicao);
    g_ultima_tecla = cv::waitKey(1) & 0xFF;
    g_janela_ativa = true;
    g_titulo_janela = titulo;

    return jp_bool(1);
}

JP_EXPORT JPValor onnx_esperar(JPValor* args, int numArgs) {
    int ms = (numArgs >= 1) ? (int)get_int(args, 0) : 0;
    return jp_int(cv::waitKey(ms) & 0xFF);
}

JP_EXPORT JPValor onnx_tecla(JPValor* args, int numArgs) {
    int codigo = (numArgs >= 1) ? (int)get_int(args, 0) : -1;
    int tecla = g_ultima_tecla;
    g_ultima_tecla = -1;

    if (codigo < 0) return jp_int(tecla);
    return jp_bool((tecla == codigo) ? 1 : 0);
}

JP_EXPORT JPValor onnx_fechar_janelas(JPValor* args, int numArgs) {
    cv::destroyAllWindows();
    return jp_nulo();
}

// ============================================================
// FUNCOES EXPORTADAS: CAPTURA DE TELA
// ============================================================

#if defined(_WIN32) || defined(_WIN64)

JP_EXPORT JPValor onnx_tela_capturar(JPValor* args, int numArgs) {
    if (numArgs < 1) return jp_bool(0);
    const char* caminho = get_string(args, 0);

    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);

    HDC hScreen = GetDC(NULL);
    HDC hDC = CreateCompatibleDC(hScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, w, h);
    SelectObject(hDC, hBitmap);
    BitBlt(hDC, 0, 0, w, h, hScreen, 0, 0, SRCCOPY);

    cv::Mat img(h, w, CV_8UC4);
    BITMAPINFOHEADER bi = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
    GetDIBits(hDC, hBitmap, 0, h, img.data, (BITMAPINFO*)&bi, DIB_RGB_COLORS);

    DeleteObject(hBitmap);
    DeleteDC(hDC);
    ReleaseDC(NULL, hScreen);

    cv::Mat bgr;
    cv::cvtColor(img, bgr, cv::COLOR_BGRA2BGR);
    g_ultimo_frame = bgr.clone();

    return jp_bool(cv::imwrite(caminho, bgr) ? 1 : 0);
}

JP_EXPORT JPValor onnx_tela_regiao(JPValor* args, int numArgs) {
    if (numArgs < 5) return jp_bool(0);
    int rx = (int)get_int(args, 0);
    int ry = (int)get_int(args, 1);
    int rw = (int)get_int(args, 2);
    int rh = (int)get_int(args, 3);
    const char* caminho = get_string(args, 4);

    HDC hScreen = GetDC(NULL);
    HDC hDC = CreateCompatibleDC(hScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, rw, rh);
    SelectObject(hDC, hBitmap);
    BitBlt(hDC, 0, 0, rw, rh, hScreen, rx, ry, SRCCOPY);

    cv::Mat img(rh, rw, CV_8UC4);
    BITMAPINFOHEADER bi = {sizeof(BITMAPINFOHEADER), rw, -rh, 1, 32, BI_RGB};
    GetDIBits(hDC, hBitmap, 0, rh, img.data, (BITMAPINFO*)&bi, DIB_RGB_COLORS);

    DeleteObject(hBitmap);
    DeleteDC(hDC);
    ReleaseDC(NULL, hScreen);

    cv::Mat bgr;
    cv::cvtColor(img, bgr, cv::COLOR_BGRA2BGR);
    g_ultimo_frame = bgr.clone();

    return jp_bool(cv::imwrite(caminho, bgr) ? 1 : 0);
}

JP_EXPORT JPValor onnx_tela_detectar(JPValor* args, int numArgs) {
    if (numArgs < 1) return jp_int(-1);
    int modelo_id = (int)get_int(args, 0);

    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);

    HDC hScreen = GetDC(NULL);
    HDC hDC = CreateCompatibleDC(hScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, w, h);
    SelectObject(hDC, hBitmap);
    BitBlt(hDC, 0, 0, w, h, hScreen, 0, 0, SRCCOPY);

    cv::Mat img(h, w, CV_8UC4);
    BITMAPINFOHEADER bi = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
    GetDIBits(hDC, hBitmap, 0, h, img.data, (BITMAPINFO*)&bi, DIB_RGB_COLORS);

    DeleteObject(hBitmap);
    DeleteDC(hDC);
    ReleaseDC(NULL, hScreen);

    cv::Mat bgr;
    cv::cvtColor(img, bgr, cv::COLOR_BGRA2BGR);
    g_ultimo_frame = bgr.clone();

    return jp_int(detectar_de_mat(modelo_id, bgr));
}

JP_EXPORT JPValor onnx_tela_regiao_detectar(JPValor* args, int numArgs) {
    if (numArgs < 5) return jp_int(-1);
    int modelo_id = (int)get_int(args, 0);
    int rx = (int)get_int(args, 1);
    int ry = (int)get_int(args, 2);
    int rw = (int)get_int(args, 3);
    int rh = (int)get_int(args, 4);

    HDC hScreen = GetDC(NULL);
    HDC hDC = CreateCompatibleDC(hScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, rw, rh);
    SelectObject(hDC, hBitmap);
    BitBlt(hDC, 0, 0, rw, rh, hScreen, rx, ry, SRCCOPY);

    cv::Mat img(rh, rw, CV_8UC4);
    BITMAPINFOHEADER bi = {sizeof(BITMAPINFOHEADER), rw, -rh, 1, 32, BI_RGB};
    GetDIBits(hDC, hBitmap, 0, rh, img.data, (BITMAPINFO*)&bi, DIB_RGB_COLORS);

    DeleteObject(hBitmap);
    DeleteDC(hDC);
    ReleaseDC(NULL, hScreen);

    cv::Mat bgr;
    cv::cvtColor(img, bgr, cv::COLOR_BGRA2BGR);
    g_ultimo_frame = bgr.clone();

    return jp_int(detectar_de_mat(modelo_id, bgr));
}

#endif

// ============================================================
// FUNCOES EXPORTADAS: UTILITARIOS DE FRAME
// ============================================================

JP_EXPORT JPValor onnx_detectar_frame(JPValor* args, int numArgs) {
    if (numArgs < 1) return jp_int(-1);
    int modelo_id = (int)get_int(args, 0);

    if (g_ultimo_frame.empty()) return jp_int(-1);
    return jp_int(detectar_de_mat(modelo_id, g_ultimo_frame));
}

JP_EXPORT JPValor onnx_salvar_frame(JPValor* args, int numArgs) {
    if (numArgs < 1) return jp_bool(0);
    const char* caminho = get_string(args, 0);

    if (g_ultimo_frame.empty()) return jp_bool(0);

    cv::Mat saida = g_ultimo_frame.clone();
    if (!g_resultados.empty()) {
        desenhar_em_mat(saida);
    }

    return jp_bool(cv::imwrite(caminho, saida) ? 1 : 0);
}

#endif // ONNX_CAPTURA_HPP