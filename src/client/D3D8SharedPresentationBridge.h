#pragma once

#include "presenter/SharedTextureProducer.h"

#include <array>
#include <cstdint>
#include <memory>

namespace bfvr
{

namespace d3d8probe
{
struct D3DMatrix;
}

using D3D8SharedPresentationLogCallback = void (*)(const wchar_t* message);

enum class D3D8PresentationCompanion
{
    OpenXR,
    OfflineTransport
};

struct D3D8RuntimeView
{
    float orientationX = 0.0F;
    float orientationY = 0.0F;
    float orientationZ = 0.0F;
    float orientationW = 1.0F;
    float positionX = 0.0F;
    float positionY = 0.0F;
    float positionZ = 0.0F;
    float angleLeft = 0.0F;
    float angleRight = 0.0F;
    float angleUp = 0.0F;
    float angleDown = 0.0F;
};

struct D3D8RuntimeControllerPose
{
    float orientationX = 0.0F;
    float orientationY = 0.0F;
    float orientationZ = 0.0F;
    float orientationW = 1.0F;
    float positionX = 0.0F;
    float positionY = 0.0F;
    float positionZ = 0.0F;
};

struct D3D8RuntimeControllerHand
{
    DWORD flags = 0;
    DWORD buttons = 0;
    D3D8RuntimeControllerPose aimPose = {};
    D3D8RuntimeControllerPose gripPose = {};
    float triggerValue = 0.0F;
    float squeezeValue = 0.0F;
    float thumbstickX = 0.0F;
    float thumbstickY = 0.0F;
};

struct D3D8RuntimeControllerSample
{
    bool valid = false;
    bool sessionFocused = false;
    std::int64_t predictedDisplayTime = 0;
    LONG mountedCameraToggleSequence = 0;
    std::array<D3D8RuntimeControllerHand, 2> hands = {};
};

struct D3D8RuntimeRenderRequest
{
    LONG sequence = 0;
    std::int64_t predictedDisplayTime = 0;
    bool headPoseValid = false;
    bool headPoseTracked = false;
    bool standingHeightValid = false;
    float standingHeightMeters = 0.0F;
    LONG recenterForwardSequence = 0;
    D3D8RuntimeView headPose = {};
    std::array<D3D8RuntimeView, 2> views = {};
    D3D8RuntimeControllerSample controllerInput = {};
};

struct D3D8SharedFramePixels
{
    const void* data = nullptr;
    UINT rowPitch = 0;
    UINT width = 0;
    UINT height = 0;
};

struct D3D8RuntimeUiPlacement
{
    bool headLocked = true;
    bool worldAnchorValid = false;
    D3D8RuntimeView worldAnchor = {};
    bool backToGameVisible = false;
    bool backToGameHovered = false;
    bool mountedCameraDecoupled = false;
};

struct D3D8RuntimeDepthFrame
{
    bool valid = false;
    bool waterMaskValid = false;
    float projections[2][16] = {};
};

struct D3D8RuntimeMovementFrame
{
    bool valid = false;
    std::uintptr_t contextToken = 0;
    float worldPositionX = 0.0F;
    float worldPositionY = 0.0F;
    float worldPositionZ = 0.0F;
};

class D3D8SharedPresentationBridge
{
public:
    D3D8SharedPresentationBridge();
    ~D3D8SharedPresentationBridge();

    D3D8SharedPresentationBridge(const D3D8SharedPresentationBridge&) = delete;
    D3D8SharedPresentationBridge& operator=(const D3D8SharedPresentationBridge&) = delete;

    bool Initialize(
        UINT logicalUiWidth,
        UINT logicalUiHeight,
        float worldRenderScale,
        D3D8PresentationCompanion companion,
        D3D8SharedPresentationLogCallback logCallback,
        bool forceCpuTransport = false);
    bool EnsureGpuFrameTargets(
        void* d3d8Device,
        std::array<void*, shared::kTextureCount>& surfaces,
        std::array<void*, shared::kDepthTextureCount>& depthSurfaces,
        std::array<void*, shared::kDepthTextureCount>& depthExportSurfaces);
    bool RequestRender(
        D3D8RuntimeRenderRequest& request,
        DWORD timeoutMs);
    bool PublishFrame(
        const D3D8RuntimeRenderRequest& request,
        const std::array<D3D8SharedFramePixels, 3>& frame,
        const D3D8RuntimeUiPlacement& uiPlacement,
        const D3D8RuntimeMovementFrame& movementFrame);
    bool PublishGpuFrame(
        void* d3d8Device,
        const D3D8RuntimeRenderRequest& request,
        DWORD timeoutMs,
        const D3D8RuntimeUiPlacement& uiPlacement,
        const D3D8RuntimeMovementFrame& movementFrame,
        const D3D8RuntimeDepthFrame& depthFrame,
        const std::array<void*, shared::kDepthTextureCount>& depthSurfaces,
        const std::array<void*, shared::kDepthTextureCount>&
            depthExportSurfaces);
    bool WaitForConsumption(LONG sequence, DWORD timeoutMs);
    bool WaitForPresentation(LONG sequence, DWORD timeoutMs);
    void PrepareForResourceRelease();
    void Shutdown();

    [[nodiscard]] bool UsesGpuSharedTargets() const noexcept;
    [[nodiscard]] bool WaterReflectionsRequested() const noexcept;
    [[nodiscard]] UINT LeftWorldWidth() const noexcept;
    [[nodiscard]] UINT LeftWorldHeight() const noexcept;
    [[nodiscard]] UINT RightWorldWidth() const noexcept;
    [[nodiscard]] UINT RightWorldHeight() const noexcept;
    [[nodiscard]] UINT UiWidth() const noexcept;
    [[nodiscard]] UINT UiHeight() const noexcept;
    // Runtime swapchain dimensions before the game-resolution Ref2 producer
    // size replaces the UI fields in the x86-side requirements.
    [[nodiscard]] UINT RuntimeUiWidth() const noexcept;
    [[nodiscard]] UINT RuntimeUiHeight() const noexcept;
    [[nodiscard]] DWORD WorldD3DFormat() const noexcept;
    [[nodiscard]] DWORD UiD3DFormat() const noexcept;
    [[nodiscard]] DXGI_FORMAT Format() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] bool BuildD3D8RuntimeStereoTransforms(
    const D3D8RuntimeRenderRequest& request,
    const D3D8RuntimeView& referenceHead,
    const d3d8probe::D3DMatrix& sourceView,
    const d3d8probe::D3DMatrix& sourceProjection,
    d3d8probe::D3DMatrix& leftView,
    d3d8probe::D3DMatrix& rightView,
    d3d8probe::D3DMatrix& leftProjection,
    d3d8probe::D3DMatrix& rightProjection);

[[nodiscard]] bool BuildD3D8DiagnosticStereoTransforms(
    const d3d8probe::D3DMatrix& sourceView,
    const d3d8probe::D3DMatrix& sourceProjection,
    float halfEyeOffset,
    float convergenceDistance,
    d3d8probe::D3DMatrix& leftView,
    d3d8probe::D3DMatrix& rightView,
    d3d8probe::D3DMatrix& leftProjection,
    d3d8probe::D3DMatrix& rightProjection);

[[nodiscard]] D3D8RuntimeView MakeD3D8RuntimeHeadReference(
    const D3D8RuntimeRenderRequest& request) noexcept;

[[nodiscard]] bool BuildD3D8RuntimeRotationOnlyTransforms(
    const D3D8RuntimeRenderRequest& request,
    const D3D8RuntimeView& referenceHead,
    const d3d8probe::D3DMatrix& sourceView,
    const d3d8probe::D3DMatrix& sourceProjection,
    d3d8probe::D3DMatrix& leftView,
    d3d8probe::D3DMatrix& rightView,
    d3d8probe::D3DMatrix& leftProjection,
    d3d8probe::D3DMatrix& rightProjection);
} // namespace bfvr
