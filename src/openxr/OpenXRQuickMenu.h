#pragma once

#include "client/QuickMenuArt.h"
#include "openxr/OpenXRPresentation.h"
#include "stereo/QuickMenuInteraction.h"

#include <windows.h>
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

struct OpenXRQuickMenuApi
{
    PFN_xrCreateSwapchain createSwapchain = nullptr;
    PFN_xrDestroySwapchain destroySwapchain = nullptr;
    PFN_xrEnumerateSwapchainImages enumerateSwapchainImages = nullptr;
    PFN_xrAcquireSwapchainImage acquireSwapchainImage = nullptr;
    PFN_xrWaitSwapchainImage waitSwapchainImage = nullptr;
    PFN_xrReleaseSwapchainImage releaseSwapchainImage = nullptr;
};

// Owns only BFVR/OpenXR/D3D11 resources. It does not dispatch keyboard input;
// the foreground-checked presenter consumes release commands separately.
class OpenXRQuickMenu
{
public:
    OpenXRQuickMenu() = default;
    ~OpenXRQuickMenu();

    OpenXRQuickMenu(const OpenXRQuickMenu&) = delete;
    OpenXRQuickMenu& operator=(const OpenXRQuickMenu&) = delete;

    bool Initialize(
        const wchar_t* payloadDirectory,
        XrSession session,
        DXGI_FORMAT swapchainFormat,
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const OpenXRQuickMenuApi& api,
        OpenXRLogCallback logCallback,
        void* logContext);
    void Update(const OpenXRPresentationFrameState& frame) noexcept;
    void SetMountedCameraDecoupled(bool decoupled) noexcept;

    std::size_t AppendLayers(
        XrSpace localSpace,
        const XrCompositionLayerBaseHeader** destination,
        std::size_t capacity);
    [[nodiscard]] stereo::QuickMenuSelection
    TakeReleasedSelection() noexcept;
    [[nodiscard]] bool GetMirrorState(
        OpenXRQuickMenuMirrorState& state) const noexcept;
    [[nodiscard]] bool IsReady() const noexcept;
    void Shutdown();

private:
    struct Swapchain
    {
        XrSwapchain handle = XR_NULL_HANDLE;
        UINT width = 0;
        UINT height = 0;
        std::vector<XrSwapchainImageD3D11KHR> images;
    };

    bool CreateSwapchain(Swapchain& swapchain, UINT width, UINT height);
    bool CreateSourceTexture(
        const std::vector<std::uint32_t>& bgraPixels,
        UINT width,
        UINT height,
        ID3D11Texture2D** texture);
    bool CopyToSwapchain(
        Swapchain& target,
        ID3D11Texture2D* source,
        const wchar_t* label);
    void DestroySwapchain(Swapchain& swapchain) noexcept;
    void WriteLog(const wchar_t* format, ...) const;

    OpenXRQuickMenuApi api_ = {};
    XrSession session_ = XR_NULL_HANDLE;
    DXGI_FORMAT swapchainFormat_ = DXGI_FORMAT_UNKNOWN;
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    std::array<ID3D11Texture2D*, stereo::kQuickMenuSelectionCount>
        menuSources_ = {};
    std::array<ID3D11Texture2D*, 8> utilitySources_ = {};
    ID3D11Texture2D* cursorSource_ = nullptr;
    Swapchain menuSwapchain_ = {};
    Swapchain utilitySwapchain_ = {};
    Swapchain cursorSwapchain_ = {};
    XrCompositionLayerQuad menuLayer_{XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerQuad utilityLayer_{XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerQuad cursorLayer_{XR_TYPE_COMPOSITION_LAYER_QUAD};
    stereo::QuickMenuInteraction interaction_ = {};
    OpenXRLogCallback logCallback_ = nullptr;
    void* logContext_ = nullptr;
    bool firstVisibleFrameLogged_ = false;
    bool mountedCameraDecoupled_ = false;
};

} // namespace bfvr
