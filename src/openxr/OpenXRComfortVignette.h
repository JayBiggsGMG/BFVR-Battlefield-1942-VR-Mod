#pragma once

#include "openxr/OpenXRPresentation.h"

#include <d3d11.h>

#ifndef XR_NO_PROTOTYPES
#define XR_NO_PROTOTYPES
#endif
#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <array>
#include <cstddef>
#include <vector>

namespace bfvr
{

struct OpenXRComfortVignetteApi
{
    PFN_xrCreateSwapchain createSwapchain = nullptr;
    PFN_xrDestroySwapchain destroySwapchain = nullptr;
    PFN_xrEnumerateSwapchainImages enumerateSwapchainImages = nullptr;
    PFN_xrAcquireSwapchainImage acquireSwapchainImage = nullptr;
    PFN_xrWaitSwapchainImage waitSwapchainImage = nullptr;
    PFN_xrReleaseSwapchainImage releaseSwapchainImage = nullptr;
};

// Renders one tiny procedural black/transparent aperture and submits it as
// two eye-exclusive VIEW quads. OpenXRPresentation inserts these immediately
// above the world projection and below every HUD/menu/overlay layer.
class OpenXRComfortVignette
{
public:
    OpenXRComfortVignette() = default;
    ~OpenXRComfortVignette();

    OpenXRComfortVignette(const OpenXRComfortVignette&) = delete;
    OpenXRComfortVignette& operator=(const OpenXRComfortVignette&) = delete;

    bool Initialize(
        XrSession session,
        DXGI_FORMAT swapchainFormat,
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const OpenXRComfortVignetteApi& api,
        OpenXRLogCallback logCallback,
        void* logContext);

    std::size_t AppendLayers(
        float targetStrength,
        float deltaSeconds,
        XrSpace viewSpace,
        const XrPosef& headInLocalSpace,
        const std::array<XrView, 2>& viewsInLocalSpace,
        const XrCompositionLayerBaseHeader** destination,
        std::size_t capacity);

    [[nodiscard]] float CurrentStrength() const noexcept;
    void Shutdown();

private:
    struct Swapchain
    {
        XrSwapchain handle = XR_NULL_HANDLE;
        UINT width = 0;
        UINT height = 0;
        std::vector<XrSwapchainImageD3D11KHR> images;
        std::vector<ID3D11RenderTargetView*> targets;
    };

    bool CreateSwapchain();
    bool CreateShaders();
    bool Render(float strength);
    void DestroySwapchain() noexcept;
    void WriteLog(const wchar_t* format, ...) const;

    OpenXRComfortVignetteApi api_ = {};
    XrSession session_ = XR_NULL_HANDLE;
    DXGI_FORMAT swapchainFormat_ = DXGI_FORMAT_UNKNOWN;
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    ID3D11VertexShader* vertexShader_ = nullptr;
    ID3D11PixelShader* pixelShader_ = nullptr;
    ID3D11Buffer* configurationBuffer_ = nullptr;
    Swapchain swapchain_ = {};
    std::array<XrCompositionLayerQuad, 2> layers_ = {};
    OpenXRLogCallback logCallback_ = nullptr;
    void* logContext_ = nullptr;
    float currentStrength_ = 0.0F;
    bool firstVisibleFrameLogged_ = false;
};

} // namespace bfvr
