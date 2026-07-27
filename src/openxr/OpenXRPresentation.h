#pragma once

#include <d3d11.h>

#include <array>
#include <cstdint>
#include <memory>

namespace bfvr
{
using OpenXRLogCallback = void (*)(void* context, const wchar_t* message);

enum class OpenXRUiLayerMode
{
    Quad,
    Cylinder
};

enum class OpenXRUiReferenceMode
{
    WorldLocked,
    HeadLocked
};

// The world and UI sources are BFVR-owned D3D11 textures. They must match the
// requirements returned by OpenXRPresentation::GetTextureRequirements exactly;
// this first presentation boundary intentionally does not resample textures or
// retain any D3D8/game-owned resource.
struct OpenXRPresentationTextures
{
    ID3D11Texture2D* leftWorld = nullptr;
    ID3D11Texture2D* rightWorld = nullptr;
    ID3D11Texture2D* ref2Ui = nullptr;
};

struct OpenXRPresentationPose
{
    float orientationX = 0.0F;
    float orientationY = 0.0F;
    float orientationZ = 0.0F;
    float orientationW = 1.0F;
    float positionX = 0.0F;
    float positionY = 0.0F;
    float positionZ = 0.0F;
};

struct OpenXRPresentationFov
{
    float angleLeft = 0.0F;
    float angleRight = 0.0F;
    float angleUp = 0.0F;
    float angleDown = 0.0F;
};

struct OpenXRPresentationView
{
    OpenXRPresentationPose pose = {};
    OpenXRPresentationFov fov = {};
};

struct OpenXRControllerHandState
{
    bool aimActive = false;
    bool aimPositionValid = false;
    bool aimOrientationValid = false;
    bool aimPositionTracked = false;
    bool aimOrientationTracked = false;
    bool gripActive = false;
    bool gripPositionValid = false;
    bool gripOrientationValid = false;
    bool gripPositionTracked = false;
    bool gripOrientationTracked = false;
    bool triggerActive = false;
    bool squeezeActive = false;
    bool thumbstickActive = false;
    bool primaryPressed = false;
    bool secondaryPressed = false;
    bool menuPressed = false;
    OpenXRPresentationPose aimPose = {};
    OpenXRPresentationPose gripPose = {};
    float triggerValue = 0.0F;
    float squeezeValue = 0.0F;
    float thumbstickX = 0.0F;
    float thumbstickY = 0.0F;
};

// This state is sampled only by the x64 OpenXR session owner and crosses the
// pointer-free presentation transport. The x86 client may consume a fresh,
// focused sample only through its separately gated temporary local PlayerInput
// frame overlay; it must not mutate simulation or network state.
struct OpenXRControllerInputState
{
    std::int64_t predictedDisplayTime = 0;
    bool sessionFocused = false;
    std::array<OpenXRControllerHandState, 2> hands = {};
};

struct OpenXRPresentationFrameState
{
    std::int64_t predictedDisplayTime = 0;
    bool shouldRender = false;
    bool viewsValid = false;
    bool headPoseValid = false;
    bool headPoseTracked = false;
    OpenXRPresentationPose headPose = {};
    std::array<OpenXRPresentationView, 2> views = {};
    OpenXRControllerInputState controllerInput = {};
};

struct OpenXRPresentationTextureRequirements
{
    UINT leftWorldWidth = 0;
    UINT leftWorldHeight = 0;
    UINT rightWorldWidth = 0;
    UINT rightWorldHeight = 0;
    UINT uiWidth = 0;
    UINT uiHeight = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    LUID adapterLuid = {};
    D3D_FEATURE_LEVEL minimumFeatureLevel = D3D_FEATURE_LEVEL_9_1;
    D3D_FEATURE_LEVEL deviceFeatureLevel = D3D_FEATURE_LEVEL_9_1;
};

struct OpenXRPresentationConfiguration
{
    OpenXRUiLayerMode uiLayerMode = OpenXRUiLayerMode::Quad;
    float uiDistanceMeters = 1.5F;
    float uiWidthMeters = 1.6F;
    float uiCylinderCentralAngleRadians = 1.3F;
    // Standalone-probe-only control: deliberately omits the otherwise required
    // D3D11 binding from xrCreateSession to distinguish loader dispatch from
    // graphics-binding validation. It must never be enabled by BFVR's client.
    bool diagnosticOmitGraphicsBinding = false;
    // Standalone-probe-only control: with an HMD already active, create the
    // D3D11 instance directly instead of the bootstrap's baseline-instance /
    // destroy / D3D11-instance sequence.
    bool diagnosticDirectD3D11Instance = false;
};

// A session-scoped OpenXR/D3D11 presentation boundary. It selects the exact
// adapter requested by the runtime, owns only OpenXR/D3D11 resources, and
// consumes BFVR-owned textures. The game-hook and D3D8 capture paths do not
// call this class yet.
class OpenXRPresentation
{
public:
    OpenXRPresentation();
    ~OpenXRPresentation();

    OpenXRPresentation(const OpenXRPresentation&) = delete;
    OpenXRPresentation& operator=(const OpenXRPresentation&) = delete;

    bool Initialize(
        const wchar_t* payloadDirectory,
        const OpenXRPresentationConfiguration& configuration,
        OpenXRLogCallback logCallback,
        void* logContext);

    // Processes OpenXR lifecycle events. A READY event begins the session;
    // STOPPING ends it. Returns false only for a terminal runtime failure.
    bool PollEvents();

    // Waits/begins/ends one OpenXR frame. When the session is running and the
    // runtime requests rendering, it copies the three supplied BFVR-owned
    // textures into the acquired swapchain images and submits world+UI layers.
    bool SubmitFrame(
        const OpenXRPresentationTextures& textures,
        OpenXRUiReferenceMode uiReferenceMode =
            OpenXRUiReferenceMode::HeadLocked);

    // Two-phase form used by the cross-process presenter. BeginFrame waits for
    // runtime timing and locates both predicted eye views before the x86 game
    // renders. EndFrame consumes the resulting BFVR textures and submits those
    // exact views. Every successful BeginFrame must be paired with EndFrame.
    bool BeginFrame(OpenXRPresentationFrameState& frameState);
    bool EndFrame(
        const OpenXRPresentationTextures& textures,
        OpenXRUiReferenceMode uiReferenceMode =
            OpenXRUiReferenceMode::HeadLocked);

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] bool IsSessionRunning() const noexcept;
    [[nodiscard]] OpenXRPresentationTextureRequirements GetTextureRequirements() const noexcept;

    // Borrowed pointers; they remain valid only until Shutdown/destruction.
    [[nodiscard]] ID3D11Device* GetD3D11Device() const noexcept;
    [[nodiscard]] ID3D11DeviceContext* GetD3D11Context() const noexcept;

    void Shutdown();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bfvr
