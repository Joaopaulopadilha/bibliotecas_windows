// janela.hpp
// Módulo de gerenciamento de janelas Win32 para biblioteca CVN (com double buffer)

#ifndef CVN_JANELA_HPP
#define CVN_JANELA_HPP

#include <windows.h>
#include <string>
#include <unordered_map>

namespace cvn {

// Estrutura para janela com double buffer
struct JanelaInfo {
    HWND hwnd;
    HDC hdcMem;
    HBITMAP hbitmapMem;
    HBITMAP hbitmapOld;
    int largura;
    int altura;
    bool executando;
};

// Gerenciador de janelas
class GerenciadorJanelas {
private:
    WNDCLASSEXW wc = {};
    bool classe_registrada = false;
    std::unordered_map<std::string, JanelaInfo> janelas;
    std::unordered_map<HWND, std::string> hwnd_para_titulo;

    static GerenciadorJanelas* inst_ptr;

    static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        auto& inst = instancia();
        
        switch (msg) {
        case WM_PAINT:
            {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hWnd, &ps);
                
                auto it = inst.hwnd_para_titulo.find(hWnd);
                if (it != inst.hwnd_para_titulo.end()) {
                    auto& info = inst.janelas[it->second];
                    if (info.hdcMem) {
                        BitBlt(hdc, 0, 0, info.largura, info.altura, info.hdcMem, 0, 0, SRCCOPY);
                    } else {
                        RECT rect;
                        GetClientRect(hWnd, &rect);
                        FillRect(hdc, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
                    }
                }
                
                EndPaint(hWnd, &ps);
            }
            return 0;
            
        case WM_DESTROY:
            {
                auto it = inst.hwnd_para_titulo.find(hWnd);
                if (it != inst.hwnd_para_titulo.end()) {
                    auto& info = inst.janelas[it->second];
                    info.executando = false;
                    if (info.hdcMem) {
                        SelectObject(info.hdcMem, info.hbitmapOld);
                        DeleteObject(info.hbitmapMem);
                        DeleteDC(info.hdcMem);
                    }
                    inst.janelas.erase(it->second);
                    inst.hwnd_para_titulo.erase(it);
                }
            }
            return 0;
            
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                auto it = inst.hwnd_para_titulo.find(hWnd);
                if (it != inst.hwnd_para_titulo.end()) {
                    inst.janelas[it->second].executando = false;
                }
            }
            return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    bool registrar_classe() {
        if (classe_registrada) return true;
        
        wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = L"CVN_JPLang";
        
        if (RegisterClassExW(&wc)) {
            classe_registrada = true;
            return true;
        }
        return false;
    }

public:
    static GerenciadorJanelas& instancia() {
        static GerenciadorJanelas inst;
        return inst;
    }

    // Cria ou obtém janela
    JanelaInfo* obter_ou_criar(const std::string& titulo, int largura, int altura) {
        auto it = janelas.find(titulo);
        if (it != janelas.end()) {
            // Redimensiona se necessário
            if (it->second.largura != largura || it->second.altura != altura) {
                redimensionar_buffer(&it->second, largura, altura);
            }
            return &it->second;
        }
        
        if (!registrar_classe()) return nullptr;
        
        // Converte título para wide string
        int len = MultiByteToWideChar(CP_UTF8, 0, titulo.c_str(), -1, NULL, 0);
        std::wstring wtitulo(len, 0);
        MultiByteToWideChar(CP_UTF8, 0, titulo.c_str(), -1, &wtitulo[0], len);
        
        // Ajusta tamanho para incluir bordas
        RECT rect = {0, 0, largura, altura};
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
        int janela_largura = rect.right - rect.left;
        int janela_altura = rect.bottom - rect.top;
        
        // Centraliza na tela, mas garante que não fique negativo
        int tela_largura = GetSystemMetrics(SM_CXSCREEN);
        int tela_altura = GetSystemMetrics(SM_CYSCREEN);
        int pos_x = (tela_largura - janela_largura) / 2;
        int pos_y = (tela_altura - janela_altura) / 2;
        
        // Garante posição mínima de 0
        if (pos_x < 0) pos_x = 0;
        if (pos_y < 0) pos_y = 0;
        
        // Se janela maior que tela, limita tamanho
        if (janela_largura > tela_largura) janela_largura = tela_largura;
        if (janela_altura > tela_altura) janela_altura = tela_altura;
        
        HWND hwnd = CreateWindowW(
            wc.lpszClassName,
            wtitulo.c_str(),
            WS_OVERLAPPEDWINDOW,
            pos_x, pos_y,
            janela_largura, janela_altura,
            nullptr, nullptr,
            wc.hInstance,
            nullptr
        );
        
        if (!hwnd) return nullptr;
        
        JanelaInfo info = {};
        info.hwnd = hwnd;
        info.largura = largura;
        info.altura = altura;
        info.executando = true;
        
        // Cria double buffer
        HDC hdcWnd = GetDC(hwnd);
        info.hdcMem = CreateCompatibleDC(hdcWnd);
        info.hbitmapMem = CreateCompatibleBitmap(hdcWnd, largura, altura);
        info.hbitmapOld = (HBITMAP)SelectObject(info.hdcMem, info.hbitmapMem);
        ReleaseDC(hwnd, hdcWnd);
        
        janelas[titulo] = info;
        hwnd_para_titulo[hwnd] = titulo;
        
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        
        return &janelas[titulo];
    }
    
    // Redimensiona buffer
    void redimensionar_buffer(JanelaInfo* info, int largura, int altura) {
        if (!info || !info->hwnd) return;
        
        // Limpa buffer antigo
        if (info->hdcMem) {
            SelectObject(info->hdcMem, info->hbitmapOld);
            DeleteObject(info->hbitmapMem);
            DeleteDC(info->hdcMem);
        }
        
        // Cria novo buffer
        HDC hdcWnd = GetDC(info->hwnd);
        info->hdcMem = CreateCompatibleDC(hdcWnd);
        info->hbitmapMem = CreateCompatibleBitmap(hdcWnd, largura, altura);
        info->hbitmapOld = (HBITMAP)SelectObject(info->hdcMem, info->hbitmapMem);
        ReleaseDC(info->hwnd, hdcWnd);
        
        info->largura = largura;
        info->altura = altura;
        
        // Redimensiona janela
        RECT rect = {0, 0, largura, altura};
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
        SetWindowPos(info->hwnd, NULL, 0, 0, rect.right - rect.left, rect.bottom - rect.top, 
                     SWP_NOMOVE | SWP_NOZORDER);
    }
    
    // Atualiza imagem na janela (double buffer, sem flicker)
    bool atualizar(const std::string& titulo, unsigned char* dados, int largura, int altura) {
        JanelaInfo* info = obter_ou_criar(titulo, largura, altura);
        if (!info) return false;
        
        // Copia dados para o buffer
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = largura;
        bmi.bmiHeader.biHeight = -altura; // Top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        
        // Converte RGBA para BGRA
        unsigned char* bgra = (unsigned char*)malloc(largura * altura * 4);
        if (!bgra) return false;
        
        for (int i = 0; i < largura * altura; i++) {
            bgra[i * 4 + 0] = dados[i * 4 + 2]; // B
            bgra[i * 4 + 1] = dados[i * 4 + 1]; // G
            bgra[i * 4 + 2] = dados[i * 4 + 0]; // R
            bgra[i * 4 + 3] = dados[i * 4 + 3]; // A
        }
        
        SetDIBitsToDevice(info->hdcMem, 0, 0, largura, altura, 0, 0, 0, altura, bgra, &bmi, DIB_RGB_COLORS);
        free(bgra);
        
        // Força repintura
        InvalidateRect(info->hwnd, NULL, FALSE);
        UpdateWindow(info->hwnd);
        
        return true;
    }
    
    // Exibe imagem (compatibilidade)
    bool exibir(const std::string& titulo, unsigned char* dados, int largura, int altura, int img_id) {
        return atualizar(titulo, dados, largura, altura);
    }
    
    // Processa mensagens e verifica se janela está ativa
    bool processar_mensagens(const std::string& titulo) {
        auto it = janelas.find(titulo);
        if (it == janelas.end()) return false;
        
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                it->second.executando = false;
                return false;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        
        return it->second.executando;
    }

    // Espera por tecla ou timeout
    int esperar(int ms) {
        MSG msg;
        DWORD inicio = GetTickCount();
        
        while (true) {
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    return -1;
                }
                if (msg.message == WM_KEYDOWN) {
                    return (int)msg.wParam;
                }
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            
            if (ms > 0) {
                DWORD elapsed = GetTickCount() - inicio;
                if (elapsed >= (DWORD)ms) {
                    return -1;
                }
            }
            
            if (janelas.empty()) {
                return -1;
            }
            
            Sleep(1);
        }
        
        return -1;
    }

    // Fecha janela específica
    bool fechar(const std::string& titulo) {
        auto it = janelas.find(titulo);
        if (it != janelas.end()) {
            DestroyWindow(it->second.hwnd);
            return true;
        }
        return false;
    }

    // Fecha todas as janelas
    void fechar_todas() {
        for (auto& par : janelas) {
            if (par.second.hdcMem) {
                SelectObject(par.second.hdcMem, par.second.hbitmapOld);
                DeleteObject(par.second.hbitmapMem);
                DeleteDC(par.second.hdcMem);
            }
            DestroyWindow(par.second.hwnd);
        }
        janelas.clear();
        hwnd_para_titulo.clear();
    }

    // Verifica se há janelas abertas
    bool tem_janelas() const {
        return !janelas.empty();
    }

    ~GerenciadorJanelas() {
        fechar_todas();
    }
};

} // namespace cvn

#endif // CVN_JANELA_HPP