#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace bfvr::shared
{
using SharedTextureLogCallback = void (*)(void* context, const wchar_t* message);

constexpr DWORD kProtocolMagic = 0x52564642; // "BFVR"
constexpr DWORD kProtocolVersion = 22;
constexpr std::size_t kTextureCount = 3;
constexpr std::size_t kDepthTextureCount = 2;
constexpr std::size_t kSharedNameCapacity = 128;
constexpr DWORD kProducerFlagRuntimeTimedRender = 0x1;
constexpr DWORD kProducerFlagAmbientOcclusionRequested = 0x2;
constexpr DWORD kProducerFlagWaterReflectionsRequested = 0x4;
constexpr DWORD kProducerFlagScreenSpaceGlobalIlluminationRequested = 0x8;
constexpr LONG kFrameOverlayBackToGameVisible = 0x1;
constexpr LONG kFrameOverlayBackToGameHovered = 0x2;
constexpr LONG kFramePresentationEyeFillingScope = 0x1;

enum class LocalPlayerLifeState : LONG
{
    Unknown = 0,
    Alive = 1,
    Dead = 2
};

constexpr DWORD kControllerSampleFlagSessionFocused = 0x1;
constexpr DWORD kControllerHandFlagAimActive = 0x1;
constexpr DWORD kControllerHandFlagAimPositionValid = 0x2;
constexpr DWORD kControllerHandFlagAimOrientationValid = 0x4;
constexpr DWORD kControllerHandFlagGripActive = 0x8;
constexpr DWORD kControllerHandFlagGripPositionValid = 0x10;
constexpr DWORD kControllerHandFlagGripOrientationValid = 0x20;
constexpr DWORD kControllerHandFlagTriggerActive = 0x40;
constexpr DWORD kControllerHandFlagSqueezeActive = 0x80;
constexpr DWORD kControllerHandFlagThumbstickActive = 0x100;
constexpr DWORD kControllerHandFlagAimPositionTracked = 0x200;
constexpr DWORD kControllerHandFlagAimOrientationTracked = 0x400;
constexpr DWORD kControllerHandFlagGripPositionTracked = 0x800;
constexpr DWORD kControllerHandFlagGripOrientationTracked = 0x1000;
constexpr DWORD kControllerHandButtonPrimary = 0x1;
constexpr DWORD kControllerHandButtonSecondary = 0x2;
constexpr DWORD kControllerHandButtonMenu = 0x4;
constexpr DWORD kControllerHandButtonThumbstick = 0x8;

enum class ProcessState : LONG
{
    Failed = -1,
    Empty = 0,
    Starting = 1,
    RequirementsReady = 2,
    TexturesReady = 3,
    Running = 4,
    Stopping = 5,
    Stopped = 6
};

enum class TextureSlot : std::size_t
{
    LeftWorld = 0,
    RightWorld = 1,
    Ref2Ui = 2
};

enum class DepthTextureSlot : std::size_t
{
    LeftWorld = 0,
    RightWorld = 1
};

enum class DepthEncoding : LONG
{
    None = 0,
    PackedDeviceDepthBgra8 = 1
};

enum class SharedTextureTransport : DWORD
{
    NamedNtHandle = 1,
    D3D9LegacyHandle = 2
};

enum class UiReferenceMode : LONG
{
    WorldLocked = 0,
    HeadLocked = 1
};

struct SharedTextureDescription
{
    DWORD width = 0;
    DWORD height = 0;
    DWORD format = 0;
    DWORD transport = 0;
    DWORD sharedHandleLow = 0;
    DWORD sharedHandleHigh = 0;
    wchar_t name[kSharedNameCapacity] = {};
};

inline void StoreLegacySharedHandle(
    SharedTextureDescription& description,
    HANDLE handle) noexcept
{
    const ULONGLONG value =
        static_cast<ULONGLONG>(reinterpret_cast<ULONG_PTR>(handle));
    description.sharedHandleLow = static_cast<DWORD>(value);
    description.sharedHandleHigh = static_cast<DWORD>(value >> 32);
}

[[nodiscard]] inline HANDLE LoadLegacySharedHandle(
    const SharedTextureDescription& description) noexcept
{
    const ULONGLONG value =
        static_cast<ULONGLONG>(description.sharedHandleLow) |
        (static_cast<ULONGLONG>(description.sharedHandleHigh) << 32);
    return reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(value));
}

struct PresentationRequirements
{
    DWORD leftWorldWidth = 0;
    DWORD leftWorldHeight = 0;
    DWORD rightWorldWidth = 0;
    DWORD rightWorldHeight = 0;
    DWORD uiWidth = 0;
    DWORD uiHeight = 0;
    DWORD format = 0;
    LONG adapterLuidHigh = 0;
    DWORD adapterLuidLow = 0;
    DWORD minimumFeatureLevel = 0;
    DWORD deviceFeatureLevel = 0;
};

struct SharedPresentationPose
{
    float orientationX = 0.0F;
    float orientationY = 0.0F;
    float orientationZ = 0.0F;
    float orientationW = 1.0F;
    float positionX = 0.0F;
    float positionY = 0.0F;
    float positionZ = 0.0F;
};

struct SharedPresentationFov
{
    float angleLeft = 0.0F;
    float angleRight = 0.0F;
    float angleUp = 0.0F;
    float angleDown = 0.0F;
};

struct SharedPresentationView
{
    SharedPresentationPose pose = {};
    SharedPresentationFov fov = {};
};

struct SharedRenderRequest
{
    alignas(8) LONGLONG predictedDisplayTime = 0;
    LONG shouldRender = 0;
    LONG viewsValid = 0;
    LONG headPoseValid = 0;
    LONG headPoseTracked = 0;
    // Current headset height above the OpenXR STAGE floor, located at the
    // same predicted display time as headPose. This lets x86 derive the floor
    // in immutable LOCAL coordinates without adding the user's entire height
    // on top of BF1942's already-authored eye camera.
    LONG standingHeightValid = 0;
    float standingHeightMeters = 0.0F;
    // Presenter-owned command edge. The x86 camera owner captures the
    // headset as the neutral pose for the currently verified infantry body or
    // occupied seat; OpenXR's runtime reference space is never rewritten.
    LONG recenterForwardSequence = 0;
    SharedPresentationPose headPose = {};
    SharedPresentationView views[2] = {};
};

struct SharedDepthFrameParameters
{
    // Exact row-major D3D8 projection matrices used for the two world replays.
    // The x64 AO stage reconstructs view position from the matching packed
    // device depth; it must not substitute an approximate runtime projection.
    float projections[2][16] = {};
};

// Controller payloads are sampled by the x64 OpenXR owner at the render
// request's predicted display time. The x86 client must reject a sample unless
// its sequence and timestamp match that request exactly. The protocol carries
// no game-input field: any local input use remains an x86-client current-frame
// transform behind independent focus, freshness, and ownership checks. The
// separate frame overlay flags below describe presenter-owned UI only.
struct SharedControllerHandSample
{
    DWORD flags = 0;
    DWORD buttons = 0;
    SharedPresentationPose aimPose = {};
    SharedPresentationPose gripPose = {};
    float triggerValue = 0.0F;
    float squeezeValue = 0.0F;
    float thumbstickX = 0.0F;
    float thumbstickY = 0.0F;
};

struct SharedControllerSample
{
    alignas(8) LONGLONG predictedDisplayTime = 0;
    DWORD flags = 0;
    // Presenter-owned monotonic action edge. The x86 camera hook consumes a
    // new value only for the currently verified occupied weapon station.
    LONG mountedCameraToggleSequence = 0;
    SharedControllerHandSample hands[2] = {};
};

// The mapping is intentionally pointer-free so one definition is valid in the
// injected x86 client and the x64 companion. State transitions use the Win32
// interlocked API; payload fields are published before their matching state.
struct ControlBlock
{
    DWORD magic = 0;
    DWORD version = 0;
    DWORD byteSize = 0;
    DWORD producerProcessId = 0;
    DWORD presenterProcessId = 0;
    volatile LONG producerState = static_cast<LONG>(ProcessState::Empty);
    volatile LONG presenterState = static_cast<LONG>(ProcessState::Empty);
    volatile LONG shutdownRequested = 0;
    volatile LONG frameSequence = 0;
    volatile LONG consumedFrameSequence = 0;
    volatile LONG producedFrameCount = 0;
    volatile LONG transportedFrameCount = 0;
    volatile LONG presentedFrameCount = 0;
    volatile LONG presenterError = 0;
    DWORD producerFlags = 0;
    volatile LONG renderReadySequence = 0;
    volatile LONG renderRequestSequence = 0;
    volatile LONG renderedFrameSequence = 0;
    volatile LONG controllerSampleSequence = 0;
    // Producer-to-presenter event counters. Each accepted x86 event advances
    // exactly one counter; the x64 OpenXR owner consumes deltas without
    // transferring pointers or mistaking held input for repeated events.
    volatile LONG hapticShotRightSequence = 0;
    volatile LONG hapticShotBothSequence = 0;
    volatile LONG hapticDeathSequence = 0;
    volatile LONG hapticNativeMenuHoverSequence = 0;
    // Producer-to-presenter authoritative local-kill event. The x64 audio
    // owner consumes deltas and creates one independent voice per event.
    volatile LONG killSoundSequence = 0;
    // Presenter-to-producer BFVR-menu feedback requests. The x86 game thread
    // consumes these counters and invokes Battlefield's own BfMenu wrappers;
    // no game pointer or audio object crosses the process boundary.
    volatile LONG nativeMenuSoundHighlightSequence = 0;
    volatile LONG nativeMenuSoundOkSequence = 0;
    volatile LONG nativeMenuSoundCancelSequence = 0;
    // Continuously published by the same verified local-player poll that
    // emits hapticDeathSequence. The presenter uses Alive to end the bounded
    // death-comfort interval immediately on respawn.
    volatile LONG localPlayerLifeState =
        static_cast<LONG>(LocalPlayerLifeState::Unknown);
    // Published by the x86 producer before frameSequence. Gameplay HUD uses
    // VIEW; native menus use a latched LOCAL pose.
    volatile LONG frameUiReferenceMode =
        static_cast<LONG>(UiReferenceMode::WorldLocked);
    // When non-zero, the x86 producer supplies the gravity-aligned LOCAL
    // anchor shared by the world-locked menu panel and controller-ray mapper.
    volatile LONG frameUiWorldAnchorValid = 0;
    SharedPresentationPose frameUiWorldAnchor = {};
    // Published by the x86 producer before frameSequence. These flags control
    // only presenter-owned pixels composited into the copied Ref2 UI texture.
    volatile LONG frameOverlayFlags = 0;
    // Published before frameSequence. A verified native useScope view promotes
    // Ref2 UI from the ordinary panel to centred eye-exclusive VIEW quads that
    // cover both OpenXR eye frusta.
    volatile LONG framePresentationFlags = 0;
    // Published by the x86 camera owner. The presenter mirrors this actual
    // seat-local state into the utility strip; it never assumes a toggle was
    // accepted merely because the user released the UI button.
    volatile LONG mountedCameraDecoupled = 0;
    // Published before frameSequence from the local infantry/occupied control
    // object's world transform. Orientation is not transported: this metadata
    // can drive a movement comfort vignette without reacting to head look,
    // artificial turning, turret aim, or vehicle rotation in place. The opaque
    // token lets x64 re-baseline instead of treating seat/respawn changes as
    // extreme velocity.
    volatile LONG frameMovementOriginValid = 0;
    DWORD frameMovementContextTokenLow = 0;
    DWORD frameMovementContextTokenHigh = 0;
    float frameMovementOriginX = 0.0F;
    float frameMovementOriginY = 0.0F;
    float frameMovementOriginZ = 0.0F;
    // Optional AO depth transport is additive to the three established color
    // slots. The producer publishes descriptors before TexturesReady and
    // publishes the per-frame matrix payload before frameSequence.
    volatile LONG depthTextureCount = 0;
    volatile LONG depthEncoding = static_cast<LONG>(DepthEncoding::None);
    volatile LONG frameDepthValid = 0;
    // The alpha channel of each packed-depth texture contains a fail-closed
    // effective-opacity mask from the exact additive water pass. RGB remains
    // the packed device depth consumed by AO and water SSR.
    volatile LONG frameWaterMaskValid = 0;
    SharedDepthFrameParameters frameDepth = {};
    PresentationRequirements requirements = {};
    SharedRenderRequest renderRequest = {};
    SharedControllerSample controllerSample = {};
    SharedTextureDescription textures[kTextureCount] = {};
    SharedTextureDescription depthTextures[kDepthTextureCount] = {};
    DWORD firstConsumedPixels[kTextureCount] = {};
};

static_assert(std::is_standard_layout_v<ControlBlock>);
static_assert(sizeof(ControlBlock) < 4096);
} // namespace bfvr::shared
