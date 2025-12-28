// captura.hpp
// Módulo de captura de tela para biblioteca CVN (Win32)

#ifndef CVN_CAPTURA_HPP
#define CVN_CAPTURA_HPP

#include <windows.h>
#include <string>

// Define PW_RENDERFULLCONTENT se não existir (Windows 8.1+)
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

namespace cvn {

class Captura {
public:
    // Captura tela inteira
    // Retorna dados RGBA, largura e altura por referência
    static unsigned char* tela(int& largura, int& altura) {
        // Obtém dimensões da tela
        largura = GetSystemMetrics(SM_CXSCREEN);
        altura = GetSystemMetrics(SM_CYSCREEN);
        
        return capturar_regiao_interna(0, 0, largura, altura);
    }
    
    // Captura região específica
    static unsigned char* regiao(int x, int y, int largura, int altura) {
        if (largura <= 0 || altura <= 0) return nullptr;
        return capturar_regiao_interna(x, y, largura, altura);
    }
    
    // Captura janela pelo título
    static unsigned char* janela(const std::string& titulo, int& largura, int& altura) {
        // Converte título para wide string
        int len = MultiByteToWideChar(CP_UTF8, 0, titulo.c_str(), -1, NULL, 0);
        std::wstring wtitulo(len, 0);
        MultiByteToWideChar(CP_UTF8, 0, titulo.c_str(), -1, &wtitulo[0], len);
        
        HWND hwnd = FindWindowW(NULL, wtitulo.c_str());
        if (!hwnd) {
            largura = 0;
            altura = 0;
            return nullptr;
        }
        
        // Obtém retângulo da janela
        RECT rect;
        GetWindowRect(hwnd, &rect);
        
        largura = rect.right - rect.left;
        altura = rect.bottom - rect.top;
        
        if (largura <= 0 || altura <= 0) return nullptr;
        
        return capturar_janela_interna(hwnd, largura, altura);
    }

private:
    // Captura região da tela
    static unsigned char* capturar_regiao_interna(int x, int y, int largura, int altura) {
        HDC hdcScreen = GetDC(NULL);
        HDC hdcMem = CreateCompatibleDC(hdcScreen);
        
        HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, largura, altura);
        HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);
        
        // Captura a região
        BitBlt(hdcMem, 0, 0, largura, altura, hdcScreen, x, y, SRCCOPY);
        
        // Extrai pixels
        unsigned char* dados = extrair_pixels(hdcMem, hBitmap, largura, altura);
        
        // Limpa
        SelectObject(hdcMem, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        
        return dados;
    }
    
    // Captura janela específica
    static unsigned char* capturar_janela_interna(HWND hwnd, int largura, int altura) {
        HDC hdcWindow = GetWindowDC(hwnd);
        HDC hdcMem = CreateCompatibleDC(hdcWindow);
        
        HBITMAP hBitmap = CreateCompatibleBitmap(hdcWindow, largura, altura);
        HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);
        
        // Usa BitBlt para capturar
        BitBlt(hdcMem, 0, 0, largura, altura, hdcWindow, 0, 0, SRCCOPY);
        
        // Extrai pixels
        unsigned char* dados = extrair_pixels(hdcMem, hBitmap, largura, altura);
        
        // Limpa
        SelectObject(hdcMem, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hdcMem);
        ReleaseDC(hwnd, hdcWindow);
        
        return dados;
    }
    
    // Extrai pixels do bitmap e converte para RGBA
    static unsigned char* extrair_pixels(HDC hdcMem, HBITMAP hBitmap, int largura, int altura) {
        BITMAPINFOHEADER bi = {};
        bi.biSize = sizeof(BITMAPINFOHEADER);
        bi.biWidth = largura;
        bi.biHeight = -altura; // Top-down
        bi.biPlanes = 1;
        bi.biBitCount = 32;
        bi.biCompression = BI_RGB;
        
        int tamanho = largura * altura * 4;
        unsigned char* temp = (unsigned char*)malloc(tamanho);
        unsigned char* dados = (unsigned char*)malloc(tamanho);
        
        if (!temp || !dados) {
            if (temp) free(temp);
            if (dados) free(dados);
            return nullptr;
        }
        
        // Obtém pixels (formato BGRA)
        GetDIBits(hdcMem, hBitmap, 0, altura, temp, (BITMAPINFO*)&bi, DIB_RGB_COLORS);
        
        // Converte BGRA para RGBA
        for (int i = 0; i < largura * altura; i++) {
            dados[i * 4 + 0] = temp[i * 4 + 2]; // R
            dados[i * 4 + 1] = temp[i * 4 + 1]; // G
            dados[i * 4 + 2] = temp[i * 4 + 0]; // B
            dados[i * 4 + 3] = 255;              // A
        }
        
        free(temp);
        return dados;
    }
};

} // namespace cvn

#endif // CVN_CAPTURA_HPP