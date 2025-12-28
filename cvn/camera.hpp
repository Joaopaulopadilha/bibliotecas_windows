// camera.hpp
// Módulo de captura de câmera para biblioteca CVN (DirectShow)

#ifndef CVN_CAMERA_HPP
#define CVN_CAMERA_HPP

#include <windows.h>
#include <dshow.h>
#include <string>
#include <vector>

#pragma comment(lib, "strmiids.lib")

// === DEFINIÇÕES DO SAMPLE GRABBER (não incluídas no SDK moderno) ===

// GUIDs
static const GUID CLSID_SampleGrabber = {0xC1F400A0, 0x3F08, 0x11d3, {0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37}};
static const GUID CLSID_NullRenderer = {0xC1F400A4, 0x3F08, 0x11d3, {0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37}};
static const GUID IID_ISampleGrabber = {0x6B652FFF, 0x11FE, 0x4fce, {0x92, 0xAD, 0x02, 0x66, 0xB5, 0xD7, 0xC7, 0x8F}};
static const GUID IID_ISampleGrabberCB = {0x0579154A, 0x2B53, 0x4994, {0xB0, 0xD0, 0xE7, 0x73, 0x14, 0x8E, 0xFF, 0x85}};

// Interface ISampleGrabberCB
MIDL_INTERFACE("0579154A-2B53-4994-B0D0-E773148EFF85")
ISampleGrabberCB : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE SampleCB(double SampleTime, IMediaSample *pSample) = 0;
    virtual HRESULT STDMETHODCALLTYPE BufferCB(double SampleTime, BYTE *pBuffer, long BufferLen) = 0;
};

// Interface ISampleGrabber
MIDL_INTERFACE("6B652FFF-11FE-4fce-92AD-0266B5D7C78F")
ISampleGrabber : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE SetOneShot(BOOL OneShot) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetMediaType(const AM_MEDIA_TYPE *pType) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetConnectedMediaType(AM_MEDIA_TYPE *pType) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetBufferSamples(BOOL BufferThem) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentBuffer(long *pBufferSize, long *pBuffer) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentSample(IMediaSample **ppSample) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCallback(ISampleGrabberCB *pCallback, long WhichMethodToCallback) = 0;
};

// === FIM DAS DEFINIÇÕES ===

namespace cvn {

// Callback para capturar frames
class SampleGrabberCallback : public ISampleGrabberCB {
private:
    LONG m_refCount = 1;
    
public:
    unsigned char* dados = nullptr;
    int largura = 0;
    int altura = 0;
    int tamanho = 0;
    bool novo_frame = false;
    CRITICAL_SECTION cs;

    SampleGrabberCallback() {
        InitializeCriticalSection(&cs);
    }

    ~SampleGrabberCallback() {
        DeleteCriticalSection(&cs);
        if (dados) free(dados);
    }

    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) {
        if (riid == IID_ISampleGrabberCB || riid == IID_IUnknown) {
            *ppv = static_cast<ISampleGrabberCB*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() { 
        return InterlockedIncrement(&m_refCount); 
    }
    
    STDMETHODIMP_(ULONG) Release() { 
        LONG ref = InterlockedDecrement(&m_refCount);
        if (ref == 0) {
            delete this;
        }
        return ref;
    }

    STDMETHODIMP SampleCB(double Time, IMediaSample *pSample) {
        return S_OK;
    }

    STDMETHODIMP BufferCB(double Time, BYTE *pBuffer, long BufferLen) {
        EnterCriticalSection(&cs);
        
        if (!dados || tamanho != BufferLen) {
            if (dados) free(dados);
            dados = (unsigned char*)malloc(BufferLen);
            tamanho = BufferLen;
        }
        
        if (dados) {
            memcpy(dados, pBuffer, BufferLen);
            novo_frame = true;
        }
        
        LeaveCriticalSection(&cs);
        return S_OK;
    }

    unsigned char* obter_frame_rgba(int& out_largura, int& out_altura) {
        EnterCriticalSection(&cs);
        
        if (!dados || !novo_frame || largura <= 0 || altura <= 0) {
            LeaveCriticalSection(&cs);
            return nullptr;
        }

        int pixels = largura * altura;
        unsigned char* rgba = (unsigned char*)malloc(pixels * 4);
        
        if (rgba) {
            // BGR para RGBA (imagem invertida verticalmente)
            for (int y = 0; y < altura; y++) {
                for (int x = 0; x < largura; x++) {
                    int src_y = altura - 1 - y;
                    int src_idx = (src_y * largura + x) * 3;
                    int dst_idx = (y * largura + x) * 4;
                    
                    rgba[dst_idx + 0] = dados[src_idx + 2]; // R
                    rgba[dst_idx + 1] = dados[src_idx + 1]; // G
                    rgba[dst_idx + 2] = dados[src_idx + 0]; // B
                    rgba[dst_idx + 3] = 255;                 // A
                }
            }
            out_largura = largura;
            out_altura = altura;
        }
        
        novo_frame = false;
        LeaveCriticalSection(&cs);
        return rgba;
    }
};

// Estrutura de câmera aberta
struct Camera {
    IGraphBuilder* graph = nullptr;
    ICaptureGraphBuilder2* capture = nullptr;
    IMediaControl* control = nullptr;
    IBaseFilter* device_filter = nullptr;
    IBaseFilter* grabber_filter = nullptr;
    IBaseFilter* null_renderer = nullptr;
    ISampleGrabber* grabber = nullptr;
    SampleGrabberCallback* callback = nullptr;
    bool ativa = false;
    int largura = 640;
    int altura = 480;
};

class GerenciadorCameras {
private:
    std::vector<Camera*> cameras;
    bool com_inicializado = false;

    void inicializar_com() {
        if (!com_inicializado) {
            CoInitializeEx(NULL, COINIT_MULTITHREADED);
            com_inicializado = true;
        }
    }

public:
    static GerenciadorCameras& instancia() {
        static GerenciadorCameras inst;
        return inst;
    }

    // Lista câmeras disponíveis
    std::string listar() {
        inicializar_com();
        
        std::string resultado;
        ICreateDevEnum* dev_enum = nullptr;
        IEnumMoniker* enum_moniker = nullptr;

        HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER,
                                       IID_ICreateDevEnum, (void**)&dev_enum);
        if (FAILED(hr)) return "";

        hr = dev_enum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &enum_moniker, 0);
        if (hr == S_OK && enum_moniker) {
            IMoniker* moniker = nullptr;
            int index = 0;
            
            while (enum_moniker->Next(1, &moniker, NULL) == S_OK) {
                IPropertyBag* prop_bag = nullptr;
                hr = moniker->BindToStorage(0, 0, IID_IPropertyBag, (void**)&prop_bag);
                
                if (SUCCEEDED(hr)) {
                    VARIANT var;
                    VariantInit(&var);
                    
                    hr = prop_bag->Read(L"FriendlyName", &var, 0);
                    if (SUCCEEDED(hr)) {
                        int len = WideCharToMultiByte(CP_UTF8, 0, var.bstrVal, -1, NULL, 0, NULL, NULL);
                        char* nome = (char*)malloc(len);
                        WideCharToMultiByte(CP_UTF8, 0, var.bstrVal, -1, nome, len, NULL, NULL);
                        
                        if (!resultado.empty()) resultado += ",";
                        resultado += std::to_string(index) + ":" + nome;
                        
                        free(nome);
                        VariantClear(&var);
                        index++;
                    }
                    prop_bag->Release();
                }
                moniker->Release();
            }
            enum_moniker->Release();
        }
        
        if (dev_enum) dev_enum->Release();
        return resultado;
    }

    // Abre câmera pelo índice
    int abrir(int indice) {
        inicializar_com();
        
        Camera* cam = new Camera();
        
        // Cria o grafo de filtros
        HRESULT hr = CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER,
                                       IID_IGraphBuilder, (void**)&cam->graph);
        if (FAILED(hr)) { delete cam; return -1; }

        // Cria o capture graph builder
        hr = CoCreateInstance(CLSID_CaptureGraphBuilder2, NULL, CLSCTX_INPROC_SERVER,
                               IID_ICaptureGraphBuilder2, (void**)&cam->capture);
        if (FAILED(hr)) { limpar_camera(cam); return -1; }

        cam->capture->SetFiltergraph(cam->graph);

        // Obtém o dispositivo de vídeo
        ICreateDevEnum* dev_enum = nullptr;
        IEnumMoniker* enum_moniker = nullptr;

        hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER,
                               IID_ICreateDevEnum, (void**)&dev_enum);
        if (FAILED(hr)) { limpar_camera(cam); return -1; }

        hr = dev_enum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &enum_moniker, 0);
        if (hr != S_OK || !enum_moniker) {
            dev_enum->Release();
            limpar_camera(cam);
            return -1;
        }

        IMoniker* moniker = nullptr;
        int current = 0;
        bool encontrado = false;

        while (enum_moniker->Next(1, &moniker, NULL) == S_OK) {
            if (current == indice) {
                hr = moniker->BindToObject(0, 0, IID_IBaseFilter, (void**)&cam->device_filter);
                encontrado = SUCCEEDED(hr);
                moniker->Release();
                break;
            }
            moniker->Release();
            current++;
        }

        enum_moniker->Release();
        dev_enum->Release();

        if (!encontrado) { limpar_camera(cam); return -1; }

        // Adiciona o filtro do dispositivo ao grafo
        cam->graph->AddFilter(cam->device_filter, L"Video Capture");

        // Cria o Sample Grabber
        hr = CoCreateInstance(CLSID_SampleGrabber, NULL, CLSCTX_INPROC_SERVER,
                               IID_IBaseFilter, (void**)&cam->grabber_filter);
        if (FAILED(hr)) { limpar_camera(cam); return -1; }

        hr = cam->grabber_filter->QueryInterface(IID_ISampleGrabber, (void**)&cam->grabber);
        if (FAILED(hr)) { limpar_camera(cam); return -1; }

        // Configura o formato do grabber (RGB24)
        AM_MEDIA_TYPE mt;
        ZeroMemory(&mt, sizeof(mt));
        mt.majortype = MEDIATYPE_Video;
        mt.subtype = MEDIASUBTYPE_RGB24;
        cam->grabber->SetMediaType(&mt);

        cam->graph->AddFilter(cam->grabber_filter, L"Sample Grabber");

        // Cria o Null Renderer
        hr = CoCreateInstance(CLSID_NullRenderer, NULL, CLSCTX_INPROC_SERVER,
                               IID_IBaseFilter, (void**)&cam->null_renderer);
        if (FAILED(hr)) { limpar_camera(cam); return -1; }

        cam->graph->AddFilter(cam->null_renderer, L"Null Renderer");

        // Conecta os filtros
        hr = cam->capture->RenderStream(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video,
                                         cam->device_filter, cam->grabber_filter, cam->null_renderer);
        if (FAILED(hr)) { limpar_camera(cam); return -1; }

        // Obtém dimensões do vídeo
        AM_MEDIA_TYPE mt_connected;
        hr = cam->grabber->GetConnectedMediaType(&mt_connected);
        if (SUCCEEDED(hr)) {
            if (mt_connected.formattype == FORMAT_VideoInfo) {
                VIDEOINFOHEADER* vih = (VIDEOINFOHEADER*)mt_connected.pbFormat;
                cam->largura = vih->bmiHeader.biWidth;
                cam->altura = abs(vih->bmiHeader.biHeight);
            }
            if (mt_connected.pbFormat) CoTaskMemFree(mt_connected.pbFormat);
        }

        // Configura callback
        cam->callback = new SampleGrabberCallback();
        cam->callback->largura = cam->largura;
        cam->callback->altura = cam->altura;
        cam->grabber->SetCallback(cam->callback, 1);
        cam->grabber->SetBufferSamples(FALSE);
        cam->grabber->SetOneShot(FALSE);

        // Obtém controle de mídia
        cam->graph->QueryInterface(IID_IMediaControl, (void**)&cam->control);

        // Inicia captura
        hr = cam->control->Run();
        if (FAILED(hr)) { limpar_camera(cam); return -1; }

        cam->ativa = true;

        int id = (int)cameras.size();
        cameras.push_back(cam);
        return id;
    }

    // Lê frame da câmera
    unsigned char* ler(int id, int& largura, int& altura) {
        if (id < 0 || id >= (int)cameras.size()) return nullptr;
        
        Camera* cam = cameras[id];
        if (!cam || !cam->ativa || !cam->callback) return nullptr;

        return cam->callback->obter_frame_rgba(largura, altura);
    }

    // Limpa recursos da câmera
    void limpar_camera(Camera* cam) {
        if (!cam) return;
        
        if (cam->control) {
            cam->control->Stop();
            cam->control->Release();
        }
        if (cam->grabber) cam->grabber->Release();
        if (cam->null_renderer) {
            if (cam->graph) cam->graph->RemoveFilter(cam->null_renderer);
            cam->null_renderer->Release();
        }
        if (cam->grabber_filter) {
            if (cam->graph) cam->graph->RemoveFilter(cam->grabber_filter);
            cam->grabber_filter->Release();
        }
        if (cam->device_filter) {
            if (cam->graph) cam->graph->RemoveFilter(cam->device_filter);
            cam->device_filter->Release();
        }
        if (cam->capture) cam->capture->Release();
        if (cam->graph) cam->graph->Release();
        if (cam->callback) delete cam->callback;
        
        delete cam;
    }

    // Fecha câmera
    void fechar(int id) {
        if (id < 0 || id >= (int)cameras.size()) return;
        
        Camera* cam = cameras[id];
        limpar_camera(cam);
        cameras[id] = nullptr;
    }

    // Fecha todas as câmeras
    void fechar_todas() {
        for (int i = 0; i < (int)cameras.size(); i++) {
            if (cameras[i]) {
                limpar_camera(cameras[i]);
                cameras[i] = nullptr;
            }
        }
        cameras.clear();
    }

    ~GerenciadorCameras() {
        fechar_todas();
        if (com_inicializado) {
            CoUninitialize();
        }
    }
};

} // namespace cvn

#endif // CVN_CAMERA_HPP