#include "client/D3D8StereoPairProbe.h"
#include "client/D3D8PresentationConfiguration.h"
#include "client/D3D8PerformanceTiming.h"
#include "client/D3D8RenderViewPoseHook.h"
#include "client/BFSoldierVrMotionFilter.h"
#include "client/D3D8RuntimePosePolicy.h"
#include "client/D3D8RuntimeDiagnostics.h"
#include "client/ControllerInputOverlay.h"
#include "client/CrosshairOverlay.h"
#include "client/D3D8WorldCrosshairRenderer.h"
#include "client/MainMenuOverlay.h"
#include "client/MenuPointerOverlay.h"
#include "client/StartupMenuPresentation.h"
#include "client/D3D8WeaponMotionOverlay.h"
#include "client/WeaponAimOverlay.h"
#include "client/D3D8SharedPresentationBridge.h"
#include "client/D3D8StereoFrameTransfer.h"
#include "client/D3D8StereoReadback.h"
#include "client/D3D8SpriteShaderTransform.h"
#include "client/D3D8StereoShaderTransform.h"
#include "client/D3D8StereoProbeReporting.h"
#include "client/D3D8TreeSpriteShaderTransform.h"
#include "client/D3D8To9VertexShaderIdentity.h"

#include "stereo/D3D8DrawPolicy.h"
#include "stereo/D3D8FirstPersonArmPolicy.h"
#include "stereo/D3D8FrameCompositionPolicy.h"
#include "stereo/D3D8SemanticDrawPolicy.h"
#include "stereo/StereoMath.h"
#include "stereo/UiPointerMath.h"

#include <MinHook.h>

#include <windows.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <intrin.h>
#include <iterator>
namespace
{
using bfvr::d3d8probe::D3DMatrix;
using bfvr::d3d8probe::D3DSurfaceDescription;
using bfvr::d3d8probe::D3DViewport;
using bfvr::d3d8probe::DrawProvenanceRecord;
using bfvr::d3d8probe::FrameDrawKind;
using bfvr::d3d8probe::kMaximumProvenanceSites;
using bfvr::d3d8probe::kProvenanceStackDepth;
using bfvr::d3d8probe::PresentationRunRecord;
using bfvr::d3d8probe::ReadbackResult;
using bfvr::d3d8probe::StereoFrameRecord;
using bfvr::d3d8probe::StereoPairRecord;
using bfvr::d3d8probe::GetVertexShaderConstantFn;
using bfvr::d3d8probe::SetVertexShaderConstantFn;

constexpr std::size_t kIUnknownReleaseSlot = 2;
constexpr std::size_t kDirect3DDevice8ResetSlot = 14;
constexpr std::size_t kDirect3DDevice8PresentSlot = 15;
constexpr std::size_t kDirect3DDevice8CreateRenderTargetSlot = 25;
constexpr std::size_t kDirect3DDevice8CreateDepthStencilSurfaceSlot = 26;
constexpr std::size_t kDirect3DDevice8CreateImageSurfaceSlot = 27;
constexpr std::size_t kDirect3DDevice8CopyRectsSlot = 28;
constexpr std::size_t kDirect3DDevice8SetRenderTargetSlot = 31;
constexpr std::size_t kDirect3DDevice8GetRenderTargetSlot = 32;
constexpr std::size_t kDirect3DDevice8GetDepthStencilSurfaceSlot = 33;
constexpr std::size_t kDirect3DDevice8ClearSlot = 36;
constexpr std::size_t kDirect3DDevice8SetTransformSlot = 37;
constexpr std::size_t kDirect3DDevice8GetTransformSlot = 38;
constexpr std::size_t kDirect3DDevice8SetViewportSlot = 40;
constexpr std::size_t kDirect3DDevice8GetViewportSlot = 41;
constexpr std::size_t kDirect3DDevice8GetRenderStateSlot = 51;
// IDirect3DDevice8::GetTextureStageState follows GetTexture/SetTexture at
// slots 60/61. Slot 64 is ValidateDevice and has an incompatible signature.
constexpr std::size_t kDirect3DDevice8GetTextureStageStateSlot = 62;
constexpr std::size_t kDirect3DDevice8DrawPrimitiveSlot = 70;
constexpr std::size_t kDirect3DDevice8DrawIndexedPrimitiveSlot = 71;
constexpr std::size_t kDirect3DDevice8DrawPrimitiveUPSlot = 72;
constexpr std::size_t kDirect3DDevice8DrawIndexedPrimitiveUPSlot = 73;
constexpr std::size_t kDirect3DDevice8GetVertexShaderSlot = 77;
constexpr std::size_t kDirect3DDevice8SetVertexShaderConstantSlot = 79;
constexpr std::size_t kDirect3DDevice8GetVertexShaderConstantSlot = 80;
constexpr std::size_t kDirect3DSurface8GetDescSlot = 8;

constexpr DWORD kD3DTransformView = 2;
constexpr DWORD kD3DTransformProjection = 3;
constexpr DWORD kD3DTransformWorld = 0x100;
constexpr DWORD kD3DClearTarget = 0x1;
constexpr DWORD kD3DClearZBuffer = 0x2;
constexpr DWORD kD3DClearStencil = 0x4;
constexpr UINT kD3DFormatA8R8G8B8 = 21;
constexpr UINT kD3DFormatX8R8G8B8 = 22;
constexpr DWORD kD3DRenderStateZEnable = 7;
constexpr DWORD kD3DRenderStateZWriteEnable = 14;
constexpr DWORD kD3DRenderStateAlphaBlendEnable = 27;
constexpr DWORD kD3DRenderStateFogEnable = 28;
constexpr DWORD kD3DRenderStateLighting = 137;
constexpr DWORD kD3DRenderStateSourceBlend = 19;
constexpr DWORD kD3DRenderStateDestinationBlend = 20;
constexpr DWORD kD3DRenderStateTextureFactor = 60;
constexpr DWORD kOwnedTargetClearColor = 0xFF102030;
constexpr DWORD kMenuLayerClearColor = 0x00000000;
constexpr UINT kMinimumCandidatePrimitiveCount = 1000;
constexpr LONG kMaximumEmptyCandidateAttempts = 8;
constexpr LONG kMaximumFrameDraws = 4096;
constexpr std::uintptr_t kGameDrawPrimitiveUPReturn = 0x00667DFD;
constexpr std::uintptr_t kGameDrawPrimitiveReturn = 0x00667EF4;
constexpr std::uintptr_t kGameDrawIndexedPrimitiveReturn = 0x0066800A;
constexpr std::uintptr_t kTreeMeshDrawBlocksReturn = 0x0067C997;
constexpr std::uintptr_t kRendererDrawPrimitiveUpReturn = 0x007EBFF6;
constexpr float kDiagnosticHalfEyeOffset = 0.032F;
constexpr float kDiagnosticConvergenceDistance = 10.0F;
constexpr DWORD kCaptureTimeoutMs = 75000;
constexpr DWORD kPresentationDurationMs = 60000;
constexpr DWORD kBoundedRenderRequestTimeoutMs = 2000;
constexpr DWORD kContinuousRenderRequestTimeoutMs = 0;
constexpr DWORD kContinuousConsumptionTimeoutMs = 250;
constexpr wchar_t kRunUntilStoppedEnvironment[] =
    L"BFVR_PRESENTATION_RUN_UNTIL_STOPPED";
constexpr wchar_t kStereoWaterReflectionEnvironment[] =
    L"BFVR_STEREO_WATER_REFLECTION";

using PresentFn = HRESULT(WINAPI*)(
    void* device,
    const RECT* sourceRectangle,
    const RECT* destinationRectangle,
    HWND destinationWindowOverride,
    const RGNDATA* dirtyRegion);
using ResetFn = HRESULT(WINAPI*)(void* device, void* presentationParameters);
using CreateRenderTargetFn = HRESULT(WINAPI*)(
    void* device,
    UINT width,
    UINT height,
    UINT format,
    UINT multiSampleType,
    BOOL lockable,
    void** returnedSurface);
using CreateDepthStencilSurfaceFn = HRESULT(WINAPI*)(
    void* device,
    UINT width,
    UINT height,
    UINT format,
    UINT multiSampleType,
    void** returnedSurface);
using CreateImageSurfaceFn = HRESULT(WINAPI*)(
    void* device,
    UINT width,
    UINT height,
    UINT format,
    void** returnedSurface);
using CopyRectsFn = HRESULT(WINAPI*)(
    void* device,
    void* sourceSurface,
    const RECT* sourceRectangles,
    UINT rectangleCount,
    void* destinationSurface,
    const POINT* destinationPoints);
using SetRenderTargetFn = HRESULT(WINAPI*)(
    void* device,
    void* colorRenderTarget,
    void* depthStencilSurface);
using GetRenderTargetFn = HRESULT(WINAPI*)(void* device, void** returnedSurface);
using GetDepthStencilSurfaceFn = HRESULT(WINAPI*)(void* device, void** returnedSurface);
using ClearFn = HRESULT(WINAPI*)(
    void* device,
    DWORD rectangleCount,
    const void* rectangles,
    DWORD flags,
    DWORD color,
    float z,
    DWORD stencil);
using SetTransformFn = HRESULT(WINAPI*)(void* device, DWORD state, const void* matrix);
using GetTransformFn = HRESULT(WINAPI*)(void* device, DWORD state, void* matrix);
using SetViewportFn = HRESULT(WINAPI*)(void* device, const D3DViewport* viewport);
using GetViewportFn = HRESULT(WINAPI*)(void* device, D3DViewport* viewport);
using GetRenderStateFn = HRESULT(WINAPI*)(void* device, DWORD state, DWORD* value);
using GetTextureStageStateFn = HRESULT(WINAPI*)(
    void* device,
    DWORD stage,
    DWORD type,
    DWORD* value);
using DrawPrimitiveFn = HRESULT(WINAPI*)(
    void* device,
    DWORD primitiveType,
    UINT startVertex,
    UINT primitiveCount);
using DrawIndexedPrimitiveFn = HRESULT(WINAPI*)(
    void* device,
    DWORD primitiveType,
    UINT minimumVertexIndex,
    UINT vertexCount,
    UINT startIndex,
    UINT primitiveCount);
using DrawPrimitiveUPFn = HRESULT(WINAPI*)(
    void* device,
    DWORD primitiveType,
    UINT primitiveCount,
    const void* vertexData,
    UINT vertexStride);
using DrawIndexedPrimitiveUPFn = HRESULT(WINAPI*)(
    void* device,
    DWORD primitiveType,
    UINT minimumVertexIndex,
    UINT vertexCount,
    UINT primitiveCount,
    const void* indexData,
    UINT indexFormat,
    const void* vertexData,
    UINT vertexStride);
using GetVertexShaderFn = HRESULT(WINAPI*)(void* device, DWORD* vertexShaderOrFvf);
using GetSurfaceDescriptionFn = HRESULT(WINAPI*)(
    void* surface,
    D3DSurfaceDescription* description);
using ReleaseUnknownFn = ULONG(STDMETHODCALLTYPE*)(void* unknown);

enum class AttemptResult
{
    NotEligible,
    EmptyOrIdentical,
    Completed,
    Failed
};

enum class ProbeMode
{
    OneDraw,
    FullFrame,
    FullFramePresentation
};

enum class FrameMirrorResult
{
    NotMirrored,
    Mirrored,
    IntentionallySuppressed,
    Failed
};

struct FrameDrawInvocation
{
    FrameDrawKind kind = FrameDrawKind::Primitive;
    DWORD primitiveType = 0;
    UINT startVertex = 0;
    UINT minimumVertexIndex = 0;
    UINT vertexCount = 0;
    UINT startIndex = 0;
    UINT primitiveCount = 0;
    const void* indexData = nullptr;
    UINT indexFormat = 0;
    const void* vertexData = nullptr;
    UINT vertexStride = 0;
    void* gameStack[kProvenanceStackDepth] = {};
    UINT gameStackDepth = 0;
    bool replayWeaponMotion = false;
    D3DMatrix weaponMotionWorldAttachment = {};
};

struct DeviceMethods
{
    ResetFn reset = nullptr;
    PresentFn present = nullptr;
    CreateRenderTargetFn createRenderTarget = nullptr;
    CreateDepthStencilSurfaceFn createDepthStencilSurface = nullptr;
    CreateImageSurfaceFn createImageSurface = nullptr;
    CopyRectsFn copyRects = nullptr;
    SetRenderTargetFn setRenderTarget = nullptr;
    GetRenderTargetFn getRenderTarget = nullptr;
    GetDepthStencilSurfaceFn getDepthStencilSurface = nullptr;
    ClearFn clear = nullptr;
    SetTransformFn setTransform = nullptr;
    GetTransformFn getTransform = nullptr;
    SetViewportFn setViewport = nullptr;
    GetViewportFn getViewport = nullptr;
    GetRenderStateFn getRenderState = nullptr;
    GetTextureStageStateFn getTextureStageState = nullptr;
    DrawPrimitiveFn drawPrimitive = nullptr;
    DrawIndexedPrimitiveFn drawIndexedPrimitive = nullptr;
    DrawPrimitiveUPFn drawPrimitiveUP = nullptr;
    DrawIndexedPrimitiveUPFn drawIndexedPrimitiveUP = nullptr;
    GetVertexShaderFn getVertexShader = nullptr;
    SetVertexShaderConstantFn setVertexShaderConstant = nullptr;
    GetVertexShaderConstantFn getVertexShaderConstant = nullptr;
};

struct DrawStateSnapshot
{
    void* sourceColor = nullptr;
    void* sourceDepth = nullptr;
    D3DSurfaceDescription colorDescription = {};
    D3DSurfaceDescription depthDescription = {};
    D3DViewport viewport = {};
    D3DMatrix world = {};
    D3DMatrix view = {};
    D3DMatrix projection = {};
    D3DMatrix leftView = {};
    D3DMatrix rightView = {};
    D3DMatrix leftProjection = {};
    D3DMatrix rightProjection = {};
    DWORD vertexShaderOrFvf = 0;
    std::uint64_t originalVertexShaderHash = 0;
    DWORD originalVertexShaderByteCount = 0;
    DWORD vertexShaderCreationOrdinal = 0;
    DWORD zEnable = 0;
    DWORD zWriteEnable = 0;
    DWORD alphaBlendEnable = 0;
    DWORD fogEnable = 0;
    DWORD lighting = 0;
    bfvr::stereo::D3D8DrawPolicy drawPolicy =
        bfvr::stereo::D3D8DrawPolicy::MonoNonPerspective;
    bfvr::stereo::D3D8SemanticDrawClass semanticClass =
        bfvr::stereo::D3D8SemanticDrawClass::Unclassified;
    bfvr::d3d8probe::D3D8SkinningShaderTransformState
        skinningShaderTransform = {};
    bfvr::d3d8probe::D3D8SpriteShaderTransformState
        spriteShaderTransform = {};
    bfvr::d3d8probe::D3D8TreeSpriteShaderTransformState
        treeSpriteShaderTransform = {};
};

bfvr::D3D8ObserverCallbacks g_callbacks = {};
bfvr::D3D8ObserverLifecycle g_lifecycle = {};
DeviceMethods g_methods = {};
StereoPairRecord g_record = {};
StereoFrameRecord g_frame = {};
bfvr::D3D8To9VertexShaderIdentityResolver
    g_vertexShaderIdentityResolver = {};
ProbeMode g_mode = ProbeMode::OneDraw;
ResetFn g_originalReset = nullptr;
PresentFn g_originalPresent = nullptr;
DrawPrimitiveFn g_originalDrawPrimitive = nullptr;
DrawIndexedPrimitiveFn g_originalDrawIndexedPrimitive = nullptr;
DrawPrimitiveUPFn g_originalDrawPrimitiveUP = nullptr;
DrawIndexedPrimitiveUPFn g_originalDrawIndexedPrimitiveUP = nullptr;
volatile LONG g_started = 0;
std::uintptr_t g_gameImageBegin = 0;
std::uintptr_t g_gameImageEnd = 0;
bfvr::D3D8SharedPresentationBridge g_presentationBridge;
bfvr::D3D8PresentationConfiguration g_presentationConfiguration = {};
bfvr::D3D8RenderViewPoseHook g_renderViewPoseHook;
bfvr::BFSoldierVrMotionFilter g_playerVrMotionFilter;
bfvr::d3d8probe::D3D8StereoReadbackApi g_readbackApi = {};
bfvr::D3D8RuntimeRenderRequest g_runtimeRenderRequest = {};
bfvr::D3D8RuntimeFramePosePolicy g_runtimeFramePosePolicy = {};
bool g_loggedImmutableLocalTrackingOrigin = false;
volatile LONG g_loggedWaterPassStateMask = 0;
volatile LONG g_processLifetimeShaderDrawSkips = 0;
bfvr::D3D8RuntimeUiPlacement g_frameUiPlacement = {};
bfvr::stereo::UiMenuAnchorTracker g_menuAnchorTracker = {};
bool g_nativeMenuActive = false;
D3DViewport g_runtimeWorldViewport = {};
bool g_presentationFramePublished = false;
bool g_offlinePresentation = false;
bool g_runUntilStopped = false;
bool g_keepOriginalFlatBackbuffer = false;
bool g_headCenteredWaterReflection = true;
bfvr::D3D8RuntimeDiagnosticLevel g_runtimeDiagnostics =
    bfvr::D3D8RuntimeDiagnosticLevel::Deep;
PresentationRunRecord g_presentationRun = {};
DWORD g_lastPresentationTimingReportAt = 0;
bool g_presentationTimingStarted = false;

struct WorldCrosshairFrameTransforms
{
    D3DMatrix eyeViews[2] = {};
    D3DMatrix eyeProjections[2] = {};
    bool valid = false;
};

WorldCrosshairFrameTransforms g_worldCrosshairFrame = {};

void ResetWorldCrosshairFrameTransforms() noexcept
{
    g_worldCrosshairFrame = {};
}
bool IsFullFrameMode()
{
    return g_mode != ProbeMode::OneDraw;
}

bool IsPresentationMode()
{
    return g_mode == ProbeMode::FullFramePresentation;
}

bool IsHeadCenteredWaterReflectionPass(const DrawStateSnapshot& snapshot)
{
    return g_headCenteredWaterReflection &&
        snapshot.semanticClass ==
            bfvr::stereo::D3D8SemanticDrawClass::WaterSurface &&
        snapshot.zWriteEnable != 0;
}

void AppendLog(const wchar_t* format, ...);

#include "client/internal/D3D8StereoPairRequestPose.inl"

void AppendLog(const wchar_t* format, ...)
{
    if (g_callbacks.appendLog == nullptr)
    {
        return;
    }

    wchar_t message[1400] = {};
    va_list arguments;
    va_start(arguments, format);
    _vsnwprintf_s(message, std::size(message), _TRUNCATE, format, arguments);
    va_end(arguments);
    g_callbacks.appendLog(message);
}

#include "client/internal/D3D8StereoPairWaterDiagnostics.inl"

void AppendPresentationLog(const wchar_t* message)
{
    AppendLog(L"%s", message);
}

void SignalCompletion()
{
    if (g_callbacks.signalCompletion != nullptr)
    {
        g_callbacks.signalCompletion();
    }
}

bool IsTrustedD3D8Target(const void* target)
{
    HMODULE module = nullptr;
    if (target == nullptr ||
        !GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(target),
            &module))
    {
        return false;
    }

    wchar_t modulePath[MAX_PATH] = {};
    if (GetModuleFileNameW(module, modulePath, static_cast<DWORD>(std::size(modulePath))) == 0)
    {
        return false;
    }

    const wchar_t* fileName = wcsrchr(modulePath, L'\\');
    fileName = fileName == nullptr ? modulePath : fileName + 1;
    return _wcsicmp(fileName, L"d3d8.dll") == 0 ||
        _wcsicmp(fileName, L"BFVRD3D8To9.dll") == 0;
}

bool InitializeGameImageRange()
{
    const HMODULE module = GetModuleHandleW(nullptr);
    if (module == nullptr)
    {
        return false;
    }

    const auto* const bytes = reinterpret_cast<const std::byte*>(module);
    const auto* const dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE || dosHeader->e_lfanew <= 0)
    {
        return false;
    }
    const auto* const ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        bytes + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE ||
        ntHeaders->OptionalHeader.SizeOfImage == 0)
    {
        return false;
    }

    g_gameImageBegin = reinterpret_cast<std::uintptr_t>(module);
    g_gameImageEnd =
        g_gameImageBegin + ntHeaders->OptionalHeader.SizeOfImage;
    return g_gameImageEnd > g_gameImageBegin;
}

#include "client/internal/D3D8StereoPairStackCapture.inl"

bool IsFiniteMatrix(const D3DMatrix& matrix)
{
    for (const auto& row : matrix.values)
    {
        for (float value : row)
        {
            if (!std::isfinite(value))
            {
                return false;
            }
        }
    }
    return true;
}

// The eye views differ only by the tracked eye offset.  Averaging them gives
// the tracked head-centre view without falling back to BF1942's flat source
// camera.  That keeps the fixed-function reflection vector responsive to HMD
// motion while removing the IPD disagreement that caused alternating bands.
bool BuildHeadCenteredWaterReflectionView(
    const D3DMatrix& leftEyeView,
    const D3DMatrix& rightEyeView,
    D3DMatrix& headCenteredView)
{
    for (std::size_t row = 0; row < std::size(headCenteredView.values); ++row)
    {
        for (std::size_t column = 0;
             column < std::size(headCenteredView.values[row]);
             ++column)
        {
            headCenteredView.values[row][column] =
                (leftEyeView.values[row][column] +
                 rightEyeView.values[row][column]) *
                0.5F;
        }
    }
    return IsFiniteMatrix(headCenteredView);
}

bool EqualMatrix(const D3DMatrix& left, const D3DMatrix& right)
{
    return std::memcmp(&left, &right, sizeof(D3DMatrix)) == 0;
}

std::uint64_t HashMatrix(const D3DMatrix& matrix)
{
    std::uint64_t hash = 1469598103934665603ULL;
    const auto* bytes = reinterpret_cast<const unsigned char*>(&matrix);
    for (std::size_t index = 0; index < sizeof(matrix); ++index)
    {
        hash = (hash ^ bytes[index]) * 1099511628211ULL;
    }
    return hash;
}

bool EqualViewport(const D3DViewport& left, const D3DViewport& right)
{
    return std::memcmp(&left, &right, sizeof(D3DViewport)) == 0;
}

ULONG ReleaseUnknown(void*& object)
{
    if (object == nullptr)
    {
        return static_cast<ULONG>(-1);
    }

    ULONG result = static_cast<ULONG>(-1);
    auto** const vtable = *reinterpret_cast<void***>(object);
    const auto release = vtable == nullptr
        ? nullptr
        : reinterpret_cast<ReleaseUnknownFn>(vtable[kIUnknownReleaseSlot]);
    if (release != nullptr && IsTrustedD3D8Target(reinterpret_cast<void*>(release)))
    {
        result = release(object);
    }
    object = nullptr;
    return result;
}

bool GetSurfaceDescription(void* surface, D3DSurfaceDescription& description)
{
    description = {};
    if (surface == nullptr)
    {
        return false;
    }

    auto** const vtable = *reinterpret_cast<void***>(surface);
    const auto getDescription = vtable == nullptr
        ? nullptr
        : reinterpret_cast<GetSurfaceDescriptionFn>(vtable[kDirect3DSurface8GetDescSlot]);
    return getDescription != nullptr &&
        IsTrustedD3D8Target(reinterpret_cast<void*>(getDescription)) &&
        SUCCEEDED(getDescription(surface, &description));
}

bool HasStencil(UINT depthFormat)
{
    return depthFormat == 73 || depthFormat == 75 || depthFormat == 79;
}

bool BuildFramePolicyTransforms(DrawStateSnapshot& snapshot)
{
    bfvr::stereo::Matrix4 projection = {};
    std::memcpy(&projection, &snapshot.projection, sizeof(projection));
    snapshot.drawPolicy = bfvr::stereo::ClassifyD3D8DrawPolicy(
        snapshot.vertexShaderOrFvf,
        projection);
    if (bfvr::stereo::UsesStereoTransforms(snapshot.drawPolicy))
    {
        if (IsPresentationMode())
        {
            return bfvr::BuildD3D8RuntimeStereoTransforms(
                g_runtimeRenderRequest,
                g_runtimeFramePosePolicy.EyeReference(
                    g_renderViewPoseHook.WasApplied(
                        g_runtimeRenderRequest.sequence)),
                snapshot.view,
                snapshot.projection,
                snapshot.leftView,
                snapshot.rightView,
                snapshot.leftProjection,
                snapshot.rightProjection);
        }
        return bfvr::BuildD3D8DiagnosticStereoTransforms(
            snapshot.view,
            snapshot.projection,
            kDiagnosticHalfEyeOffset,
            kDiagnosticConvergenceDistance,
            snapshot.leftView,
            snapshot.rightView,
            snapshot.leftProjection,
            snapshot.rightProjection);
    }

    snapshot.leftView = snapshot.view;
    snapshot.rightView = snapshot.view;
    snapshot.leftProjection = snapshot.projection;
    snapshot.rightProjection = snapshot.projection;
    return true;
}

#include "client/internal/D3D8StereoPairShaderPolicy.inl"

#include "client/internal/D3D8StereoPairProvenance.inl"

bool VerifyRestoredState(
    void* device,
    void* expectedColor,
    void* expectedDepth,
    const D3DViewport& expectedViewport,
    const D3DMatrix& expectedView,
    const D3DMatrix& expectedProjection)
{
    void* actualColor = nullptr;
    void* actualDepth = nullptr;
    D3DViewport actualViewport = {};
    D3DMatrix actualView = {};
    D3DMatrix actualProjection = {};

    const HRESULT colorResult = g_methods.getRenderTarget(device, &actualColor);
    const HRESULT depthResult = g_methods.getDepthStencilSurface(device, &actualDepth);
    const HRESULT viewportResult = g_methods.getViewport(device, &actualViewport);
    const HRESULT viewResult = g_methods.getTransform(device, kD3DTransformView, &actualView);
    const HRESULT projectionResult =
        g_methods.getTransform(device, kD3DTransformProjection, &actualProjection);

    g_record.targetRestored = SUCCEEDED(colorResult) && actualColor == expectedColor;
    g_record.depthRestored = SUCCEEDED(depthResult) && actualDepth == expectedDepth;
    g_record.viewportRestored =
        SUCCEEDED(viewportResult) && EqualViewport(actualViewport, expectedViewport);
    g_record.viewRestored = SUCCEEDED(viewResult) && EqualMatrix(actualView, expectedView);
    g_record.projectionRestored =
        SUCCEEDED(projectionResult) && EqualMatrix(actualProjection, expectedProjection);

    ReleaseUnknown(actualColor);
    ReleaseUnknown(actualDepth);
    return g_record.targetRestored &&
        g_record.depthRestored &&
        g_record.viewportRestored &&
        g_record.viewRestored &&
        g_record.projectionRestored;
}

void ReleaseFrameSourceReferences(DrawStateSnapshot& snapshot)
{
    if (snapshot.sourceColor != nullptr)
    {
        const ULONG colorRelease = ReleaseUnknown(snapshot.sourceColor);
        InterlockedIncrement(&g_frame.sourceReleaseChecks);
        if (colorRelease == 0 || colorRelease == static_cast<ULONG>(-1))
        {
            InterlockedIncrement(&g_frame.sourceReleaseFailures);
        }
    }
    if (snapshot.sourceDepth != nullptr)
    {
        const ULONG depthRelease = ReleaseUnknown(snapshot.sourceDepth);
        InterlockedIncrement(&g_frame.sourceReleaseChecks);
        if (depthRelease == 0 || depthRelease == static_cast<ULONG>(-1))
        {
            InterlockedIncrement(&g_frame.sourceReleaseFailures);
        }
    }
}

bool AcquireFrameDrawState(void* device, DrawStateSnapshot& snapshot, bool& eligibleTarget)
{
    snapshot = {};
    eligibleTarget = false;
    if (FAILED(g_methods.getRenderTarget(device, &snapshot.sourceColor)) ||
        FAILED(g_methods.getDepthStencilSurface(device, &snapshot.sourceDepth)) ||
        snapshot.sourceColor == nullptr ||
        snapshot.sourceDepth == nullptr ||
        !GetSurfaceDescription(snapshot.sourceColor, snapshot.colorDescription) ||
        !GetSurfaceDescription(snapshot.sourceDepth, snapshot.depthDescription))
    {
        return false;
    }

    eligibleTarget =
        g_lifecycle.presentationReadable &&
        snapshot.colorDescription.width == g_lifecycle.backBufferWidth &&
        snapshot.colorDescription.height == g_lifecycle.backBufferHeight &&
        snapshot.colorDescription.format == kD3DFormatX8R8G8B8 &&
        snapshot.colorDescription.multiSampleType == 0 &&
        snapshot.depthDescription.width == snapshot.colorDescription.width &&
        snapshot.depthDescription.height == snapshot.colorDescription.height &&
        snapshot.depthDescription.multiSampleType == snapshot.colorDescription.multiSampleType;
    if (!eligibleTarget)
    {
        return true;
    }

    if (FAILED(g_methods.getViewport(device, &snapshot.viewport)) ||
        FAILED(g_methods.getTransform(device, kD3DTransformWorld, &snapshot.world)) ||
        FAILED(g_methods.getTransform(device, kD3DTransformView, &snapshot.view)) ||
        FAILED(g_methods.getTransform(
            device,
            kD3DTransformProjection,
            &snapshot.projection)))
    {
        eligibleTarget = false;
        return false;
    }
    if (FAILED(g_methods.getVertexShader(device, &snapshot.vertexShaderOrFvf)))
    {
        InterlockedIncrement(&g_frame.vertexShaderReadFailures);
        eligibleTarget = false;
        return false;
    }
    BFVRD3D8To9VertexShaderIdentity translatedIdentity = {};
    if (g_vertexShaderIdentityResolver.TryGet(
            device,
            snapshot.vertexShaderOrFvf,
            translatedIdentity))
    {
        snapshot.originalVertexShaderHash =
            translatedIdentity.originalFunctionHash;
        snapshot.originalVertexShaderByteCount =
            translatedIdentity.originalFunctionByteCount;
        snapshot.vertexShaderCreationOrdinal =
            translatedIdentity.creationOrdinal;
    }
    ReadProvenanceRenderStates(device, snapshot);
    if (!IsFiniteMatrix(snapshot.world) ||
        !IsFiniteMatrix(snapshot.view) ||
        !IsFiniteMatrix(snapshot.projection) ||
        !BuildFramePolicyTransforms(snapshot))
    {
        eligibleTarget = false;
        return false;
    }
    return true;
}

#include "client/internal/D3D8StereoPairRestore.inl"

#include "client/internal/D3D8StereoPairFrameResources.inl"

HRESULT InvokeOriginalFrameDraw(
    void* device,
    const FrameDrawInvocation& invocation)
{
    switch (invocation.kind)
    {
    case FrameDrawKind::Primitive:
        return g_originalDrawPrimitive == nullptr
            ? E_FAIL
            : g_originalDrawPrimitive(
                device,
                invocation.primitiveType,
                invocation.startVertex,
                invocation.primitiveCount);
    case FrameDrawKind::IndexedPrimitive:
        return g_originalDrawIndexedPrimitive == nullptr
            ? E_FAIL
            : g_originalDrawIndexedPrimitive(
                device,
                invocation.primitiveType,
                invocation.minimumVertexIndex,
                invocation.vertexCount,
                invocation.startIndex,
                invocation.primitiveCount);
    case FrameDrawKind::PrimitiveUP:
        return g_originalDrawPrimitiveUP == nullptr
            ? E_FAIL
            : g_originalDrawPrimitiveUP(
                device,
                invocation.primitiveType,
                invocation.primitiveCount,
                invocation.vertexData,
                invocation.vertexStride);
    case FrameDrawKind::IndexedPrimitiveUP:
        return g_originalDrawIndexedPrimitiveUP == nullptr
            ? E_FAIL
            : g_originalDrawIndexedPrimitiveUP(
                device,
                invocation.primitiveType,
                invocation.minimumVertexIndex,
                invocation.vertexCount,
                invocation.primitiveCount,
                invocation.indexData,
                invocation.indexFormat,
                invocation.vertexData,
                invocation.vertexStride);
    default:
        return E_INVALIDARG;
    }
}

FrameMirrorResult MirrorDrawIntoFrame(
    void* device,
    const FrameDrawInvocation& invocation)
{
    if (InterlockedCompareExchange(&g_frame.mirroredDraws, 0, 0) >=
        kMaximumFrameDraws)
    {
        InterlockedIncrement(&g_frame.boundedDrawSkips);
        return FrameMirrorResult::NotMirrored;
    }

    bfvr::d3d8probe::ScopedPerformanceAccumulator preparationTimer(
        g_frame.preparationQpcTicks);
    DrawStateSnapshot snapshot = {};
    bool eligibleTarget = false;
    const bool readable = AcquireFrameDrawState(device, snapshot, eligibleTarget);
    if (!readable || !eligibleTarget)
    {
        InterlockedIncrement(&g_frame.excludedTargetDraws);
        InterlockedIncrement(
            &g_frame.excludedByKind[static_cast<std::size_t>(invocation.kind)]);
        ReleaseFrameSourceReferences(snapshot);
        return FrameMirrorResult::NotMirrored;
    }
    ApplyFrameSemanticPolicy(invocation, snapshot);
    if (snapshot.semanticClass ==
        bfvr::stereo::D3D8SemanticDrawClass::WaterSurface)
    {
        LogWaterPassState(device, snapshot);
    }
    const bool firstPersonArmDraw =
        bfvr::stereo::IsBF1942FirstPersonArmDraw(
            snapshot.semanticClass,
            snapshot.drawPolicy ==
                bfvr::stereo::D3D8DrawPolicy::StereoPerspective,
            snapshot.projection.values[0][0],
            snapshot.projection.values[1][1]);
    if (bfvr::stereo::ShouldSuppressBF1942FirstPersonArmDraw(
            IsPresentationMode(),
            firstPersonArmDraw,
            g_presentationConfiguration.nativeFirstPersonArmsEnabled))
    {
        InterlockedIncrement(&g_frame.suppressedFirstPersonArmDraws);
        ReleaseFrameSourceReferences(snapshot);
        return FrameMirrorResult::IntentionallySuppressed;
    }
    if (!PrepareFrameSkinningShaderTransforms(device, snapshot) ||
        !PrepareFrameSpriteShaderTransforms(device, snapshot) ||
        !PrepareFrameTreeSpriteShaderTransforms(device, snapshot))
    {
        ReleaseFrameSourceReferences(snapshot);
        if (IsPresentationMode() && g_runUntilStopped)
        {
            const LONG skippedDraws =
                InterlockedIncrement(&g_processLifetimeShaderDrawSkips);
            if (skippedDraws <= 3)
            {
                AppendLog(
                    L"D3D8 process-lifetime presentation skipped one draw whose programmable-shader source constants could not be safely transformed (processSkips=%ld); the native draw and OpenXR session remain active.",
                    skippedDraws);
            }
            // Preparation is read-only. A source-constant mismatch on one
            // animated mesh or sprite must omit only that draw from the VR
            // replay; the hook wrapper forwards its native flat draw. Treating
            // this as a terminal frame failure previously ended OpenXR at the
            // first observed spawned animated-mesh mismatch.
            return FrameMirrorResult::NotMirrored;
        }
        return FrameMirrorResult::Failed;
    }

    if (!CreateAndClearFrameResources(device, snapshot))
    {
        ReleaseFrameSourceReferences(snapshot);
        return FrameMirrorResult::Failed;
    }
    preparationTimer.Stop();

    bfvr::d3d8probe::ScopedPerformanceAccumulator drawTimer(
        g_frame.eyeOrLayerDrawQpcTicks);
    const D3DMatrix* const eyeViews[2] = {&snapshot.leftView, &snapshot.rightView};
    const D3DMatrix* const eyeProjections[2] = {
        &snapshot.leftProjection,
        &snapshot.rightProjection};
    D3DMatrix headCenteredWaterReflectionView = {};
    const bool headCenteredWaterReflection =
        IsHeadCenteredWaterReflectionPass(snapshot) &&
        BuildHeadCenteredWaterReflectionView(
            snapshot.leftView,
            snapshot.rightView,
            headCenteredWaterReflectionView);
    const auto compositionLayer =
        bfvr::stereo::SelectD3D8FrameCompositionLayer(snapshot.semanticClass);
    if (compositionLayer ==
            bfvr::stereo::D3D8FrameCompositionLayer::WorldEyes &&
        snapshot.drawPolicy ==
            bfvr::stereo::D3D8DrawPolicy::StereoPerspective &&
        !firstPersonArmDraw &&
        snapshot.semanticClass !=
            bfvr::stereo::D3D8SemanticDrawClass::WaterSurface)
    {
        g_worldCrosshairFrame.eyeViews[0] = snapshot.leftView;
        g_worldCrosshairFrame.eyeViews[1] = snapshot.rightView;
        g_worldCrosshairFrame.eyeProjections[0] = snapshot.leftProjection;
        g_worldCrosshairFrame.eyeProjections[1] = snapshot.rightProjection;
        g_worldCrosshairFrame.valid = true;
    }
    HRESULT eyeDrawResults[2] = {E_FAIL, E_FAIL};
    HRESULT menuDrawResult = E_FAIL;
    bool layerDrawn = true;
    if (compositionLayer == bfvr::stereo::D3D8FrameCompositionLayer::Ref2Ui)
    {
        const HRESULT targetResult = g_methods.setRenderTarget(
            device,
            g_frame.menuColor,
            g_frame.menuDepth);
        const HRESULT viewportResult = SUCCEEDED(targetResult)
            ? g_methods.setViewport(device, &snapshot.viewport)
            : E_FAIL;
        const HRESULT viewResult = SUCCEEDED(viewportResult)
            ? g_methods.setTransform(device, kD3DTransformView, &snapshot.view)
            : E_FAIL;
        const HRESULT projectionResult = SUCCEEDED(viewResult)
            ? g_methods.setTransform(
                device,
                kD3DTransformProjection,
                &snapshot.projection)
            : E_FAIL;
        menuDrawResult = SUCCEEDED(projectionResult)
            ? InvokeOriginalFrameDraw(device, invocation)
            : E_FAIL;
        layerDrawn = SUCCEEDED(menuDrawResult);
    }
    else
    {
        for (std::size_t eye = 0; eye < 2; ++eye)
        {
            const D3DMatrix* const replayView = headCenteredWaterReflection
                ? &headCenteredWaterReflectionView
                : eyeViews[eye];
            const D3DMatrix* const replayProjection = eyeProjections[eye];
            const HRESULT targetResult = g_methods.setRenderTarget(
                device,
                g_frame.ownedColor[eye],
                g_frame.ownedDepth[eye]);
            const HRESULT viewportResult = SUCCEEDED(targetResult)
                ? g_methods.setViewport(
                    device,
                    IsPresentationMode()
                        ? &g_runtimeWorldViewport
                        : &snapshot.viewport)
                : E_FAIL;
            const HRESULT viewResult = SUCCEEDED(viewportResult)
            ? g_methods.setTransform(
                device,
                kD3DTransformView,
                replayView)
                : E_FAIL;
            const HRESULT projectionResult = SUCCEEDED(viewResult)
                ? g_methods.setTransform(
                    device,
                    kD3DTransformProjection,
                    replayProjection)
                : E_FAIL;
            HRESULT weaponWorldResult = projectionResult;
            if (SUCCEEDED(weaponWorldResult) && invocation.replayWeaponMotion)
            {
                bfvr::D3D8WeaponMotionMatrix replayWorld = {};
                bfvr::D3D8WeaponMotionMatrix worldSpaceAttachment = {};
                std::memcpy(&replayWorld, &snapshot.world, sizeof(replayWorld));
                std::memcpy(
                    &worldSpaceAttachment,
                    &invocation.weaponMotionWorldAttachment,
                    sizeof(worldSpaceAttachment));
                weaponWorldResult =
                    bfvr::BuildD3D8WeaponMotionReplayWorld(
                        replayWorld,
                        worldSpaceAttachment,
                        replayWorld)
                    ? g_methods.setTransform(
                        device,
                        kD3DTransformWorld,
                        &replayWorld)
                    : E_FAIL;
            }
            const bfvr::d3d8probe::D3D8VertexShaderConstantApi shaderApi = {
                g_methods.setVertexShaderConstant,
                g_methods.getVertexShaderConstant};
            const HRESULT shaderResult =
                SUCCEEDED(weaponWorldResult) &&
                snapshot.skinningShaderTransform.prepared
                ? bfvr::d3d8probe::ApplyD3D8SkinningShaderEye(
                    shaderApi,
                    device,
                    snapshot.skinningShaderTransform,
                    eye)
                : weaponWorldResult;
            const HRESULT spriteShaderResult =
                SUCCEEDED(shaderResult) &&
                snapshot.spriteShaderTransform.prepared
                ? bfvr::d3d8probe::ApplyD3D8SpriteShaderEye(
                    shaderApi,
                    device,
                    snapshot.spriteShaderTransform,
                    eye)
                : shaderResult;
            const HRESULT treeSpriteShaderResult =
                SUCCEEDED(spriteShaderResult) &&
                snapshot.treeSpriteShaderTransform.prepared
                ? bfvr::d3d8probe::ApplyD3D8TreeSpriteShaderEye(
                    shaderApi,
                    device,
                    snapshot.treeSpriteShaderTransform,
                    eye)
                : spriteShaderResult;
            if (FAILED(shaderResult) &&
                snapshot.skinningShaderTransform.prepared)
            {
                InterlockedIncrement(&g_frame.skinningShaderApplyFailures);
            }
            if (FAILED(spriteShaderResult) &&
                snapshot.spriteShaderTransform.prepared)
            {
                InterlockedIncrement(&g_frame.spriteShaderApplyFailures);
            }
            if (FAILED(treeSpriteShaderResult) &&
                snapshot.treeSpriteShaderTransform.prepared)
            {
                InterlockedIncrement(&g_frame.treeSpriteShaderApplyFailures);
            }
            eyeDrawResults[eye] = SUCCEEDED(treeSpriteShaderResult)
                ? InvokeOriginalFrameDraw(device, invocation)
                : E_FAIL;
            if (FAILED(eyeDrawResults[eye]))
            {
                layerDrawn = false;
                break;
            }
        }
    }

    drawTimer.Stop();
    const bool frameStateRestored = RestoreFrameState(device, snapshot);
    const bool restored = frameStateRestored;
    ReleaseFrameSourceReferences(snapshot);
    g_frame.lastLeftDrawResult = eyeDrawResults[0];
    g_frame.lastRightDrawResult = eyeDrawResults[1];
    g_frame.lastMenuDrawResult = menuDrawResult;
    if (!layerDrawn || !restored)
    {
        ReleaseFrameOwnedResources();
        return FrameMirrorResult::Failed;
    }

    InterlockedIncrement(&g_frame.mirroredDraws);
    InterlockedIncrement(
        &g_frame.mirroredByKind[static_cast<std::size_t>(invocation.kind)]);
    if (compositionLayer == bfvr::stereo::D3D8FrameCompositionLayer::Ref2Ui)
    {
        InterlockedIncrement(&g_frame.menuLayerDraws);
    }
    else
    {
        InterlockedIncrement(&g_frame.worldEyeDraws);
    }
    bfvr::d3d8probe::CountStereoFrameDrawPolicy(g_frame, snapshot.drawPolicy);
    bfvr::d3d8probe::CountStereoFrameSemanticDraw(
        g_frame,
        snapshot.semanticClass);
    if (headCenteredWaterReflection)
    {
        InterlockedIncrement(&g_frame.headCenteredWaterReflectionDraws);
    }
    if (bfvr::IsDeepD3D8RuntimeDiagnostics(g_runtimeDiagnostics))
    {
        bfvr::d3d8probe::ScopedPerformanceAccumulator provenanceTimer(
            g_frame.provenanceQpcTicks);
        RecordDrawProvenance(invocation, snapshot);
    }
    InterlockedExchangeAdd(
        &g_frame.mirroredPrimitives,
        static_cast<LONG>(std::min<UINT>(
            invocation.primitiveCount,
            static_cast<UINT>(LONG_MAX))));
    return FrameMirrorResult::Mirrored;
}

#include "client/internal/D3D8StereoPairFinalization.inl"

bool CompletePresentationFrame(void* device)
{
    const bool completed = FinalizeFrameTargets(device);
    const LONG sequence = g_runtimeRenderRequest.sequence;
    const std::int64_t consumptionWaitStarted =
        bfvr::d3d8probe::ReadPerformanceCounter();
    const bool consumed =
        completed &&
        g_presentationBridge.WaitForConsumption(
            sequence,
            g_runUntilStopped
                ? kContinuousConsumptionTimeoutMs
                : 5000);
    g_presentationRun.totalConsumptionWaitQpcTicks +=
        bfvr::d3d8probe::ReadPerformanceCounter() - consumptionWaitStarted;
    if (!completed || !consumed)
    {
        AppendLog(
            L"D3D8 continuous presentation frame %ld failed: frameCompleted=%d sourceConsumed=%d mirroredDraws=%ld worldEyeDraws=%ld uiLayerDraws=%ld framePublished=%d.",
            sequence,
            completed ? 1 : 0,
            consumed ? 1 : 0,
            InterlockedCompareExchange(
                &g_frame.mirroredDraws,
                0,
                0),
            InterlockedCompareExchange(
                &g_frame.worldEyeDraws,
                0,
                0),
            InterlockedCompareExchange(
                &g_frame.menuLayerDraws,
                0,
                0),
            g_presentationFramePublished ? 1 : 0);
        InterlockedIncrement(&g_presentationRun.failedFrames);
        if (g_runUntilStopped)
        {
            // A match/server transition may invalidate a source frame after
            // the runtime request has already been accepted.  That is not a
            // process-lifetime presentation failure. GPU transport surfaces
            // are process-lifetime resources: releasing them also asks the
            // bridge to stop its companion. Preserve them and let the next
            // native Present request a fresh OpenXR frame. The x64 presenter
            // supersedes the unanswered sequence while continuing to submit
            // its last valid image, so the headset session never needs to end.
            bfvr::d3d8probe::ResetStereoFrameRecordForResourceReuse(g_frame);
            ResetWorldCrosshairFrameTransforms();
            g_presentationFramePublished = false;
            g_runtimeRenderRequest = {};
            g_runtimeFramePosePolicy = {};
            g_renderViewPoseHook.ClearPose();
            InterlockedExchange(&g_record.state, 1);
            AppendLog(
                L"D3D8 process-lifetime presentation recovered from transition frame %ld; retaining the OpenXR session and awaiting the next native Present.",
                sequence);
            return false;
        }
        ReleaseFrameOwnedResources();
        InterlockedExchange(&g_record.state, 5);
        return false;
    }

    bfvr::d3d8probe::AccumulateContinuousPresentationFrame(
        g_presentationRun,
        g_frame,
        sequence);
    if (g_runUntilStopped &&
        bfvr::d3d8probe::IsContinuousPresentationTimingReportDue(
            GetTickCount(),
            g_lastPresentationTimingReportAt))
    {
        bfvr::d3d8probe::ReportContinuousPresentationResult(
            AppendLog,
            g_presentationRun,
            g_presentationBridge.LeftWorldWidth(),
            g_presentationBridge.LeftWorldHeight());
    }

    if (!g_runUntilStopped &&
        GetTickCount() - g_presentationRun.startedAt >=
            kPresentationDurationMs)
    {
        ReleaseFrameOwnedResources();
        InterlockedExchange(&g_record.state, 4);
        return true;
    }

    bfvr::d3d8probe::ResetStereoFrameRecordForResourceReuse(g_frame);
    ResetWorldCrosshairFrameTransforms();
    g_presentationFramePublished = false;
    const std::int64_t requestWaitStarted =
        bfvr::d3d8probe::ReadPerformanceCounter();
    const bool nextReady = g_presentationBridge.RequestRender(
        g_runtimeRenderRequest,
        g_runUntilStopped
            ? kContinuousRenderRequestTimeoutMs
            : kBoundedRenderRequestTimeoutMs);
    g_presentationRun.totalRequestWaitQpcTicks +=
        bfvr::d3d8probe::ReadPerformanceCounter() - requestWaitStarted;
    if (nextReady)
    {
        PrepareRuntimeRenderRequestPose();
    }
    else if (!g_runUntilStopped)
    {
        ReleaseFrameOwnedResources();
    }
    // In the explicit continuous mode, a missing request can represent a
    // temporary OpenXR shouldRender/focus pause.  Leave the request pending
    // and resume from the next native Present rather than stopping the owned
    // x64 presenter and ending the headset session.
    InterlockedExchange(&g_record.state, nextReady ? 2 : (g_runUntilStopped ? 1 : 5));
    return nextReady;
}

AttemptResult TryReplayStereoPair(
    void* device,
    DWORD primitiveType,
    UINT minimumVertexIndex,
    UINT vertexCount,
    UINT startIndex,
    UINT primitiveCount)
{
    bfvr::d3d8probe::ResetStereoPairAttemptRecord(g_record);
    void* sourceColor = nullptr;
    void* sourceDepth = nullptr;
    void* ownedColor[2] = {};
    void* ownedDepth[2] = {};
    bool stateMutated = false;
    bool setupComplete = false;

    const HRESULT sourceColorResult = g_methods.getRenderTarget(device, &sourceColor);
    const HRESULT sourceDepthResult = g_methods.getDepthStencilSurface(device, &sourceDepth);
    if (FAILED(sourceColorResult) ||
        FAILED(sourceDepthResult) ||
        sourceColor == nullptr ||
        sourceDepth == nullptr ||
        !GetSurfaceDescription(sourceColor, g_record.colorDescription) ||
        !GetSurfaceDescription(sourceDepth, g_record.depthDescription))
    {
        g_record.sourceColorRelease = ReleaseUnknown(sourceColor);
        g_record.sourceDepthRelease = ReleaseUnknown(sourceDepth);
        return AttemptResult::NotEligible;
    }

    const bool fullSizeCandidate =
        g_lifecycle.presentationReadable &&
        g_record.colorDescription.width == g_lifecycle.backBufferWidth &&
        g_record.colorDescription.height == g_lifecycle.backBufferHeight &&
        g_record.depthDescription.width == g_record.colorDescription.width &&
        g_record.depthDescription.height == g_record.colorDescription.height &&
        g_record.depthDescription.multiSampleType == g_record.colorDescription.multiSampleType &&
        g_record.colorDescription.format == kD3DFormatX8R8G8B8 &&
        g_record.colorDescription.multiSampleType == 0;
    if (!fullSizeCandidate)
    {
        g_record.sourceColorRelease = ReleaseUnknown(sourceColor);
        g_record.sourceDepthRelease = ReleaseUnknown(sourceDepth);
        return AttemptResult::NotEligible;
    }

    const HRESULT viewportResult = g_methods.getViewport(device, &g_record.originalViewport);
    const HRESULT viewResult =
        g_methods.getTransform(device, kD3DTransformView, &g_record.originalView);
    const HRESULT projectionResult =
        g_methods.getTransform(device, kD3DTransformProjection, &g_record.originalProjection);
    if (FAILED(viewportResult) ||
        FAILED(viewResult) ||
        FAILED(projectionResult) ||
        !IsFiniteMatrix(g_record.originalView) ||
        !IsFiniteMatrix(g_record.originalProjection) ||
        !bfvr::BuildD3D8DiagnosticStereoTransforms(
            g_record.originalView,
            g_record.originalProjection,
            kDiagnosticHalfEyeOffset,
            kDiagnosticConvergenceDistance,
            g_record.leftView,
            g_record.rightView,
            g_record.leftProjection,
            g_record.rightProjection))
    {
        g_record.sourceColorRelease = ReleaseUnknown(sourceColor);
        g_record.sourceDepthRelease = ReleaseUnknown(sourceDepth);
        return AttemptResult::NotEligible;
    }

    g_record.primitiveType = primitiveType;
    g_record.minimumVertexIndex = minimumVertexIndex;
    g_record.vertexCount = vertexCount;
    g_record.startIndex = startIndex;
    g_record.primitiveCount = primitiveCount;

    do
    {
        for (std::size_t eye = 0; eye < 2; ++eye)
        {
            const HRESULT colorResult = g_methods.createRenderTarget(
                device,
                g_record.colorDescription.width,
                g_record.colorDescription.height,
                g_record.colorDescription.format,
                g_record.colorDescription.multiSampleType,
                FALSE,
                &ownedColor[eye]);
            const HRESULT depthResult = g_methods.createDepthStencilSurface(
                device,
                g_record.depthDescription.width,
                g_record.depthDescription.height,
                g_record.depthDescription.format,
                g_record.depthDescription.multiSampleType,
                &ownedDepth[eye]);
            if (FAILED(colorResult) ||
                FAILED(depthResult) ||
                ownedColor[eye] == nullptr ||
                ownedDepth[eye] == nullptr)
            {
                break;
            }
            if (eye == 1)
            {
                setupComplete = true;
            }
        }
        if (!setupComplete)
        {
            break;
        }

        const D3DMatrix* const eyeViews[2] = {&g_record.leftView, &g_record.rightView};
        const D3DMatrix* const eyeProjections[2] = {
            &g_record.leftProjection,
            &g_record.rightProjection};
        HRESULT* const eyeDrawResults[2] = {
            &g_record.leftDrawResult,
            &g_record.rightDrawResult};
        const DWORD clearFlags = kD3DClearTarget |
            kD3DClearZBuffer |
            (HasStencil(g_record.depthDescription.format) ? kD3DClearStencil : 0);

        bool bothEyesDrawn = true;
        for (std::size_t eye = 0; eye < 2; ++eye)
        {
            const HRESULT targetResult =
                g_methods.setRenderTarget(device, ownedColor[eye], ownedDepth[eye]);
            stateMutated = stateMutated || SUCCEEDED(targetResult);
            const HRESULT viewportSetResult =
                SUCCEEDED(targetResult)
                ? g_methods.setViewport(device, &g_record.originalViewport)
                : E_FAIL;
            const HRESULT clearResult =
                SUCCEEDED(viewportSetResult)
                ? g_methods.clear(
                    device,
                    0,
                    nullptr,
                    clearFlags,
                    kOwnedTargetClearColor,
                    1.0F,
                    0)
                : E_FAIL;
            const HRESULT viewSetResult =
                SUCCEEDED(clearResult)
                ? g_methods.setTransform(device, kD3DTransformView, eyeViews[eye])
                : E_FAIL;
            const HRESULT projectionSetResult =
                SUCCEEDED(viewSetResult)
                ? g_methods.setTransform(
                    device,
                    kD3DTransformProjection,
                    eyeProjections[eye])
                : E_FAIL;
            *eyeDrawResults[eye] =
                SUCCEEDED(projectionSetResult)
                ? g_originalDrawIndexedPrimitive(
                    device,
                    primitiveType,
                    minimumVertexIndex,
                    vertexCount,
                    startIndex,
                    primitiveCount)
                : E_FAIL;
            if (FAILED(*eyeDrawResults[eye]))
            {
                bothEyesDrawn = false;
                break;
            }
        }
        g_record.pairDrawn = bothEyesDrawn;
    } while (false);

    if (stateMutated)
    {
        g_record.restoreTargetResult =
            g_methods.setRenderTarget(device, sourceColor, sourceDepth);
        g_record.restoreViewportResult =
            g_methods.setViewport(device, &g_record.originalViewport);
        g_record.restoreViewResult =
            g_methods.setTransform(device, kD3DTransformView, &g_record.originalView);
        g_record.restoreProjectionResult =
            g_methods.setTransform(
                device,
                kD3DTransformProjection,
                &g_record.originalProjection);
        g_record.allStateRestored = VerifyRestoredState(
            device,
            sourceColor,
            sourceDepth,
            g_record.originalViewport,
            g_record.originalView,
            g_record.originalProjection);
    }

    if (g_record.pairDrawn && g_record.allStateRestored)
    {
        g_record.readback[0] =
            bfvr::d3d8probe::ReadbackOwnedTarget(
                g_readbackApi,
                device,
                ownedColor[0],
                g_record.colorDescription,
                kOwnedTargetClearColor,
                nullptr,
                true);
        g_record.readback[1] =
            bfvr::d3d8probe::ReadbackOwnedTarget(
                g_readbackApi,
                device,
                ownedColor[1],
                g_record.colorDescription,
                kOwnedTargetClearColor,
                nullptr,
                true);
    }

    for (std::size_t eye = 0; eye < 2; ++eye)
    {
        g_record.ownedDepthRelease[eye] = ReleaseUnknown(ownedDepth[eye]);
        g_record.ownedColorRelease[eye] = ReleaseUnknown(ownedColor[eye]);
    }
    g_record.sourceDepthRelease = ReleaseUnknown(sourceDepth);
    g_record.sourceColorRelease = ReleaseUnknown(sourceColor);

    if (!g_record.pairDrawn || !g_record.allStateRestored)
    {
        return AttemptResult::Failed;
    }

    const bool bothEyesHaveColor =
        g_record.readback[0].nonClearPixels != 0 &&
        g_record.readback[1].nonClearPixels != 0;
    const bool imagesDiffer =
        g_record.readback[0].hash != g_record.readback[1].hash;
    return bothEyesHaveColor && imagesDiffer
        ? AttemptResult::Completed
        : AttemptResult::EmptyOrIdentical;
}

#include "client/internal/D3D8StereoPairReset.inl"

HRESULT WINAPI HookPresent(
    void* device,
    const RECT* sourceRectangle,
    const RECT* destinationRectangle,
    HWND destinationWindowOverride,
    const RGNDATA* dirtyRegion)
{
    InterlockedIncrement(&g_record.activeCallbacks);
    const LONG stateAtEntry =
        InterlockedCompareExchange(&g_record.state, 0, 0);
    const std::int64_t originalPresentStarted =
        bfvr::d3d8probe::ReadPerformanceCounter();
    const HRESULT result = g_originalPresent == nullptr
        ? E_FAIL
        : g_originalPresent(
            device,
            sourceRectangle,
            destinationRectangle,
            destinationWindowOverride,
            dirtyRegion);
    if (IsPresentationMode())
    {
        g_presentationRun.totalOriginalPresentQpcTicks +=
            bfvr::d3d8probe::ReadPerformanceCounter() -
            originalPresentStarted;
        InterlockedIncrement(&g_presentationRun.originalPresentCalls);
    }

    if (IsFullFrameMode() &&
        stateAtEntry == 2 &&
        SUCCEEDED(result) &&
        device == g_record.device &&
        GetCurrentThreadId() == g_record.deviceThreadId &&
        InterlockedCompareExchange(&g_frame.mirroredDraws, 0, 0) != 0)
    {
        if (IsPresentationMode())
        {
            CompletePresentationFrame(device);
        }
        else
        {
            const bool completed = FinalizeFrameTargets(device);
            MemoryBarrier();
            InterlockedExchange(&g_record.state, completed ? 4 : 5);
        }
    }
    else if (SUCCEEDED(result) &&
        device == g_record.device &&
        GetCurrentThreadId() == g_record.deviceThreadId &&
        stateAtEntry == 1)
    {
        std::array<void*, 3> presentationTargets = {
            g_frame.ownedColor[0],
            g_frame.ownedColor[1],
            g_frame.menuColor};
        const bool transportReady =
            !IsPresentationMode() ||
            g_presentationBridge.EnsureGpuFrameTargets(
                device,
                presentationTargets);
        g_frame.ownedColor[0] = presentationTargets[0];
        g_frame.ownedColor[1] = presentationTargets[1];
        g_frame.menuColor = presentationTargets[2];
        // The process-lifetime OpenXR route must present Ref2-only startup,
        // spawn, death, and pause frames. Gameplay overlays retain their own
        // alive-local-player gates; only frame production starts before spawn.
        const bool processLifetimePresentation =
            IsPresentationMode() && g_runUntilStopped;
        const bool captureEligible =
            g_offlinePresentation ||
            processLifetimePresentation ||
            (g_callbacks.isCaptureEligible != nullptr &&
             g_callbacks.isCaptureEligible());
        const bool renderReady =
            transportReady &&
            captureEligible &&
            (!IsPresentationMode() ||
            g_presentationBridge.RequestRender(
                g_runtimeRenderRequest,
                g_runUntilStopped
                    ? kContinuousRenderRequestTimeoutMs
                    : kBoundedRenderRequestTimeoutMs));
        if (renderReady && IsPresentationMode())
        {
            if (!g_presentationTimingStarted)
            {
                g_presentationRun.startedAt = GetTickCount();
                g_presentationTimingStarted = true;
            }
            // A continuous-mode request can become ready here after a prior
            // non-blocking poll left it pending.  Keep RenderView in lockstep
            // with every accepted request; otherwise BF1942 culls for the
            // first request while D3D8 replays a later headset pose.
            PrepareRuntimeRenderRequestPose();
        }
        if (!transportReady)
        {
            InterlockedCompareExchange(
                &g_record.state,
                5,
                1);
        }
        else if (captureEligible)
        {
            InterlockedCompareExchange(
                &g_record.state,
                renderReady ? 2 : (g_runUntilStopped ? 1 : 5),
                1);
        }
    }
    else if (IsPresentationMode() &&
        g_runUntilStopped &&
        SUCCEEDED(result) &&
        device == g_record.device &&
        GetCurrentThreadId() == g_record.deviceThreadId &&
        stateAtEntry == 2)
    {
        // Leaving a match can produce a Present with no replayable world or
        // Ref2 draw.  Returning to the idle state here permits the next
        // native Present to obtain a new runtime request instead of pinning
        // the game and presenter to the empty transition frame.
        // Do not call ReleaseFrameOwnedResources here: GPU shared color
        // targets are process-lifetime resources and that teardown also stops
        // the owned OpenXR companion.
        bfvr::d3d8probe::ResetStereoFrameRecordForResourceReuse(g_frame);
        ResetWorldCrosshairFrameTransforms();
        g_presentationFramePublished = false;
        g_runtimeRenderRequest = {};
        g_runtimeFramePosePolicy = {};
        g_renderViewPoseHook.ClearPose();
        InterlockedExchange(&g_record.state, 1);
    }
    InterlockedDecrement(&g_record.activeCallbacks);
    return result;
}

FrameMirrorResult TryMirrorFrameDraw(
    void* device,
    const FrameDrawInvocation& invocation)
{
    if (!IsFullFrameMode() ||
        device != g_record.device ||
        GetCurrentThreadId() != g_record.deviceThreadId ||
        InterlockedCompareExchange(&g_record.state, 3, 2) != 2)
    {
        return FrameMirrorResult::NotMirrored;
    }

    const std::int64_t replayStarted =
        bfvr::d3d8probe::ReadPerformanceCounter();
    const FrameMirrorResult result = MirrorDrawIntoFrame(device, invocation);
    g_frame.replayQpcTicks +=
        bfvr::d3d8probe::ReadPerformanceCounter() - replayStarted;
    MemoryBarrier();
    InterlockedExchange(
        &g_record.state,
        result == FrameMirrorResult::Failed ? 5 : 2);
    return result;
}

bool ShouldSkipOriginalFlatDraw(FrameMirrorResult result)
{
    return IsPresentationMode() &&
        !g_offlinePresentation &&
        !g_keepOriginalFlatBackbuffer &&
        (result == FrameMirrorResult::Mirrored ||
         result == FrameMirrorResult::IntentionallySuppressed);
}

HRESULT WINAPI HookDrawPrimitive(
    void* device,
    DWORD primitiveType,
    UINT startVertex,
    UINT primitiveCount)
{
    InterlockedIncrement(&g_record.activeCallbacks);
    FrameDrawInvocation invocation = {
        FrameDrawKind::Primitive,
        primitiveType,
        startVertex,
        0,
        0,
        0,
        primitiveCount};
    CaptureDrawStack(
        invocation,
        reinterpret_cast<void**>(_AddressOfReturnAddress()),
        kGameDrawPrimitiveReturn,
        9);
    const FrameMirrorResult mirrorResult =
        TryMirrorFrameDraw(device, invocation);
    const HRESULT originalResult = ShouldSkipOriginalFlatDraw(mirrorResult)
        ? S_OK
        : InvokeOriginalFrameDraw(device, invocation);
    InterlockedDecrement(&g_record.activeCallbacks);
    return originalResult;
}

HRESULT WINAPI HookDrawIndexedPrimitive(
    void* device,
    DWORD primitiveType,
    UINT minimumVertexIndex,
    UINT vertexCount,
    UINT startIndex,
    UINT primitiveCount)
{
    InterlockedIncrement(&g_record.activeCallbacks);
    if (IsFullFrameMode())
    {
        FrameDrawInvocation invocation = {
            FrameDrawKind::IndexedPrimitive,
            primitiveType,
            0,
            minimumVertexIndex,
            vertexCount,
            startIndex,
            primitiveCount};
        CaptureDrawStack(
            invocation,
            reinterpret_cast<void**>(_AddressOfReturnAddress()),
            kGameDrawIndexedPrimitiveReturn,
            10);
        const bfvr::D3D8WeaponMotionD3D8Api weaponMotionApi = {
            g_methods.setTransform,
            g_methods.getTransform,
            g_methods.getRenderState,
            g_methods.getVertexShader};
        bfvr::D3D8WeaponMotionRestore weaponMotionRestore = {};
        const bool weaponMotionApplied =
            IsPresentationMode() && !g_offlinePresentation &&
            bfvr::BeginD3D8WeaponMotionOverlayDraw(
                device,
                weaponMotionApi,
                reinterpret_cast<void**>(_AddressOfReturnAddress()),
                weaponMotionRestore);
        if (weaponMotionApplied)
        {
            // Replay starts from the exact source World transform. Restore the
            // temporary flat-draw attachment before taking its state snapshot;
            // it is reapplied per eye from this captured rigid attachment.
            bfvr::EndD3D8WeaponMotionOverlayDraw(
                device,
                weaponMotionApi,
                weaponMotionRestore);
            invocation.replayWeaponMotion = true;
            std::memcpy(
                &invocation.weaponMotionWorldAttachment,
                &weaponMotionRestore.worldSpaceAttachment,
                sizeof(invocation.weaponMotionWorldAttachment));
        }
        const FrameMirrorResult mirrorResult =
            TryMirrorFrameDraw(device, invocation);
        HRESULT originalResult = S_OK;
        if (!ShouldSkipOriginalFlatDraw(mirrorResult))
        {
            // The original flat draw remains the fail-safe fallback for an
            // excluded target, a safety bound, or any BFVR replay failure.
            // Reapply the same temporary attachment only around that fallback.
            bfvr::D3D8WeaponMotionRestore fallbackWeaponMotionRestore = {};
            const bool fallbackWeaponMotionApplied =
                IsPresentationMode() && !g_offlinePresentation &&
                bfvr::BeginD3D8WeaponMotionOverlayDraw(
                    device,
                    weaponMotionApi,
                    reinterpret_cast<void**>(_AddressOfReturnAddress()),
                    fallbackWeaponMotionRestore);
            originalResult = InvokeOriginalFrameDraw(device, invocation);
            if (fallbackWeaponMotionApplied)
            {
                bfvr::EndD3D8WeaponMotionOverlayDraw(
                    device,
                    weaponMotionApi,
                    fallbackWeaponMotionRestore);
            }
        }
        InterlockedDecrement(&g_record.activeCallbacks);
        return originalResult;
    }

    const FrameDrawInvocation invocation = {
        FrameDrawKind::IndexedPrimitive,
        primitiveType,
        0,
        minimumVertexIndex,
        vertexCount,
        startIndex,
        primitiveCount};
    const HRESULT originalResult = InvokeOriginalFrameDraw(device, invocation);
    if (g_mode == ProbeMode::OneDraw &&
        SUCCEEDED(originalResult) &&
        primitiveCount >= kMinimumCandidatePrimitiveCount &&
        device == g_record.device &&
        GetCurrentThreadId() == g_record.deviceThreadId &&
        InterlockedCompareExchange(&g_record.state, 3, 2) == 2)
    {
        g_record.originalDrawResult = originalResult;
        const AttemptResult attempt = TryReplayStereoPair(
            device,
            primitiveType,
            minimumVertexIndex,
            vertexCount,
            startIndex,
            primitiveCount);
        if (attempt == AttemptResult::NotEligible)
        {
            InterlockedIncrement(&g_record.rejectedCandidates);
            InterlockedExchange(&g_record.state, 2);
        }
        else if (attempt == AttemptResult::EmptyOrIdentical)
        {
            const LONG emptyAttempts =
                InterlockedIncrement(&g_record.emptyOrIdenticalCandidates);
            InterlockedExchange(
                &g_record.state,
                emptyAttempts < kMaximumEmptyCandidateAttempts ? 2 : 5);
        }
        else
        {
            MemoryBarrier();
            InterlockedExchange(
                &g_record.state,
                attempt == AttemptResult::Completed ? 4 : 5);
        }
    }

    InterlockedDecrement(&g_record.activeCallbacks);
    return originalResult;
}

HRESULT WINAPI HookDrawPrimitiveUP(
    void* device,
    DWORD primitiveType,
    UINT primitiveCount,
    const void* vertexData,
    UINT vertexStride)
{
    InterlockedIncrement(&g_record.activeCallbacks);
    FrameDrawInvocation invocation = {
        FrameDrawKind::PrimitiveUP,
        primitiveType,
        0,
        0,
        0,
        0,
        primitiveCount,
        nullptr,
        0,
        vertexData,
        vertexStride};
    CaptureDrawStack(
        invocation,
        reinterpret_cast<void**>(_AddressOfReturnAddress()),
        kGameDrawPrimitiveUPReturn,
        8,
        true);
    const FrameMirrorResult mirrorResult =
        TryMirrorFrameDraw(device, invocation);
    const HRESULT originalResult = ShouldSkipOriginalFlatDraw(mirrorResult)
        ? S_OK
        : InvokeOriginalFrameDraw(device, invocation);
    InterlockedDecrement(&g_record.activeCallbacks);
    return originalResult;
}

HRESULT WINAPI HookDrawIndexedPrimitiveUP(
    void* device,
    DWORD primitiveType,
    UINT minimumVertexIndex,
    UINT vertexCount,
    UINT primitiveCount,
    const void* indexData,
    UINT indexFormat,
    const void* vertexData,
    UINT vertexStride)
{
    InterlockedIncrement(&g_record.activeCallbacks);
    FrameDrawInvocation invocation = {
        FrameDrawKind::IndexedPrimitiveUP,
        primitiveType,
        0,
        minimumVertexIndex,
        vertexCount,
        0,
        primitiveCount,
        indexData,
        indexFormat,
        vertexData,
        vertexStride};
    if (!g_runUntilStopped)
    {
        CaptureGameStack(invocation);
    }
    const FrameMirrorResult mirrorResult =
        TryMirrorFrameDraw(device, invocation);
    const HRESULT originalResult = ShouldSkipOriginalFlatDraw(mirrorResult)
        ? S_OK
        : InvokeOriginalFrameDraw(device, invocation);
    InterlockedDecrement(&g_record.activeCallbacks);
    return originalResult;
}

bool ResolveDeviceMethods()
{
    if (g_lifecycle.device == nullptr)
    {
        return false;
    }

    auto** const vtable = *reinterpret_cast<void***>(g_lifecycle.device);
    if (vtable == nullptr)
    {
        return false;
    }

    g_methods.reset = reinterpret_cast<ResetFn>(vtable[kDirect3DDevice8ResetSlot]);
    g_methods.present = reinterpret_cast<PresentFn>(vtable[kDirect3DDevice8PresentSlot]);
    g_methods.createRenderTarget =
        reinterpret_cast<CreateRenderTargetFn>(vtable[kDirect3DDevice8CreateRenderTargetSlot]);
    g_methods.createDepthStencilSurface =
        reinterpret_cast<CreateDepthStencilSurfaceFn>(
            vtable[kDirect3DDevice8CreateDepthStencilSurfaceSlot]);
    g_methods.createImageSurface =
        reinterpret_cast<CreateImageSurfaceFn>(vtable[kDirect3DDevice8CreateImageSurfaceSlot]);
    g_methods.copyRects =
        reinterpret_cast<CopyRectsFn>(vtable[kDirect3DDevice8CopyRectsSlot]);
    g_methods.setRenderTarget =
        reinterpret_cast<SetRenderTargetFn>(vtable[kDirect3DDevice8SetRenderTargetSlot]);
    g_methods.getRenderTarget =
        reinterpret_cast<GetRenderTargetFn>(vtable[kDirect3DDevice8GetRenderTargetSlot]);
    g_methods.getDepthStencilSurface =
        reinterpret_cast<GetDepthStencilSurfaceFn>(
            vtable[kDirect3DDevice8GetDepthStencilSurfaceSlot]);
    g_methods.clear = reinterpret_cast<ClearFn>(vtable[kDirect3DDevice8ClearSlot]);
    g_methods.setTransform =
        reinterpret_cast<SetTransformFn>(vtable[kDirect3DDevice8SetTransformSlot]);
    g_methods.getTransform =
        reinterpret_cast<GetTransformFn>(vtable[kDirect3DDevice8GetTransformSlot]);
    g_methods.setViewport =
        reinterpret_cast<SetViewportFn>(vtable[kDirect3DDevice8SetViewportSlot]);
    g_methods.getViewport =
        reinterpret_cast<GetViewportFn>(vtable[kDirect3DDevice8GetViewportSlot]);
    g_methods.getRenderState =
        reinterpret_cast<GetRenderStateFn>(vtable[kDirect3DDevice8GetRenderStateSlot]);
    g_methods.getTextureStageState =
        reinterpret_cast<GetTextureStageStateFn>(
            vtable[kDirect3DDevice8GetTextureStageStateSlot]);
    g_methods.drawPrimitive =
        reinterpret_cast<DrawPrimitiveFn>(vtable[kDirect3DDevice8DrawPrimitiveSlot]);
    g_methods.drawIndexedPrimitive =
        reinterpret_cast<DrawIndexedPrimitiveFn>(
            vtable[kDirect3DDevice8DrawIndexedPrimitiveSlot]);
    g_methods.drawPrimitiveUP =
        reinterpret_cast<DrawPrimitiveUPFn>(vtable[kDirect3DDevice8DrawPrimitiveUPSlot]);
    g_methods.drawIndexedPrimitiveUP =
        reinterpret_cast<DrawIndexedPrimitiveUPFn>(
            vtable[kDirect3DDevice8DrawIndexedPrimitiveUPSlot]);
    g_methods.getVertexShader =
        reinterpret_cast<GetVertexShaderFn>(vtable[kDirect3DDevice8GetVertexShaderSlot]);
    g_methods.setVertexShaderConstant =
        reinterpret_cast<SetVertexShaderConstantFn>(
            vtable[kDirect3DDevice8SetVertexShaderConstantSlot]);
    g_methods.getVertexShaderConstant =
        reinterpret_cast<GetVertexShaderConstantFn>(
            vtable[kDirect3DDevice8GetVertexShaderConstantSlot]);
    void* const targets[] = {
        reinterpret_cast<void*>(g_methods.reset),
        reinterpret_cast<void*>(g_methods.present),
        reinterpret_cast<void*>(g_methods.createRenderTarget),
        reinterpret_cast<void*>(g_methods.createDepthStencilSurface),
        reinterpret_cast<void*>(g_methods.createImageSurface),
        reinterpret_cast<void*>(g_methods.copyRects),
        reinterpret_cast<void*>(g_methods.setRenderTarget),
        reinterpret_cast<void*>(g_methods.getRenderTarget),
        reinterpret_cast<void*>(g_methods.getDepthStencilSurface),
        reinterpret_cast<void*>(g_methods.clear),
        reinterpret_cast<void*>(g_methods.setTransform),
        reinterpret_cast<void*>(g_methods.getTransform),
        reinterpret_cast<void*>(g_methods.setViewport),
        reinterpret_cast<void*>(g_methods.getViewport),
        reinterpret_cast<void*>(g_methods.getRenderState),
        reinterpret_cast<void*>(g_methods.getTextureStageState),
        reinterpret_cast<void*>(g_methods.drawPrimitive),
        reinterpret_cast<void*>(g_methods.drawIndexedPrimitive),
        reinterpret_cast<void*>(g_methods.drawPrimitiveUP),
        reinterpret_cast<void*>(g_methods.drawIndexedPrimitiveUP),
        reinterpret_cast<void*>(g_methods.getVertexShader),
        reinterpret_cast<void*>(g_methods.setVertexShaderConstant),
        reinterpret_cast<void*>(g_methods.getVertexShaderConstant)};
    return std::all_of(
        std::begin(targets),
        std::end(targets),
        [](const void* target) { return IsTrustedD3D8Target(target); });
}

void RemoveHooks()
{
    g_playerVrMotionFilter.DisableAndRemove();
    g_renderViewPoseHook.DisableAndRemove();
    if (IsFullFrameMode())
    {
        void* const frameTargets[] = {
            g_record.resetTarget,
            g_record.drawPrimitiveTarget,
            g_record.drawPrimitiveUPTarget,
            g_record.drawIndexedPrimitiveUPTarget};
        for (void* target : frameTargets)
        {
            if (target != nullptr)
            {
                MH_DisableHook(target);
                MH_RemoveHook(target);
            }
        }
    }
    void* const targets[] = {
        g_record.presentTarget,
        g_record.drawIndexedPrimitiveTarget};
    for (void* target : targets)
    {
        if (target != nullptr)
        {
            MH_DisableHook(target);
            MH_RemoveHook(target);
        }
    }
}

bool InstallHooks()
{
    if (!ResolveDeviceMethods() || !InitializeGameImageRange())
    {
        return false;
    }
    if (IsPresentationMode())
    {
        AppendLog(
            g_headCenteredWaterReflection
                ? L"Enabled the exact water additive-reflection fallback: its fixed-function reflection-vector pass uses the tracked head-centre View while retaining each eye's projection; base water remains stereo. Set BFVR_STEREO_WATER_REFLECTION=1 to restore the legacy fully stereo reflection path."
                : L"BFVR_STEREO_WATER_REFLECTION=1 restored the legacy fully stereo water reflection path.");
    }
    g_readbackApi = {
        g_methods.createImageSurface,
        g_methods.copyRects,
        ReleaseUnknown,
        IsTrustedD3D8Target};

    g_record.device = g_lifecycle.device;
    g_record.deviceThreadId = g_lifecycle.deviceThreadId;
    g_record.resetTarget = reinterpret_cast<void*>(g_methods.reset);
    g_record.presentTarget = reinterpret_cast<void*>(g_methods.present);
    g_record.drawPrimitiveTarget =
        reinterpret_cast<void*>(g_methods.drawPrimitive);
    g_record.drawIndexedPrimitiveTarget =
        reinterpret_cast<void*>(g_methods.drawIndexedPrimitive);
    g_record.drawPrimitiveUPTarget =
        reinterpret_cast<void*>(g_methods.drawPrimitiveUP);
    g_record.drawIndexedPrimitiveUPTarget =
        reinterpret_cast<void*>(g_methods.drawIndexedPrimitiveUP);

    const MH_STATUS initializeStatus = MH_Initialize();
    if (initializeStatus != MH_OK)
    {
        AppendLog(
            L"D3D8 stereo-pair probe skipped: MH_Initialize failed (%d).",
            static_cast<int>(initializeStatus));
        return false;
    }

    bool created =
        MH_CreateHook(
            g_record.presentTarget,
            reinterpret_cast<LPVOID>(&HookPresent),
            reinterpret_cast<LPVOID*>(&g_originalPresent)) == MH_OK &&
        g_originalPresent != nullptr &&
        MH_CreateHook(
            g_record.drawIndexedPrimitiveTarget,
            reinterpret_cast<LPVOID>(&HookDrawIndexedPrimitive),
        reinterpret_cast<LPVOID*>(&g_originalDrawIndexedPrimitive)) == MH_OK &&
        g_originalDrawIndexedPrimitive != nullptr;
    if (created && IsFullFrameMode())
    {
        created =
            MH_CreateHook(
                g_record.resetTarget,
                reinterpret_cast<LPVOID>(&HookReset),
                reinterpret_cast<LPVOID*>(&g_originalReset)) == MH_OK &&
            g_originalReset != nullptr &&
            MH_CreateHook(
                g_record.drawPrimitiveTarget,
                reinterpret_cast<LPVOID>(&HookDrawPrimitive),
                reinterpret_cast<LPVOID*>(&g_originalDrawPrimitive)) == MH_OK &&
            g_originalDrawPrimitive != nullptr &&
            MH_CreateHook(
                g_record.drawPrimitiveUPTarget,
                reinterpret_cast<LPVOID>(&HookDrawPrimitiveUP),
                reinterpret_cast<LPVOID*>(&g_originalDrawPrimitiveUP)) == MH_OK &&
            g_originalDrawPrimitiveUP != nullptr &&
            MH_CreateHook(
                g_record.drawIndexedPrimitiveUPTarget,
                reinterpret_cast<LPVOID>(&HookDrawIndexedPrimitiveUP),
                reinterpret_cast<LPVOID*>(&g_originalDrawIndexedPrimitiveUP)) == MH_OK &&
            g_originalDrawIndexedPrimitiveUP != nullptr;
    }
    if (created && IsPresentationMode())
    {
        created = g_renderViewPoseHook.Create(
            reinterpret_cast<void*>(g_gameImageBegin),
            AppendPresentationLog);
    }
    if (created && IsPresentationMode())
    {
        created = g_playerVrMotionFilter.Create(
            reinterpret_cast<void*>(g_gameImageBegin),
            AppendPresentationLog);
    }
    if (!created)
    {
        RemoveHooks();
        MH_Uninitialize();
        return false;
    }

    const bool enabled =
        MH_EnableHook(g_record.presentTarget) == MH_OK &&
        MH_EnableHook(g_record.drawIndexedPrimitiveTarget) == MH_OK &&
        (!IsFullFrameMode() ||
            (MH_EnableHook(g_record.resetTarget) == MH_OK &&
                MH_EnableHook(g_record.drawPrimitiveTarget) == MH_OK &&
                MH_EnableHook(g_record.drawPrimitiveUPTarget) == MH_OK &&
                MH_EnableHook(g_record.drawIndexedPrimitiveUPTarget) == MH_OK)) &&
        (!IsPresentationMode() ||
            (g_renderViewPoseHook.Enable() && g_playerVrMotionFilter.Enable()));
    if (!enabled)
    {
        RemoveHooks();
        MH_Uninitialize();
        return false;
    }
    return true;
}

DWORD WINAPI RunProbe(void*)
{
    constexpr DWORD kLifecycleReadyTimeoutMs = 60000;
    g_presentationConfiguration =
        bfvr::ReadD3D8PresentationConfiguration();
    bfvr::MainMenuOverlay mainMenuOverlayAssets;
    const bool mainMenuOverlayEnabled =
        IsPresentationMode() && !g_offlinePresentation &&
        mainMenuOverlayAssets.Initialize(AppendPresentationLog);
    bfvr::SetMainMenuOverlayAvailable(mainMenuOverlayEnabled);
    bfvr::StartupMenuPresentation startupMenuPresentation;
    if (g_runUntilStopped)
    {
        // Gameplay presentation now follows BF1942's process lifetime only.
        // PID 27344 proved that an independently polled named stop handle can
        // end the injected renderer while the loader and game remain alive.
        // The game process already provides the authoritative lifetime; an
        // explicit diagnostic stop channel is not part of the player launch.
        AppendLog(
            L"Continuous OpenXR presentation is bound to BF1942 process lifetime; no independent renderer-stop event is opened.");
    }
    if (IsPresentationMode() &&
        g_runUntilStopped &&
        !g_offlinePresentation)
    {
        startupMenuPresentation.Start(
            GetModuleHandleW(nullptr),
            AppendPresentationLog);
    }
    const DWORD lifecycleStartedAt = GetTickCount();
    while (g_runUntilStopped ||
        GetTickCount() - lifecycleStartedAt < kLifecycleReadyTimeoutMs)
    {
        if (g_callbacks.tryGetReadyLifecycle != nullptr &&
            g_callbacks.tryGetReadyLifecycle(&g_lifecycle))
        {
            break;
        }
        startupMenuPresentation.Pump();
        Sleep(2);
    }
    startupMenuPresentation.Stop();
    if (g_lifecycle.device == nullptr || g_lifecycle.deviceThreadId == 0)
    {
        AppendLog(
            L"D3D8 stereo-pair probe skipped: the verified D3D8 lifecycle did not become ready within %lu ms.",
            kLifecycleReadyTimeoutMs);
        bfvr::SetMainMenuOverlayAvailable(false);
        SignalCompletion();
        return 0;
    }
    g_vertexShaderIdentityResolver.Resolve();
    if (IsPresentationMode() &&
        g_presentationConfiguration.scaleSource ==
            bfvr::D3D8PresentationScaleSource::InvalidEnvironment)
    {
        AppendLog(
            L"Ignoring invalid BFVR_OPENXR_WORLD_RENDER_SCALE; expected 0.50 through 1.25 and using default %.2f.",
            g_presentationConfiguration.worldRenderScale);
    }
    if (IsPresentationMode() &&
        g_presentationConfiguration.nativeFirstPersonArmsSource ==
            bfvr::D3D8NativeFirstPersonArmsSource::InvalidEnvironment)
    {
        AppendLog(
            L"Ignoring invalid BFVR_NATIVE_1P_ARMS; expected strict 0 or 1 and retaining the default native first-person-arm replay.");
    }
    if (IsPresentationMode() &&
        g_presentationConfiguration.nativeFirstPersonArmsEnabled)
    {
        AppendLog(
            L"Native first-person-arm replay is enabled. BFVR will stereo-replay only the exact game-selected AnimatedMesh arm draws; it does not apply controller IK or change gameplay state.");
    }
    if (IsPresentationMode() &&
        (!g_lifecycle.presentationReadable ||
         !g_presentationBridge.Initialize(
             g_lifecycle.backBufferWidth,
             g_lifecycle.backBufferHeight,
             g_presentationConfiguration.worldRenderScale,
             g_offlinePresentation
                ? bfvr::D3D8PresentationCompanion::OfflineTransport
                : bfvr::D3D8PresentationCompanion::OpenXR,
             AppendPresentationLog) ||
         g_presentationBridge.LeftWorldWidth() !=
             g_presentationBridge.RightWorldWidth() ||
         g_presentationBridge.LeftWorldHeight() !=
             g_presentationBridge.RightWorldHeight()))
    {
        AppendLog(
            L"D3D8 OpenXR frame probe skipped: the x64 companion did not provide one symmetric runtime-sized stereo target pair.");
        g_presentationBridge.Shutdown();
        bfvr::SetMainMenuOverlayAvailable(false);
        SignalCompletion();
        return 0;
    }
    g_presentationRun.gpuResidentTransport =
        g_presentationBridge.UsesGpuSharedTargets() ? TRUE : FALSE;
    if (!InstallHooks())
    {
        AppendLog(
            L"D3D8 stereo probe skipped: its required methods were not direct targets in system d3d8.dll or the pinned BFVRD3D8To9.dll, or its bounded hook set could not be installed.");
        if (IsPresentationMode())
        {
            g_presentationBridge.Shutdown();
        }
        bfvr::SetMainMenuOverlayAvailable(false);
        SignalCompletion();
        return 0;
    }
    if (IsPresentationMode() && !g_offlinePresentation)
    {
        // Startup pointer teardown clears this process-wide gate. Re-arm it
        // for the GPU-resident presenter, which owns an independent copy of
        // the same verified raster stacks after the handoff.
        bfvr::SetMainMenuOverlayAvailable(mainMenuOverlayEnabled);
        bfvr::StartControllerInputOverlay(
            reinterpret_cast<void*>(g_gameImageBegin),
            AppendPresentationLog);
        bfvr::StartMenuPointerOverlay(
            reinterpret_cast<void*>(g_gameImageBegin),
            g_presentationBridge.RuntimeUiWidth(),
            g_presentationBridge.RuntimeUiHeight(),
            g_lifecycle.backBufferWidth,
            g_lifecycle.backBufferHeight,
            AppendPresentationLog);
        bfvr::StartD3D8WeaponMotionOverlay(AppendPresentationLog);
        bfvr::StartWeaponAimOverlay(
            reinterpret_cast<void*>(g_gameImageBegin),
            AppendPresentationLog);
        bfvr::StartCrosshairOverlay(
            reinterpret_cast<void*>(g_gameImageBegin),
            AppendPresentationLog);
    }

    InterlockedExchange(&g_record.state, 1);
    if (IsFullFrameMode())
    {
        AppendLog(
            g_runUntilStopped
                ? L"Enabled process-lifetime full-draw-frame D3D8 stereo presentation after launch-time CPU-bridge handoff: Reset=%p Present=%p DrawPrimitive=%p DrawIndexedPrimitive=%p DrawPrimitiveUP=%p DrawIndexedPrimitiveUP=%p. It requests OpenXR frames from the first game-device Present, including spawn/death/pause Ref2-only transitions, and mirrors at most %ld eligible full-size draws per frame into frame-lived BFVR-owned left/right world targets plus one transparent Ref2 UI target. Native menus use a latched world-space reference and the gameplay HUD uses a head-locked reference. Gameplay input/weapon overlays retain their independent alive-local-player gates; Reset-safe transport recreation and exact state restoration remain active."
                : L"Enabled bounded full-draw-frame D3D8 stereo probe: Reset=%p Present=%p DrawPrimitive=%p DrawIndexedPrimitive=%p DrawPrimitiveUP=%p DrawIndexedPrimitiveUP=%p. After the sustained local-player-isAlive gate it will mirror at most %ld eligible full-size draws into frame-lived BFVR-owned left/right world targets plus one transparent Ref2 UI target. Exact NewRendFont glyph batches and Ref2 menu quads are omitted from both world eyes and replayed once into that layer; the skybox policy remains separate. The probe excludes non-presentation/depthless targets, verifies state after every draw, finalizes at the next Present, and releases before Reset.",
            g_record.resetTarget,
            g_record.presentTarget,
            g_record.drawPrimitiveTarget,
            g_record.drawIndexedPrimitiveTarget,
            g_record.drawPrimitiveUPTarget,
            g_record.drawIndexedPrimitiveUPTarget,
            kMaximumFrameDraws);
    }
    else
    {
        AppendLog(
            L"Enabled one-draw D3D8 stereo-pair probe: Present=%p DrawIndexedPrimitive=%p. After the sustained local-player-isAlive gate, it will forward the game draw, select one full-size format-22 indexed candidate with at least %u primitives, render that same draw into transient BFVR-owned left/right color+depth targets using diagnostic View/Projection offsets, restore exact target/depth/viewport/View/Projection state, read back the owned colors, and release every reference.",
            g_record.presentTarget,
            g_record.drawIndexedPrimitiveTarget,
            kMinimumCandidatePrimitiveCount);
    }

    const DWORD captureStartedAt = GetTickCount();
    while (g_runUntilStopped ||
        GetTickCount() - captureStartedAt < kCaptureTimeoutMs)
    {
        LONG state = InterlockedCompareExchange(&g_record.state, 0, 0);
        if (state == 4 || state == 5)
        {
            break;
        }
        Sleep(10);
    }

    LONG state = InterlockedCompareExchange(&g_record.state, 0, 0);
    if (state == 4 || state == 5)
    {
        while (InterlockedCompareExchange(&g_record.activeCallbacks, 0, 0) != 0)
        {
            Sleep(1);
        }
        if (IsFullFrameMode())
        {
            bfvr::d3d8probe::ReportStereoFrameResult(
                AppendLog,
                g_record,
                g_frame);
        }
        else
        {
            bfvr::d3d8probe::ReportStereoPairResult(
                AppendLog,
                g_record,
                kDiagnosticHalfEyeOffset,
                kDiagnosticConvergenceDistance);
        }
        if (IsPresentationMode())
        {
            g_renderViewPoseHook.LogSummary();
            g_playerVrMotionFilter.LogSummary();
            bfvr::d3d8probe::ReportContinuousPresentationResult(
                AppendLog,
                g_presentationRun,
                g_presentationBridge.LeftWorldWidth(),
                g_presentationBridge.LeftWorldHeight());
        }
    }
    else
    {
        if (IsFullFrameMode())
        {
            AppendLog(
                L"D3D8 full-draw-frame stereo probe timed out after %lu ms; mirroredDraws=%ld excludedTargetDraws=%ld resourcesReady=%ld. The bounded loader will close this directly launched process.",
                kCaptureTimeoutMs,
                InterlockedCompareExchange(&g_frame.mirroredDraws, 0, 0),
                InterlockedCompareExchange(&g_frame.excludedTargetDraws, 0, 0),
                InterlockedCompareExchange(&g_frame.resourcesReady, 0, 0));
        }
        else
        {
            AppendLog(
                L"D3D8 stereo-pair probe timed out after %lu ms without completing an eligible replay; rejectedCandidates=%ld emptyOrIdenticalCandidates=%ld. No BFVR resource survives cleanup.",
                kCaptureTimeoutMs,
                InterlockedCompareExchange(&g_record.rejectedCandidates, 0, 0),
                InterlockedCompareExchange(&g_record.emptyOrIdenticalCandidates, 0, 0));
        }
        InterlockedExchange(&g_record.state, 0);
    }

    if (IsPresentationMode() && !g_offlinePresentation)
    {
        bfvr::StopCrosshairOverlay();
        bfvr::StopWeaponAimOverlay();
        bfvr::StopD3D8WeaponMotionOverlay();
        bfvr::StopMenuPointerOverlay();
        bfvr::StopControllerInputOverlay();
    }
    bfvr::SetMainMenuOverlayAvailable(false);
    RemoveHooks();
    MH_Uninitialize();
    AppendLog(
        IsFullFrameMode() && g_runUntilStopped
            ? L"D3D8 full-draw-frame stereo probe removed its Reset, Present, and four draw-family hooks after the presentation pipeline ended."
            : IsFullFrameMode()
            ? L"D3D8 full-draw-frame stereo probe removed its Reset, Present, and four draw-family hooks after the bounded window."
            : L"D3D8 stereo-pair probe removed both hooks after its bounded window.");
    if (IsPresentationMode())
    {
        g_presentationBridge.Shutdown();
    }
    SignalCompletion();
    return 0;
}

} // namespace

namespace bfvr
{

void StartStereoProbe(
    const D3D8ObserverCallbacks& callbacks,
    ProbeMode mode)
{
    if (callbacks.tryGetReadyLifecycle == nullptr ||
        callbacks.isCaptureEligible == nullptr ||
        callbacks.appendLog == nullptr ||
        callbacks.signalCompletion == nullptr ||
        InterlockedCompareExchange(&g_started, 1, 0) != 0)
    {
        return;
    }

    g_callbacks = callbacks;
    g_mode = mode;
    if (mode == ProbeMode::FullFramePresentation)
    {
        AppendLog(
            L"BFVR runtime diagnostics=%s: normal keeps all rendering and restoration writes while skipping per-draw proof readbacks and provenance aggregation; set BFVR_DIAGNOSTICS=deep to enable those expensive proof checks for a troubleshooting run.",
            bfvr::DescribeD3D8RuntimeDiagnosticLevel(g_runtimeDiagnostics));
    }
    HANDLE worker = CreateThread(nullptr, 0, RunProbe, nullptr, 0, nullptr);
    if (worker == nullptr)
    {
        AppendLog(
            L"D3D8 stereo-pair worker could not start (%lu); no D3D8 code was patched.",
            GetLastError());
        SignalCompletion();
        return;
    }
    CloseHandle(worker);
    AppendLog(
        mode == ProbeMode::FullFramePresentation &&
            g_offlinePresentation
            ? L"Requested the bounded D3D9Ex-to-x64 no-HMD shared-target proof; it remains inactive until the verified lifecycle and offline companion are ready."
            : mode == ProbeMode::FullFramePresentation &&
                g_runUntilStopped
            ? L"Requested a D3D8-to-x64 OpenXR presentation proof bound to BF1942 process lifetime; a CPU startup-menu bridge opens OpenXR immediately, then hands off to GPU-resident stereo when BF1942 creates its D3D8 device."
            : mode == ProbeMode::FullFramePresentation
            ? L"Requested the bounded continuous D3D8-to-x64 OpenXR presentation proof; it remains inactive until the verified lifecycle, runtime companion, and sustained local-player-isAlive gate are ready."
            : mode != ProbeMode::OneDraw
            ? L"Requested the bounded one-frame, no-HMD D3D8 full draw-family stereo-stream proof; it remains inactive until the verified lifecycle and sustained local-player-isAlive gate are ready."
            : L"Requested the explicit one-draw, no-HMD D3D8 stereo-pair proof; it remains inactive until the verified lifecycle and sustained local-player-isAlive gate are ready.");
}

void StartD3D8StereoPairProbe(const D3D8ObserverCallbacks& callbacks)
{
    StartStereoProbe(callbacks, ProbeMode::OneDraw);
}

void StartD3D8StereoFrameProbe(const D3D8ObserverCallbacks& callbacks)
{
    StartStereoProbe(callbacks, ProbeMode::FullFrame);
}

void StartD3D8StereoFramePresentationProbe(
    const D3D8ObserverCallbacks& callbacks)
{
    wchar_t runUntilStopped[2] = {};
    g_runUntilStopped =
        GetEnvironmentVariableW(
            kRunUntilStoppedEnvironment,
            runUntilStopped,
        static_cast<DWORD>(std::size(runUntilStopped))) == 1 &&
        runUntilStopped[0] == L'1';
    wchar_t stereoWaterReflection[2] = {};
    g_headCenteredWaterReflection = !(
        GetEnvironmentVariableW(
            kStereoWaterReflectionEnvironment,
            stereoWaterReflection,
            static_cast<DWORD>(std::size(stereoWaterReflection))) == 1 &&
        stereoWaterReflection[0] == L'1');
    g_keepOriginalFlatBackbuffer = ReadKeepOriginalFlatBackbuffer();
    g_runtimeDiagnostics = g_runUntilStopped
        ? ReadD3D8RuntimeDiagnosticLevel()
        : D3D8RuntimeDiagnosticLevel::Deep;
    if (g_keepOriginalFlatBackbuffer)
    {
        AppendLog(
            L"Retaining the original flat backbuffer because BFVR_KEEP_FLAT_BACKBUFFER=1 or BFVR_DESKTOP_MIRROR=0; stereo replay remains active but the triple-render performance optimization is disabled.");
    }
    StartStereoProbe(callbacks, ProbeMode::FullFramePresentation);
}

void StartD3D8StereoFrameSharedTransportProbe(
    const D3D8ObserverCallbacks& callbacks)
{
    g_offlinePresentation = true;
    g_runtimeDiagnostics = D3D8RuntimeDiagnosticLevel::Deep;
    StartStereoProbe(callbacks, ProbeMode::FullFramePresentation);
}

} // namespace bfvr
