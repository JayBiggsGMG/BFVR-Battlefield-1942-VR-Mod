#pragma once

#include "openxr/OpenXRPresentation.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>

namespace bfvr
{
// OpenXR core presents to the headset but has no portable desktop mirror
// surface. This optional child canvas masks BF1942's original flat backbuffer
// with BFVR's left-eye image and the same UI texture sent to XR.
class DesktopMirror
{
public:
    DesktopMirror() = default;
    ~DesktopMirror();

    DesktopMirror(const DesktopMirror&) = delete;
    DesktopMirror& operator=(const DesktopMirror&) = delete;

    bool Initialize(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        DWORD producerProcessId,
        OpenXRLogCallback logCallback,
        void* logContext);
    void PumpMessages();
    void Render(const OpenXRPresentationTextures& textures);
    void Shutdown();

private:
    static LRESULT CALLBACK WindowProcedure(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam);

    bool EnsureWindow();
    bool EnsureSwapchain();
    bool EnsureSourceViews(const OpenXRPresentationTextures& textures);
    bool CreatePipeline();
    void UpdateWindowBounds();
    void ReleaseSourceViews();
    void ReleaseSwapchain();
    void Disable(const wchar_t* message);
    void WriteLog(const wchar_t* message) const;

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IDXGISwapChain1* swapchain_ = nullptr;
    ID3D11RenderTargetView* targetView_ = nullptr;
    ID3D11VertexShader* vertexShader_ = nullptr;
    ID3D11PixelShader* pixelShader_ = nullptr;
    ID3D11SamplerState* sampler_ = nullptr;
    ID3D11Buffer* cropConfiguration_ = nullptr;
    ID3D11ShaderResourceView* worldView_ = nullptr;
    ID3D11ShaderResourceView* uiView_ = nullptr;
    ID3D11Texture2D* worldTexture_ = nullptr;
    ID3D11Texture2D* uiTexture_ = nullptr;
    HWND parentWindow_ = nullptr;
    HWND window_ = nullptr;
    DWORD producerProcessId_ = 0;
    UINT bufferWidth_ = 0;
    UINT bufferHeight_ = 0;
    OpenXRLogCallback logCallback_ = nullptr;
    void* logContext_ = nullptr;
    bool initialized_ = false;
    bool permanentlyDisabled_ = false;
};
} // namespace bfvr
