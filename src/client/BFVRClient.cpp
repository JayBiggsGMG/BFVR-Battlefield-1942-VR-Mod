#include "openxr/OpenXRBootstrap.h"
#include "client/D3D8CallInventory.h"
#include "client/BF1942FrameLimiterOverride.h"
#include "client/D3D8ImportRoute.h"
#include "client/DSoundListenerProbe.h"
#include "client/D3D8RuntimeRedirect.h"
#include "client/D3D8RuntimeDiagnostics.h"
#include "client/D3D8StateCensus.h"
#include "client/D3D8WeaponTransformOwnershipProbe.h"
#include "client/D3D8WeaponViewModelProbe.h"
#include "client/BFSoldierFirstPersonArmProbe.h"
#include "client/D3D8StereoPairProbe.h"
#include "client/PlayerInputProbe.h"
#include "client/WeaponFireProbe.h"
#include "presenter/D3DSystemRuntime.h"
#include "settings/UserSettings.h"
#include "bfvr_runtime_diagnostics.hpp"

#include <MinHook.h>

#include <windows.h>
#include <d3d11.h>
#include <tlhelp32.h>

#include <cstdarg>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <algorithm>
#include <atomic>
#include <new>
#include <string>
#include <iterator>
#include <vector>

namespace
{
constexpr std::size_t kDirect3D8VtableSlots = 16;
constexpr std::size_t kDirect3D8CreateDeviceSlot = 15;
constexpr std::size_t kIUnknownReleaseSlot = 2;
constexpr std::size_t kDirect3DDevice8ResetSlot = 14;
constexpr std::size_t kDirect3DDevice8PresentSlot = 15;
constexpr std::size_t kDirect3DDevice8CreateRenderTargetSlot = 25;
constexpr std::size_t kDirect3DDevice8CreateImageSurfaceSlot = 27;
constexpr std::size_t kDirect3DDevice8CopyRectsSlot = 28;
// Keep this contiguous tail explicit. The historical observer omitted
// UpdateTexture at slot 29 and consequently mislabeled each later target.
constexpr std::size_t kDirect3DDevice8UpdateTextureSlot = 29;
constexpr std::size_t kDirect3DDevice8GetFrontBufferSlot = 30;
constexpr std::size_t kDirect3DDevice8SetRenderTargetSlot = 31;
constexpr std::size_t kDirect3DDevice8GetRenderTargetSlot = 32;
constexpr std::size_t kDirect3DDevice8GetDepthStencilSurfaceSlot = 33;
constexpr std::size_t kDirect3DDevice8BeginSceneSlot = 34;
constexpr std::size_t kDirect3DDevice8EndSceneSlot = 35;
constexpr std::size_t kDirect3DDevice8ClearSlot = 36;
constexpr std::size_t kDirect3DDevice8SetTransformSlot = 37;
// The DrawPrimitive family follows the D3D8 palette methods.  Keep these
// slots separate from the well-tested 29..37 lifecycle tail above: a wrong
// draw ABI would be unsafe to detour.
constexpr std::size_t kDirect3DDevice8DrawPrimitiveSlot = 70;
constexpr std::size_t kDirect3DDevice8DrawIndexedPrimitiveSlot = 71;
constexpr std::size_t kDirect3DDevice8DrawPrimitiveUPSlot = 72;
constexpr std::size_t kDirect3DDevice8DrawIndexedPrimitiveUPSlot = 73;
constexpr std::size_t kDirect3DSurface8GetDescSlot = 8;
constexpr std::size_t kDirect3DSurface8LockRectSlot = 9;
constexpr std::size_t kDirect3DSurface8UnlockRectSlot = 10;
constexpr DWORD kRendererTransactionStateGlobalRva = 0x005C017C;
constexpr std::size_t kRendererWorldTransformOffset = 0x284;
constexpr std::size_t kRendererViewTransformOffset = 0x2C4;
constexpr std::size_t kRendererProjectionTransformOffset = 0x304;
constexpr DWORD kD3DTransformProjection = 3;
constexpr DWORD_PTR kLocalCameraAddRefRva = 0x00165870;
constexpr DWORD kFrameCoordinatorRva = 0x0004ABC0;
constexpr DWORD kRendererCoordinatorRva = 0x00066D80;
constexpr DWORD kRenderViewRva = 0x000662C0;
constexpr DWORD kSkinningShaderSceneBatchMode2Rva = 0x0022CB00;
constexpr DWORD kSkinningShaderSceneBatchMode0Rva = 0x0022CC70;
constexpr DWORD kSkinningShaderSceneBatchMode1Rva = 0x0022CDD0;
constexpr DWORD kSkinningShaderSubmittedItemRouterRva = 0x0022B8D0;
constexpr DWORD kSkinningShaderItemTypeQueryReturnRva = 0x0022B905;
constexpr DWORD kSkinningShaderPrimaryItemSubmissionRva = 0x0022AC50;
constexpr DWORD kSkinningShaderSecondaryItemSubmissionRva = 0x0022AD20;
constexpr DWORD kSkinningShaderPrimaryItemTypeGlobalRva = 0x00503508;
constexpr DWORD kSkinningShaderSecondaryItemTypeGlobalRva = 0x00503500;
constexpr DWORD kSkinningShaderTransformDispatcherGlobalRva = 0x005C0184;
constexpr DWORD kSkinningShaderPrimitiveDispatcherGlobalRva = 0x005A99D4;
constexpr DWORD kSkinningShaderRenderModeGlobalRva = 0x005C9BE4;
constexpr DWORD kRenderViewPrimaryObjectSubmitRva = 0x00066AD0;
constexpr DWORD kRenderViewPrimaryRendererStageRva = 0x00066B05;
constexpr DWORD kRenderViewSecondaryRendererStageRva = 0x00066B89;
constexpr DWORD kRenderViewTrailingRendererStageRva = 0x00066C5A;
constexpr DWORD kRenderViewLoopExitRva = 0x00066FD9;
constexpr DWORD kPostViewCallbackListRva = 0x0006E900;
constexpr DWORD kTextOverlayQueueRva = 0x00061400;
constexpr DWORD kActiveRenderViewGlobalRva = 0x005AB868;
constexpr DWORD kRenderViewTransformSetterDispatchRva = 0x000668C1;
constexpr DWORD kRenderViewViewMatrixResultRva = 0x000668DC;
constexpr DWORD kRenderViewProjectionMatrixResultRva = 0x000668EE;
constexpr DWORD kRenderViewTransformTransactionReturnRva = 0x000668F5;
constexpr std::size_t kRenderViewTransformStackOffset = 0x70;
constexpr DWORD kRenderViewSetTransformationRva = 0x001B7E00;
constexpr DWORD kRenderViewSetTransformationReturnRva = 0x000668D1;
constexpr std::size_t kRenderViewTransformationOffset = 0x3C;
constexpr DWORD kConfiguredViewManagerGlobalRva = 0x00571EAC;
constexpr DWORD kConfiguredViewManagerAssignedRva = 0x00056FF4;
constexpr DWORD kConfiguredViewRegistryGlobalRva = 0x0055F8D4;
constexpr std::size_t kConfiguredViewActiveIndexOffset = 0x168;
constexpr std::size_t kConfiguredViewListBeginOffset = 0x228;
constexpr std::size_t kConfiguredViewListEndOffset = 0x22C;
constexpr std::size_t kViewOwnerRenderViewOffset = 0x94;
constexpr std::size_t kRenderViewViewPortOffset = 0x08;
constexpr std::size_t kRenderViewCloneVtableOffset = 0x70;
constexpr DWORD kRenderViewDispatchPostAssignmentRva = 0x000662DA;
constexpr bool kEnableCameraTransactionBreakpoints = false;
constexpr ULONG_PTR kObserverInitializationRequestPresentBridgeProbe = 1;
constexpr ULONG_PTR kObserverInitializationRequestSurfaceDescriptorProbe = 2;
constexpr ULONG_PTR kObserverInitializationRequestSurfaceCopyProbe = 3;
constexpr ULONG_PTR kObserverInitializationRequestSurfaceStreamProbe = 4;
constexpr ULONG_PTR kObserverInitializationRequestSurfaceResetProbe = 5;
constexpr ULONG_PTR kObserverInitializationRequestSurfaceReadbackProbe = 6;
constexpr ULONG_PTR kObserverInitializationRequestSurfaceSceneReadbackProbe = 7;
constexpr ULONG_PTR kObserverInitializationRequestSurfaceD3D11UploadProbe = 8;
constexpr ULONG_PTR kObserverInitializationRequestRenderViewTransformProbe = 9;
constexpr ULONG_PTR kObserverInitializationRequestRenderViewSetterBaselineProbe = 10;
constexpr ULONG_PTR kObserverInitializationRequestConfiguredViewListProbe = 11;
constexpr ULONG_PTR kObserverInitializationRequestConfiguredViewListWriterProbe = 12;
constexpr ULONG_PTR kObserverInitializationRequestRenderViewSingleEyeProbe = 13;
constexpr ULONG_PTR kObserverInitializationRequestSceneBatchProbe = 14;
constexpr ULONG_PTR kObserverInitializationRequestD3D8CallInventoryProbe = 15;
constexpr ULONG_PTR kObserverInitializationRequestD3D8StateCensusProbe = 16;
constexpr ULONG_PTR kObserverInitializationRequestD3D8StereoPairProbe = 17;
constexpr ULONG_PTR kObserverInitializationRequestD3D8StereoFrameProbe = 18;
constexpr ULONG_PTR
    kObserverInitializationRequestD3D8StereoFramePresentationProbe = 19;
constexpr ULONG_PTR kObserverInitializationRequestD3D8To9FlatProbe = 20;
constexpr ULONG_PTR kObserverInitializationRequestD3D8To9ObserverProbe = 21;
constexpr ULONG_PTR
    kObserverInitializationRequestD3D8To9SharedFrameProbe = 22;
constexpr ULONG_PTR
    kObserverInitializationRequestD3D8To9OpenXRPresentationProbe = 23;
constexpr ULONG_PTR kObserverInitializationRequestPlayerInputProbe = 24;
constexpr ULONG_PTR kObserverInitializationRequestWeaponViewModelProbe = 25;
constexpr ULONG_PTR
    kObserverInitializationRequestWeaponTransformOwnershipProbe = 26;
constexpr ULONG_PTR kObserverInitializationRequestWeaponFireProbe = 27;
constexpr ULONG_PTR kObserverInitializationRequestFirstPersonArmProbe = 28;
constexpr DWORD kObserverInitializationParametersMagic = 0x52564642;
constexpr LONG kProjectionStreamCopyLimit = 60;
constexpr DWORD_PTR kProjectionWrapperReturnAddress = 0x0045FE21;
constexpr DWORD kOrdinaryWorldProjectionCallerReturnAddress = 0x00466F56;

// This is the x86 D3D8 layout needed by the observer.  The project deliberately
// avoids a proxy d3d8.dll and does not need the deprecated DirectX SDK headers.
struct D3DPresentParameters
{
    UINT backBufferWidth;
    UINT backBufferHeight;
    UINT backBufferFormat;
    UINT backBufferCount;
    UINT multiSampleType;
    UINT swapEffect;
    HWND deviceWindow;
    BOOL windowed;
    BOOL enableAutoDepthStencil;
    UINT autoDepthStencilFormat;
    DWORD flags;
    UINT fullScreenRefreshRateInHz;
    UINT fullScreenPresentationInterval;
};

struct D3DSurfaceDescription
{
    UINT format;
    UINT type;
    DWORD usage;
    UINT pool;
    UINT size;
    UINT multiSampleType;
    UINT width;
    UINT height;
};
static_assert(sizeof(D3DSurfaceDescription) == sizeof(UINT) * 8, "D3D8 D3DSURFACE_DESC ABI changed unexpectedly.");

struct D3DLockedRect
{
    INT pitch;
    void* bits;
};
static_assert(sizeof(D3DLockedRect) == sizeof(void*) * 2, "D3D8 D3DLOCKED_RECT ABI changed unexpectedly.");

using Direct3DCreate8Fn = bfvr::D3D8CreateFunction;
using CreateDeviceFn = HRESULT(WINAPI*)(
    void* direct3D,
    UINT adapter,
    UINT deviceType,
    HWND focusWindow,
    DWORD behaviorFlags,
    D3DPresentParameters* presentationParameters,
    void** returnedDevice);
using PresentFn = HRESULT(WINAPI*)(
    void* device,
    const RECT* sourceRectangle,
    const RECT* destinationRectangle,
    HWND destinationWindowOverride,
    const RGNDATA* dirtyRegion);
using EndSceneFn = HRESULT(WINAPI*)(void* device);
using BeginSceneFn = HRESULT(WINAPI*)(void* device);
using ResetFn = HRESULT(WINAPI*)(void* device, D3DPresentParameters* presentationParameters);
using SetTransformFn = HRESULT(WINAPI*)(void* device, DWORD state, const void* matrix);
using ClearFn = HRESULT(WINAPI*)(void* device, DWORD rectangleCount, const void* rectangles, DWORD flags, DWORD color, float z, DWORD stencil);
using SetRenderTargetFn = HRESULT(WINAPI*)(void* device, void* colorRenderTarget, void* depthStencilSurface);
using DrawPrimitiveFn = HRESULT(WINAPI*)(void* device, DWORD primitiveType, UINT startVertex, UINT primitiveCount);
using DrawIndexedPrimitiveFn = HRESULT(WINAPI*)(void* device, DWORD primitiveType, UINT minimumVertexIndex, UINT vertexCount, UINT startIndex, UINT primitiveCount);
using DrawPrimitiveUPFn = HRESULT(WINAPI*)(void* device, DWORD primitiveType, UINT primitiveCount, const void* vertexData, UINT vertexStride);
using DrawIndexedPrimitiveUPFn = HRESULT(WINAPI*)(void* device, DWORD primitiveType, UINT minimumVertexIndex, UINT vertexCount, UINT primitiveCount, const void* indexData, UINT indexFormat, const void* vertexData, UINT vertexStride);
using GetRenderTargetFn = HRESULT(WINAPI*)(void* device, void** returnedSurface);
using CreateRenderTargetFn = HRESULT(WINAPI*)(
    void* device,
    UINT width,
    UINT height,
    UINT format,
    UINT multiSampleType,
    BOOL lockable,
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
using GetSurfaceDescriptionFn = HRESULT(WINAPI*)(void* surface, D3DSurfaceDescription* description);
using LockSurfaceRectFn = HRESULT(WINAPI*)(void* surface, D3DLockedRect* lockedRect, const RECT* rectangle, DWORD flags);
using UnlockSurfaceRectFn = HRESULT(WINAPI*)(void* surface);
using ReleaseUnknownFn = ULONG(STDMETHODCALLTYPE*)(void* unknown);
using RenderViewSetTransformationFn = void(__thiscall*)(void* renderView, const void* transformation);

HMODULE g_module = nullptr;
HMODULE g_d3d8To9Module = nullptr;
BFVRD3D8To9GetRuntimeDiagnosticsFn
    g_d3d8To9GetRuntimeDiagnostics = nullptr;
Direct3DCreate8Fn g_originalDirect3DCreate8 = nullptr;
CreateDeviceFn g_originalCreateDevice = nullptr;
volatile LONG g_openXRBootstrapStarted = 0;
volatile LONG g_localPlayerManagerProbeStarted = 0;
volatile LONG g_observedLocalPlayer = 0;
volatile LONG g_activeCameraTransformObserved = 0;
volatile LONG g_sustainedActiveCameraTransformObserved = 0;
volatile LONG g_consecutiveNonZeroCameraTransformSamples = 0;
volatile LONG g_sustainedLocalPlayerAliveObserved = 0;
volatile LONG g_consecutiveLocalPlayerAliveSamples = 0;
volatile LONG g_profiledCameraInterfaceObserved = 0;
volatile LONG g_combinedFrameTraceStarted = 0;
volatile LONG g_skipExtendedFrameDiagnostics = 0;
volatile LONG g_rendererPassTraceStarted = 0;
volatile LONG g_rendererViewSubpassTraceStarted = 0;
volatile LONG g_rendererLayerTraceStarted = 0;
volatile LONG g_frameModelTraceStarted = 0;
volatile LONG g_surfaceDescriptionTraceStarted = 0;
volatile LONG g_direct3D8ObserverInitializationStarted = 0;
volatile LONG g_presentBridgeProbeRequested = 0;
volatile LONG g_surfaceDescriptorProbeRequested = 0;
volatile LONG g_surfaceCopyProbeRequested = 0;
volatile LONG g_surfaceStreamProbeRequested = 0;
volatile LONG g_surfaceResetProbeRequested = 0;
volatile LONG g_surfaceReadbackProbeRequested = 0;
volatile LONG g_surfaceSceneReadbackProbeRequested = 0;
volatile LONG g_surfaceD3D11UploadProbeRequested = 0;
volatile LONG g_renderViewTransformProbeRequested = 0;
volatile LONG g_renderViewTransformProbeStarted = 0;
volatile LONG g_renderViewSetterBaselineProbeRequested = 0;
volatile LONG g_renderViewSetterBaselineProbeStarted = 0;
volatile LONG g_renderViewSingleEyeProbeRequested = 0;
volatile LONG g_renderViewSingleEyeProbeStarted = 0;
volatile LONG g_configuredViewListProbeRequested = 0;
volatile LONG g_configuredViewListProbeStarted = 0;
volatile LONG g_configuredViewListWriterProbeRequested = 0;
volatile LONG g_configuredViewListWriterProbeStarted = 0;
volatile LONG g_sceneBatchProbeRequested = 0;
volatile LONG g_sceneBatchProbeStarted = 0;
volatile LONG g_d3d8CallInventoryProbeRequested = 0;
volatile LONG g_d3d8CallInventoryProbeStarted = 0;
volatile LONG g_d3d8StateCensusProbeRequested = 0;
volatile LONG g_d3d8StateCensusProbeStarted = 0;
DWORD g_loaderPrimaryThreadId = 0;
volatile LONG g_presentBridgeProbeStarted = 0;
volatile LONG g_presentBridgeProbeEnabled = 0;
volatile LONG g_presentBridgeProbeCallCount = 0;
void* g_presentBridgeProbeTarget = nullptr;
PresentFn g_originalPresentForBridgeProbe = nullptr;
void* g_projectionTargetDescriptorProbeTarget = nullptr;
SetTransformFn g_originalSetTransformForDescriptorProbe = nullptr;
void* g_projectionTargetSceneReadbackEndSceneTarget = nullptr;
EndSceneFn g_originalEndSceneForSceneReadbackProbe = nullptr;
void* g_projectionTargetStreamProbeTarget = nullptr;
void* g_projectionTargetStreamResetTarget = nullptr;
SetTransformFn g_originalSetTransformForStreamProbe = nullptr;
ResetFn g_originalResetForStreamProbe = nullptr;
volatile LONG g_presentBridgeProbeFirstThreadId = 0;
volatile LONG g_presentBridgeProbeLastThreadId = 0;
LONGLONG g_presentBridgeProbeFirstCounter = 0;
LONGLONG g_presentBridgeProbeLastCounter = 0;

void AppendLog(const wchar_t* format, ...)
{
    static const bool diagnosticsEnabled =
        bfvr::IsD3D8RuntimeDiagnosticsEnabled(
            bfvr::ReadD3D8RuntimeDiagnosticLevel());
    if (g_module == nullptr || !diagnosticsEnabled)
    {
        return;
    }

    wchar_t modulePath[MAX_PATH] = {};
    if (GetModuleFileNameW(g_module, modulePath, static_cast<DWORD>(std::size(modulePath))) == 0)
    {
        return;
    }

    wchar_t* separator = wcsrchr(modulePath, L'\\');
    if (separator == nullptr)
    {
        return;
    }
    *separator = L'\0';

    wchar_t logDirectory[MAX_PATH] = {};
    if (swprintf_s(logDirectory, std::size(logDirectory), L"%s\\logs", modulePath) < 0)
    {
        return;
    }
    CreateDirectoryW(logDirectory, nullptr);

    wchar_t logPath[MAX_PATH] = {};
    if (swprintf_s(logPath, std::size(logPath), L"%s\\observer.log", logDirectory) < 0)
    {
        return;
    }

    wchar_t message[1024] = {};
    va_list arguments;
    va_start(arguments, format);
    _vsnwprintf_s(message, std::size(message), _TRUNCATE, format, arguments);
    va_end(arguments);

    SYSTEMTIME now = {};
    GetLocalTime(&now);

    wchar_t line[1280] = {};
    if (swprintf_s(
            line,
            std::size(line),
            L"%04u-%02u-%02u %02u:%02u:%02u.%03u [pid:%lu] %s\r\n",
            now.wYear,
            now.wMonth,
            now.wDay,
            now.wHour,
            now.wMinute,
            now.wSecond,
            now.wMilliseconds,
            GetCurrentProcessId(),
            message) < 0)
    {
        return;
    }

    HANDLE file = CreateFileW(
        logPath,
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return;
    }

    const DWORD byteCount = static_cast<DWORD>(wcslen(line) * sizeof(wchar_t));
    DWORD written = 0;
    WriteFile(file, line, byteCount, &written, nullptr);
    CloseHandle(file);
}

DWORD WINAPI ObserveD3D8To9RuntimeDiagnostics(LPVOID)
{
    constexpr DWORD kPollIntervalMs = 100;
    constexpr DWORD kPollCount = 450;
    BFVRD3D8To9RuntimeDiagnostics last = {};

    for (DWORD attempt = 0; attempt < kPollCount; ++attempt)
    {
        BFVRD3D8To9RuntimeDiagnostics diagnostics = {};
        diagnostics.size = sizeof(diagnostics);
        const HRESULT result =
            g_d3d8To9GetRuntimeDiagnostics == nullptr
            ? E_NOINTERFACE
            : g_d3d8To9GetRuntimeDiagnostics(&diagnostics);
        if (FAILED(result) ||
            diagnostics.version !=
                BFVR_D3D8TO9_RUNTIME_DIAGNOSTICS_VERSION)
        {
            AppendLog(
                L"BFVR d3d8to9 runtime diagnostics query failed "
                L"(result=0x%08lX version=%lu).",
                static_cast<unsigned long>(result),
                diagnostics.version);
            return 0;
        }

        last = diagnostics;
        if (diagnostics.resetCalls > 0)
        {
            AppendLog(
                L"BFVR d3d8to9 runtime diagnostics: "
                L"managedTextures=%ld managedVolumeTextures=%ld "
                L"managedCubeTextures=%ld managedVertexBuffers=%ld "
                L"managedIndexBuffers=%ld managedFailures=%ld "
                L"resetCalls=%ld lastResetResult=0x%08lX "
                L"forcedWindowedConversions=%ld primaryPresentation=%ldx%ld "
                L"generation=%ld.",
                diagnostics.managedTextureTranslations,
                diagnostics.managedVolumeTextureTranslations,
                diagnostics.managedCubeTextureTranslations,
                diagnostics.managedVertexBufferTranslations,
                diagnostics.managedIndexBufferTranslations,
                diagnostics.managedTranslationFailures,
                diagnostics.resetCalls,
                static_cast<unsigned long>(
                    diagnostics.lastResetResult),
                diagnostics.forcedWindowedConversions,
                diagnostics.primaryPresentationWidth,
                diagnostics.primaryPresentationHeight,
                diagnostics.primaryPresentationGeneration);
            return 0;
        }
        Sleep(kPollIntervalMs);
    }

    AppendLog(
        L"BFVR d3d8to9 runtime diagnostics timed out before Reset: "
        L"managedTextures=%ld managedVolumeTextures=%ld "
        L"managedCubeTextures=%ld managedVertexBuffers=%ld "
        L"managedIndexBuffers=%ld managedFailures=%ld.",
        last.managedTextureTranslations,
        last.managedVolumeTextureTranslations,
        last.managedCubeTextureTranslations,
        last.managedVertexBufferTranslations,
        last.managedIndexBufferTranslations,
        last.managedTranslationFailures);
    return 0;
}

bool StartD3D8To9RuntimeDiagnosticsObserver()
{
    if (!bfvr::IsD3D8RuntimeDiagnosticsEnabled(
            bfvr::ReadD3D8RuntimeDiagnosticLevel()))
    {
        return true;
    }
    HANDLE thread = CreateThread(
        nullptr,
        0,
        ObserveD3D8To9RuntimeDiagnostics,
        nullptr,
        0,
        nullptr);
    if (thread == nullptr)
    {
        AppendLog(
            L"Could not start the BFVR d3d8to9 runtime diagnostics "
            L"observer (%lu).",
            GetLastError());
        return false;
    }
    CloseHandle(thread);
    return true;
}

constexpr GUID kIUnknownInterfaceId = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
// A complete forwarding IDirect3D8 wrapper avoids modifying the game-owned
// D3D8 object. The virtual method order is the Direct3D 8 COM interface order.
class Direct3D8ObserverProxy
{
public:
    explicit Direct3D8ObserverProxy(void* inner)
        : inner_(inner)
    {
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID requestedInterface, void** returnedInterface)
    {
        if (returnedInterface == nullptr)
        {
            return E_POINTER;
        }
        if (IsEqualIID(requestedInterface, kIUnknownInterfaceId))
        {
            *returnedInterface = this;
            AddRef();
            return S_OK;
        }
        return Method<QueryInterfaceFn>(0)(inner_, requestedInterface, returnedInterface);
    }

    ULONG STDMETHODCALLTYPE AddRef()
    {
        return ++references_;
    }

    ULONG STDMETHODCALLTYPE Release()
    {
        const ULONG references = --references_;
        if (references == 0)
        {
            delete this;
        }
        return references;
    }

    HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void* initializeFunction)
    {
        return Method<RegisterSoftwareDeviceFn>(3)(inner_, initializeFunction);
    }

    UINT STDMETHODCALLTYPE GetAdapterCount()
    {
        return Method<GetAdapterCountFn>(4)(inner_);
    }

    HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT adapter, DWORD flags, void* identifier)
    {
        return Method<GetAdapterIdentifierFn>(5)(inner_, adapter, flags, identifier);
    }

    UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT adapter)
    {
        return Method<GetAdapterModeCountFn>(6)(inner_, adapter);
    }

    HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT adapter, UINT mode, void* displayMode)
    {
        return Method<EnumAdapterModesFn>(7)(inner_, adapter, mode, displayMode);
    }

    HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT adapter, void* displayMode)
    {
        return Method<GetAdapterDisplayModeFn>(8)(inner_, adapter, displayMode);
    }

    HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT adapter, UINT deviceType, UINT adapterFormat, UINT backBufferFormat, BOOL windowed)
    {
        return Method<CheckDeviceTypeFn>(9)(inner_, adapter, deviceType, adapterFormat, backBufferFormat, windowed);
    }

    HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT adapter, UINT deviceType, UINT adapterFormat, DWORD usage, UINT resourceType, UINT checkFormat)
    {
        return Method<CheckDeviceFormatFn>(10)(inner_, adapter, deviceType, adapterFormat, usage, resourceType, checkFormat);
    }

    HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT adapter, UINT deviceType, UINT surfaceFormat, BOOL windowed, UINT multiSampleType)
    {
        return Method<CheckDeviceMultiSampleTypeFn>(11)(inner_, adapter, deviceType, surfaceFormat, windowed, multiSampleType);
    }

    HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT adapter, UINT deviceType, UINT adapterFormat, UINT renderTargetFormat, UINT depthStencilFormat)
    {
        return Method<CheckDepthStencilMatchFn>(12)(inner_, adapter, deviceType, adapterFormat, renderTargetFormat, depthStencilFormat);
    }

    HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT adapter, UINT deviceType, void* capabilities)
    {
        return Method<GetDeviceCapsFn>(13)(inner_, adapter, deviceType, capabilities);
    }

    HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT adapter)
    {
        return Method<GetAdapterMonitorFn>(14)(inner_, adapter);
    }

    HRESULT STDMETHODCALLTYPE CreateDevice(
        UINT adapter,
        UINT deviceType,
        HWND focusWindow,
        DWORD behaviorFlags,
        D3DPresentParameters* presentationParameters,
        void** returnedDevice)
    {
        const HRESULT result = Method<CreateDeviceFn>(15)(
            inner_,
            adapter,
            deviceType,
            focusWindow,
            behaviorFlags,
            presentationParameters,
            returnedDevice);

        const void* device = returnedDevice == nullptr ? nullptr : *returnedDevice;
        if (presentationParameters == nullptr)
        {
            AppendLog(
                L"CreateDevice result=0x%08lX device=%p adapter=%u type=%u behavior=0x%08lX parameters=null",
                static_cast<unsigned long>(result),
                device,
                adapter,
                deviceType,
                static_cast<unsigned long>(behaviorFlags));
            return result;
        }

        AppendLog(
            L"CreateDevice result=0x%08lX device=%p adapter=%u type=%u behavior=0x%08lX size=%ux%u format=%u windowed=%d swap=%u depth=%d depthFormat=%u interval=0x%08lX",
            static_cast<unsigned long>(result),
            device,
            adapter,
            deviceType,
            static_cast<unsigned long>(behaviorFlags),
            presentationParameters->backBufferWidth,
            presentationParameters->backBufferHeight,
            presentationParameters->backBufferFormat,
            presentationParameters->windowed,
            presentationParameters->swapEffect,
            presentationParameters->enableAutoDepthStencil,
            presentationParameters->autoDepthStencilFormat,
            static_cast<unsigned long>(presentationParameters->fullScreenPresentationInterval));
        return result;
    }

private:
    using QueryInterfaceFn = HRESULT(STDMETHODCALLTYPE*)(void*, REFIID, void**);
    using RegisterSoftwareDeviceFn = HRESULT(STDMETHODCALLTYPE*)(void*, void*);
    using GetAdapterCountFn = UINT(STDMETHODCALLTYPE*)(void*);
    using GetAdapterIdentifierFn = HRESULT(STDMETHODCALLTYPE*)(void*, UINT, DWORD, void*);
    using GetAdapterModeCountFn = UINT(STDMETHODCALLTYPE*)(void*, UINT);
    using EnumAdapterModesFn = HRESULT(STDMETHODCALLTYPE*)(void*, UINT, UINT, void*);
    using GetAdapterDisplayModeFn = HRESULT(STDMETHODCALLTYPE*)(void*, UINT, void*);
    using CheckDeviceTypeFn = HRESULT(STDMETHODCALLTYPE*)(void*, UINT, UINT, UINT, UINT, BOOL);
    using CheckDeviceFormatFn = HRESULT(STDMETHODCALLTYPE*)(void*, UINT, UINT, UINT, DWORD, UINT, UINT);
    using CheckDeviceMultiSampleTypeFn = HRESULT(STDMETHODCALLTYPE*)(void*, UINT, UINT, UINT, BOOL, UINT);
    using CheckDepthStencilMatchFn = HRESULT(STDMETHODCALLTYPE*)(void*, UINT, UINT, UINT, UINT, UINT);
    using GetDeviceCapsFn = HRESULT(STDMETHODCALLTYPE*)(void*, UINT, UINT, void*);
    using GetAdapterMonitorFn = HMONITOR(STDMETHODCALLTYPE*)(void*, UINT);

    template <typename Function>
    Function Method(std::size_t index) const
    {
        auto** vtable = *reinterpret_cast<void***>(inner_);
        return reinterpret_cast<Function>(vtable[index]);
    }

    ~Direct3D8ObserverProxy()
    {
        Method<ReleaseFn>(2)(inner_);
    }

    using ReleaseFn = ULONG(STDMETHODCALLTYPE*)(void*);

    void* inner_ = nullptr;
    std::atomic<ULONG> references_ = 1;
};

struct CreateDeviceBreakpointRecord
{
    volatile LONG pending = 0;
    DWORD threadId = 0;
    volatile LONG stage = 0;
    void* target = nullptr;
    DWORD direct3D = 0;
    DWORD adapter = 0;
    DWORD deviceType = 0;
    DWORD focusWindow = 0;
    DWORD behaviorFlags = 0;
    DWORD presentationParameters = 0;
    D3DPresentParameters presentation = {};
    BOOL presentationReadable = FALSE;
    DWORD returnedDevice = 0;
    BOOL stackReadable = FALSE;
    DWORD returnAddress = 0;
    void* device = nullptr;
    void* resetTarget = nullptr;
    void* presentTarget = nullptr;
    void* beginSceneTarget = nullptr;
    void* endSceneTarget = nullptr;
    void* clearTarget = nullptr;
    void* setRenderTargetTarget = nullptr;
    void* setTransformTarget = nullptr;
    BOOL beginSceneObserved = FALSE;
    BOOL endSceneObserved = FALSE;
    BOOL clearObserved = FALSE;
    BOOL setRenderTargetObserved = FALSE;
    BOOL setTransformObserved = FALSE;
    volatile LONG passEventSequence = 0;
    volatile LONG beginSceneSequence = 0;
    volatile LONG endSceneSequence = 0;
    volatile LONG clearSequence = 0;
    volatile LONG setRenderTargetSequence = 0;
    volatile LONG setTransformSequence = 0;
    BOOL presentObserved = FALSE;
    DWORD resetDevice = 0;
    DWORD resetPresentationParameters = 0;
    D3DPresentParameters resetPresentation = {};
    BOOL resetStackReadable = FALSE;
    BOOL resetPresentationReadable = FALSE;
    BOOL resetObserved = FALSE;
    BOOL postResetPresentObserved = FALSE;
    DWORD postResetPresentThreadId = 0;
    volatile LONG postResetPresentSequence = 0;
};

CreateDeviceBreakpointRecord g_createDeviceBreakpoint = {};
PVOID g_createDeviceBreakpointHandler = nullptr;

constexpr DWORD kRenderThreadOwnershipObservationWindowMs = 90000;
constexpr DWORD kRenderThreadOwnershipRescanIntervalMs = 250;
constexpr std::size_t kRenderThreadOwnershipMaximumThreads = 64;

struct RenderThreadOwnershipProbeSlot
{
    volatile LONG armed = 0;
    DWORD threadId = 0;
    BOOL skippedExistingDebugState = FALSE;
    DWORD_PTR originalDr0 = 0;
    DWORD_PTR originalDr1 = 0;
    DWORD_PTR originalDr2 = 0;
    DWORD_PTR originalDr3 = 0;
    DWORD_PTR originalDr6 = 0;
    DWORD_PTR originalDr7 = 0;
};

struct RenderThreadOwnershipProbeRecord
{
    volatile LONG state = 0;
    void* target = nullptr;
    BOOL targetObserved = FALSE;
    DWORD observedThreadId = 0;
    LONG preservedThreadCount = 0;
    LONG unavailableThreadCount = 0;
    LONG capacitySkippedThreadCount = 0;
    RenderThreadOwnershipProbeSlot slots[kRenderThreadOwnershipMaximumThreads] = {};
};

RenderThreadOwnershipProbeRecord g_renderThreadOwnershipProbe = {};
PVOID g_renderThreadOwnershipProbeHandler = nullptr;

void RestoreRenderThreadOwnershipProbeContext(
    CONTEXT* context,
    const RenderThreadOwnershipProbeSlot& slot)
{
    context->Dr0 = slot.originalDr0;
    context->Dr1 = slot.originalDr1;
    context->Dr2 = slot.originalDr2;
    context->Dr3 = slot.originalDr3;
    context->Dr6 = slot.originalDr6;
    context->Dr7 = slot.originalDr7;
}

LONG CALLBACK HandleRenderThreadOwnershipProbe(EXCEPTION_POINTERS* exceptionPointers)
{
    if (exceptionPointers == nullptr || exceptionPointers->ExceptionRecord == nullptr || exceptionPointers->ContextRecord == nullptr ||
        exceptionPointers->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    CONTEXT* context = exceptionPointers->ContextRecord;
    if (context->Eip != reinterpret_cast<DWORD_PTR>(g_renderThreadOwnershipProbe.target))
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    const DWORD currentThreadId = GetCurrentThreadId();
    for (auto& slot : g_renderThreadOwnershipProbe.slots)
    {
        if (slot.threadId != currentThreadId || InterlockedCompareExchange(&slot.armed, 0, 0) == 0)
        {
            continue;
        }

        RestoreRenderThreadOwnershipProbeContext(context, slot);
        InterlockedExchange(&slot.armed, 0);
        if (InterlockedCompareExchange(&g_renderThreadOwnershipProbe.state, 2, 1) == 1)
        {
            g_renderThreadOwnershipProbe.observedThreadId = currentThreadId;
            g_renderThreadOwnershipProbe.targetObserved = TRUE;
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

RenderThreadOwnershipProbeSlot* FindRenderThreadOwnershipProbeSlot(DWORD threadId)
{
    for (auto& slot : g_renderThreadOwnershipProbe.slots)
    {
        if (slot.threadId == threadId)
        {
            return &slot;
        }
    }
    return nullptr;
}

RenderThreadOwnershipProbeSlot* ReserveRenderThreadOwnershipProbeSlot()
{
    for (auto& slot : g_renderThreadOwnershipProbe.slots)
    {
        if (slot.threadId == 0)
        {
            return &slot;
        }
    }
    return nullptr;
}

void RestoreRenderThreadOwnershipProbeSlot(RenderThreadOwnershipProbeSlot& slot)
{
    if (InterlockedCompareExchange(&slot.armed, 0, 0) == 0)
    {
        return;
    }

    HANDLE thread = OpenThread(
        THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
        FALSE,
        slot.threadId);
    if (thread == nullptr)
    {
        InterlockedExchange(&slot.armed, 0);
        return;
    }

    if (SuspendThread(thread) == static_cast<DWORD>(-1))
    {
        CloseHandle(thread);
        return;
    }

    CONTEXT context = {};
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(thread, &context))
    {
        const bool probeStateIsStillOwned =
            context.Dr0 == reinterpret_cast<DWORD_PTR>(g_renderThreadOwnershipProbe.target) &&
            (context.Dr7 & 0x000F0003) == 1;
        if (probeStateIsStillOwned)
        {
            RestoreRenderThreadOwnershipProbeContext(&context, slot);
            SetThreadContext(thread, &context);
        }
    }
    ResumeThread(thread);
    CloseHandle(thread);
    InterlockedExchange(&slot.armed, 0);
}

void CleanupRenderThreadOwnershipProbe()
{
    for (auto& slot : g_renderThreadOwnershipProbe.slots)
    {
        RestoreRenderThreadOwnershipProbeSlot(slot);
    }
}

void ArmRenderThreadOwnershipProbeOnThread(DWORD threadId)
{
    if (threadId == GetCurrentThreadId() || FindRenderThreadOwnershipProbeSlot(threadId) != nullptr)
    {
        return;
    }

    HANDLE thread = OpenThread(
        THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
        FALSE,
        threadId);
    if (thread == nullptr)
    {
        InterlockedIncrement(&g_renderThreadOwnershipProbe.unavailableThreadCount);
        return;
    }

    if (SuspendThread(thread) == static_cast<DWORD>(-1))
    {
        CloseHandle(thread);
        InterlockedIncrement(&g_renderThreadOwnershipProbe.unavailableThreadCount);
        return;
    }

    CONTEXT context = {};
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (!GetThreadContext(thread, &context))
    {
        ResumeThread(thread);
        CloseHandle(thread);
        InterlockedIncrement(&g_renderThreadOwnershipProbe.unavailableThreadCount);
        return;
    }

    RenderThreadOwnershipProbeSlot* slot = ReserveRenderThreadOwnershipProbeSlot();
    if (slot == nullptr)
    {
        ResumeThread(thread);
        CloseHandle(thread);
        InterlockedIncrement(&g_renderThreadOwnershipProbe.capacitySkippedThreadCount);
        return;
    }

    slot->threadId = threadId;
    if ((context.Dr7 & 0xFF) != 0)
    {
        slot->skippedExistingDebugState = TRUE;
        InterlockedIncrement(&g_renderThreadOwnershipProbe.preservedThreadCount);
        ResumeThread(thread);
        CloseHandle(thread);
        return;
    }

    slot->originalDr0 = context.Dr0;
    slot->originalDr1 = context.Dr1;
    slot->originalDr2 = context.Dr2;
    slot->originalDr3 = context.Dr3;
    slot->originalDr6 = context.Dr6;
    slot->originalDr7 = context.Dr7;
    context.Dr0 = reinterpret_cast<DWORD_PTR>(g_renderThreadOwnershipProbe.target);
    context.Dr6 = 0;
    context.Dr7 = (context.Dr7 & ~static_cast<DWORD_PTR>(0x000F0003)) | 1;
    if (!SetThreadContext(thread, &context))
    {
        slot->threadId = 0;
        ResumeThread(thread);
        CloseHandle(thread);
        InterlockedIncrement(&g_renderThreadOwnershipProbe.unavailableThreadCount);
        return;
    }

    InterlockedExchange(&slot->armed, 1);
    ResumeThread(thread);
    CloseHandle(thread);
}

void ArmRenderThreadOwnershipProbeOnExistingThreads()
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        InterlockedIncrement(&g_renderThreadOwnershipProbe.unavailableThreadCount);
        return;
    }

    THREADENTRY32 entry = {};
    entry.dwSize = sizeof(entry);
    if (Thread32First(snapshot, &entry))
    {
        do
        {
            if (entry.th32OwnerProcessID == GetCurrentProcessId())
            {
                ArmRenderThreadOwnershipProbeOnThread(entry.th32ThreadID);
            }
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
}

DWORD WINAPI ReportRenderThreadOwnershipProbe(void*)
{
    constexpr DWORD kResetWaitWindowMs = 10000;
    const DWORD resetWaitStartedAt = GetTickCount();
    while (!g_createDeviceBreakpoint.resetObserved && GetTickCount() - resetWaitStartedAt < kResetWaitWindowMs)
    {
        Sleep(10);
    }

    if (!g_createDeviceBreakpoint.resetObserved || g_createDeviceBreakpoint.presentTarget == nullptr)
    {
        AppendLog(L"Render-thread ownership probe skipped because the observed D3D8 Reset/Present boundary was unavailable.");
        return 0;
    }

    if (g_renderThreadOwnershipProbeHandler == nullptr)
    {
        g_renderThreadOwnershipProbeHandler = AddVectoredExceptionHandler(1, HandleRenderThreadOwnershipProbe);
        if (g_renderThreadOwnershipProbeHandler == nullptr)
        {
            AppendLog(L"Render-thread ownership probe skipped: AddVectoredExceptionHandler failed (%lu).", GetLastError());
            return 0;
        }
    }

    g_renderThreadOwnershipProbe = {};
    g_renderThreadOwnershipProbe.target = g_createDeviceBreakpoint.presentTarget;
    InterlockedExchange(&g_renderThreadOwnershipProbe.state, 1);

    const DWORD startedAt = GetTickCount();
    while (GetTickCount() - startedAt < kRenderThreadOwnershipObservationWindowMs)
    {
        if (InterlockedCompareExchange(&g_renderThreadOwnershipProbe.state, 0, 0) == 2)
        {
            break;
        }
        ArmRenderThreadOwnershipProbeOnExistingThreads();
        Sleep(kRenderThreadOwnershipRescanIntervalMs);
    }

    CleanupRenderThreadOwnershipProbe();
    if (g_renderThreadOwnershipProbe.targetObserved)
    {
        AppendLog(
            L"Render-thread ownership probe observed Present target=%p on thread %lu; cleanup restored every remaining thread whose debug state still matched this probe.",
            g_renderThreadOwnershipProbe.target,
            g_renderThreadOwnershipProbe.observedThreadId);
    }
    else
    {
        AppendLog(
            L"Render-thread ownership probe did not observe Present target=%p on another existing thread in 90 seconds (preservedDebugState=%ld unavailable=%ld capacity=%ld).",
            g_renderThreadOwnershipProbe.target,
            InterlockedCompareExchange(&g_renderThreadOwnershipProbe.preservedThreadCount, 0, 0),
            InterlockedCompareExchange(&g_renderThreadOwnershipProbe.unavailableThreadCount, 0, 0),
            InterlockedCompareExchange(&g_renderThreadOwnershipProbe.capacitySkippedThreadCount, 0, 0));
    }
    return 0;
}

void StartRenderThreadOwnershipProbe()
{
    HANDLE reporter = CreateThread(nullptr, 0, ReportRenderThreadOwnershipProbe, nullptr, 0, nullptr);
    if (reporter == nullptr)
    {
        AppendLog(L"Render-thread ownership probe could not start (%lu).", GetLastError());
        return;
    }
    CloseHandle(reporter);
}

constexpr LONG kCombinedPresentSampleMaximum = 8;
constexpr LONG kCombinedTransformSampleMaximum = 12;

struct CombinedPresentSample
{
    DWORD tick = 0;
    LONGLONG performanceCounter = 0;
};

struct CombinedTransformSample
{
    DWORD tick = 0;
    DWORD ecx = 0;
    DWORD edx = 0;
    DWORD stackWords[7] = {};
    DWORD transformState = 0;
    DWORD matrixAddress = 0;
    BOOL stackReadable = FALSE;
    BOOL matrixReadable = FALSE;
    float matrix[16] = {};
};

struct RendererTransformCacheSample
{
    DWORD tick = 0;
    DWORD rendererTransaction = 0;
    BOOL readable = FALSE;
    float world[16] = {};
    float view[16] = {};
    float projection[16] = {};
};

struct LocalCameraTransformSample
{
    DWORD tick = 0;
    DWORD localPlayer = 0;
    DWORD cameraInterface = 0;
    BOOL readable = FALSE;
    float matrix[16] = {};
};

struct CombinedFrameTraceRecord
{
    volatile LONG state = 0;
    DWORD threadId = 0;
    void* presentTarget = nullptr;
    void* setTransformTarget = nullptr;
    const std::byte* rendererTransactionStateGlobal = nullptr;
    const void* localCameraAddRefTarget = nullptr;
    void* beginSceneTarget = nullptr;
    void* endSceneTarget = nullptr;
    void* clearTarget = nullptr;
    void* setRenderTargetTarget = nullptr;
    DWORD_PTR originalDr0 = 0;
    DWORD_PTR originalDr1 = 0;
    DWORD_PTR originalDr2 = 0;
    DWORD_PTR originalDr3 = 0;
    DWORD_PTR originalDr6 = 0;
    DWORD_PTR originalDr7 = 0;
    LONGLONG performanceCounterFrequency = 0;
    volatile LONG presentSamplesCaptured = 0;
    volatile LONG transformSamplesCaptured = 0;
    volatile LONG scenePhase = 0;
    BOOL projectionBoundaryCaptured = FALSE;
    BOOL setRenderTargetObserved = FALSE;
    DWORD setRenderTargetTick = 0;
    DWORD setRenderTargetReturnAddress = 0;
    DWORD setRenderTargetColor = 0;
    DWORD setRenderTargetDepth = 0;
    BOOL setRenderTargetStackReadable = FALSE;
    DWORD setRenderTargetColorVtable = 0;
    DWORD setRenderTargetColorGetDescTarget = 0;
    BOOL setRenderTargetColorInterfaceReadable = FALSE;
    BOOL cleanupRestored = FALSE;
    CombinedPresentSample presentSamples[kCombinedPresentSampleMaximum] = {};
    CombinedTransformSample transformSamples[kCombinedTransformSampleMaximum] = {};
    RendererTransformCacheSample rendererTransformCachesAtProjection = {};
    RendererTransformCacheSample rendererTransformCachesAtTraceEnd = {};
    LocalCameraTransformSample localCameraAtProjection = {};
};

CombinedFrameTraceRecord g_combinedFrameTrace = {};
PVOID g_combinedFrameTraceHandler = nullptr;

struct SurfaceDescriptionTraceRecord
{
    volatile LONG state = 0;
    DWORD threadId = 0;
    void* target = nullptr;
    DWORD surface = 0;
    DWORD returnAddress = 0;
    DWORD descriptionAddress = 0;
    DWORD_PTR originalDr0 = 0;
    DWORD_PTR originalDr6 = 0;
    DWORD_PTR originalDr7 = 0;
    BOOL descriptionReadable = FALSE;
    BOOL cleanupRestored = FALSE;
    D3DSurfaceDescription description = {};
};

SurfaceDescriptionTraceRecord g_surfaceDescriptionTrace = {};
PVOID g_surfaceDescriptionTraceHandler = nullptr;

struct ProjectionTargetDescriptorProbeRecord
{
    // 0=waiting for the exact ordinary-world Projection call, 1=executing on
    // the device thread, 2=complete.
    volatile LONG state = 0;
    DWORD executionThreadId = 0;
    DWORD transformState = 0;
    void* callerReturnAddress = nullptr;
    DWORD externalCallerReturnAddress = 0;
    BOOL externalStackReadable = FALSE;
    HRESULT setTransformResult = E_FAIL;
    HRESULT getRenderTargetResult = E_FAIL;
    void* acquiredSurface = nullptr;
    BOOL sourceReferenceOutstanding = FALSE;
    void* getDescTarget = nullptr;
    HRESULT getDescResult = E_FAIL;
    BOOL releaseCalled = FALSE;
    ULONG releaseResult = 0;
    BOOL descriptionReadable = FALSE;
    BOOL copyRequested = FALSE;
    BOOL readbackRequested = FALSE;
    BOOL readbackAfterEndScene = FALSE;
    BOOL d3d11UploadRequested = FALSE;
    BOOL copySafetyGatePassed = FALSE;
    void* createRenderTargetTarget = nullptr;
    BOOL createRenderTargetInSystemD3D8 = FALSE;
    HRESULT createRenderTargetResult = E_FAIL;
    void* ownedSurface = nullptr;
    BOOL ownedReferenceOutstanding = FALSE;
    void* ownedGetDescTarget = nullptr;
    HRESULT ownedGetDescResult = E_FAIL;
    BOOL ownedDescriptionReadable = FALSE;
    BOOL ownedDescriptionMatchesSource = FALSE;
    D3DSurfaceDescription ownedDescription = {};
    void* copyRectsTarget = nullptr;
    BOOL copyRectsInSystemD3D8 = FALSE;
    BOOL copyAttempted = FALSE;
    HRESULT copyRectsResult = E_FAIL;
    BOOL ownedReleaseCalled = FALSE;
    ULONG ownedReleaseResult = 0;
    void* createImageSurfaceTarget = nullptr;
    BOOL createImageSurfaceInSystemD3D8 = FALSE;
    HRESULT createImageSurfaceResult = E_FAIL;
    void* readbackSurface = nullptr;
    BOOL readbackReferenceOutstanding = FALSE;
    void* readbackGetDescTarget = nullptr;
    HRESULT readbackGetDescResult = E_FAIL;
    BOOL readbackDescriptionReadable = FALSE;
    BOOL readbackDescriptionMatchesSource = FALSE;
    D3DSurfaceDescription readbackDescription = {};
    HRESULT readbackCopyRectsResult = E_FAIL;
    BOOL readbackCopyAttempted = FALSE;
    void* readbackLockRectTarget = nullptr;
    void* readbackUnlockRectTarget = nullptr;
    BOOL readbackLockRectInSystemD3D8 = FALSE;
    BOOL readbackUnlockRectInSystemD3D8 = FALSE;
    HRESULT readbackLockRectResult = E_FAIL;
    HRESULT readbackUnlockRectResult = E_FAIL;
    LONG readbackPitch = 0;
    BOOL readbackPixelsReadable = FALSE;
    DWORD readbackPixels[5] = {};
    DWORD readbackNonZeroPixelCount = 0;
    // The bridge accepts only the observed D3DFMT_X8R8G8B8 (22) source and
    // creates a transient, BFVR-owned DXGI_FORMAT_B8G8R8X8_UNORM texture.
    BOOL d3d11FormatMappingAccepted = FALSE;
    HRESULT d3d11CreateDeviceResult = E_FAIL;
    D3D_FEATURE_LEVEL d3d11FeatureLevel = D3D_FEATURE_LEVEL_9_1;
    HRESULT d3d11CreateTextureResult = E_FAIL;
    HRESULT d3d11CreateStagingTextureResult = E_FAIL;
    BOOL d3d11UploadAttempted = FALSE;
    HRESULT d3d11MapResult = E_FAIL;
    LONG d3d11ReadbackPitch = 0;
    BOOL d3d11PixelsMatchSource = FALSE;
    DWORD d3d11VerificationPixels[5] = {};
    BOOL d3d11ResourcesReleased = FALSE;
    BOOL readbackReleaseCalled = FALSE;
    ULONG readbackReleaseResult = 0;
    BOOL resetSafeAtReturn = FALSE;
    BOOL structuredException = FALSE;
    D3DSurfaceDescription description = {};
};

ProjectionTargetDescriptorProbeRecord g_projectionTargetDescriptorProbe = {};

struct ProjectionTargetSceneReadbackArm
{
    // This borrows the device pointer only between the returning Projection
    // submission and the next EndScene on the same device thread. It neither
    // AddRefs nor retains any game-owned object.
    volatile LONG pending = 0;
    void* device = nullptr;
    DWORD transformState = 0;
    void* callerReturnAddress = nullptr;
    DWORD externalCallerReturnAddress = 0;
    BOOL externalStackReadable = FALSE;
    HRESULT setTransformResult = E_FAIL;
    DWORD threadId = 0;
};

ProjectionTargetSceneReadbackArm g_projectionTargetSceneReadbackArm = {};

enum ProjectionTargetStreamCompletionReason : DWORD
{
    kProjectionTargetStreamCompletionNone = 0,
    kProjectionTargetStreamCompletionCopyLimitReached = 1,
    kProjectionTargetStreamCompletionSourceUnavailable = 2,
    kProjectionTargetStreamCompletionSourceRejected = 3,
    kProjectionTargetStreamCompletionOwnedTargetFailure = 4,
    kProjectionTargetStreamCompletionCopyFailure = 5,
    kProjectionTargetStreamCompletionResetFailure = 6,
    kProjectionTargetStreamCompletionWorkerTimeout = 7,
    kProjectionTargetStreamCompletionUnexpectedThread = 8,
    kProjectionTargetStreamCompletionResetRecreated = 9
};

struct ProjectionTargetStreamProbeRecord
{
    // 0=waiting, 1=active on the D3D device thread, 2=fully released and
    // complete. The target is BFVR-owned only while state is 1.
    volatile LONG state = 0;
    volatile LONG stopRequested = 0;
    BOOL resetProbeRequested = FALSE;
    BOOL resetObservedWithOwnedTarget = FALSE;
    BOOL awaitingPostResetRecreation = FALSE;
    BOOL postResetRecreationVerified = FALSE;
    LONG copySuccessCountAtReset = 0;
    DWORD firstExecutionThreadId = 0;
    DWORD lastExecutionThreadId = 0;
    DWORD transformState = 0;
    void* callerReturnAddress = nullptr;
    DWORD externalCallerReturnAddress = 0;
    BOOL externalStackReadable = FALSE;
    void* createRenderTargetTarget = nullptr;
    void* copyRectsTarget = nullptr;
    BOOL createRenderTargetInSystemD3D8 = FALSE;
    BOOL copyRectsInSystemD3D8 = FALSE;
    void* ownedSurface = nullptr;
    BOOL ownedReferenceOutstanding = FALSE;
    D3DSurfaceDescription ownedDescription = {};
    BOOL ownedDescriptionReadable = FALSE;
    LONG ownedCreateCount = 0;
    LONG ownedReleaseCount = 0;
    ULONG lastOwnedReleaseResult = 0;
    LONG sourceAcquireCount = 0;
    LONG sourceReleaseCount = 0;
    BOOL sourceReferenceOutstanding = FALSE;
    ULONG lastSourceReleaseResult = 0;
    HRESULT lastGetRenderTargetResult = E_FAIL;
    HRESULT lastGetDescResult = E_FAIL;
    HRESULT lastCreateRenderTargetResult = E_FAIL;
    HRESULT lastOwnedGetDescResult = E_FAIL;
    HRESULT lastCopyRectsResult = E_FAIL;
    D3DSurfaceDescription lastSourceDescription = {};
    BOOL lastSourceDescriptionReadable = FALSE;
    BOOL lastSourceSafetyGatePassed = FALSE;
    LONG copyAttemptCount = 0;
    LONG copySuccessCount = 0;
    LONG resetHookCallCount = 0;
    LONG resetReleaseCount = 0;
    LONG resetSuccessCount = 0;
    HRESULT lastResetResult = E_FAIL;
    BOOL resetParametersCaptured = FALSE;
    BOOL resetSafeAtReturn = FALSE;
    DWORD completionReason = kProjectionTargetStreamCompletionNone;
    BOOL structuredException = FALSE;
};

ProjectionTargetStreamProbeRecord g_projectionTargetStreamProbe = {};

struct RendererPassTraceEvent
{
    DWORD tick = 0;
    LONGLONG performanceCounter = 0;
    DWORD instruction = 0;
    DWORD ecx = 0;
    DWORD stackReturnAddress = 0;
    BOOL stackReadable = FALSE;
};

struct RendererPassTraceRecord
{
    volatile LONG state = 0;
    DWORD threadId = 0;
    void* frameCoordinatorTarget = nullptr;
    void* rendererCoordinatorTarget = nullptr;
    void* renderViewTarget = nullptr;
    void* renderViewReturnTarget = nullptr;
    DWORD_PTR originalDr0 = 0;
    DWORD_PTR originalDr1 = 0;
    DWORD_PTR originalDr2 = 0;
    DWORD_PTR originalDr3 = 0;
    DWORD_PTR originalDr6 = 0;
    DWORD_PTR originalDr7 = 0;
    LONGLONG performanceCounterFrequency = 0;
    volatile LONG frameCoordinatorObserved = 0;
    volatile LONG rendererCoordinatorObserved = 0;
    volatile LONG renderViewEntryObserved = 0;
    volatile LONG renderViewReturnObserved = 0;
    BOOL cleanupRestored = FALSE;
    RendererPassTraceEvent frameCoordinator = {};
    RendererPassTraceEvent rendererCoordinator = {};
    RendererPassTraceEvent renderViewEntry = {};
    RendererPassTraceEvent renderViewReturn = {};
};

RendererPassTraceRecord g_rendererPassTrace = {};
PVOID g_rendererPassTraceHandler = nullptr;

struct RendererViewSubpassEvent
{
    DWORD tick = 0;
    LONGLONG performanceCounter = 0;
    DWORD instruction = 0;
    DWORD ecx = 0;
    DWORD vtable = 0;
    DWORD callTarget = 0;
    DWORD stackReturnAddress = 0;
    BOOL readable = FALSE;
};

struct RendererViewSubpassTraceRecord
{
    volatile LONG state = 0;
    DWORD threadId = 0;
    void* primaryObjectSubmitTarget = nullptr;
    void* primaryRendererStageTarget = nullptr;
    void* secondaryRendererStageTarget = nullptr;
    void* trailingRendererStageTarget = nullptr;
    DWORD_PTR originalDr0 = 0;
    DWORD_PTR originalDr1 = 0;
    DWORD_PTR originalDr2 = 0;
    DWORD_PTR originalDr3 = 0;
    DWORD_PTR originalDr6 = 0;
    DWORD_PTR originalDr7 = 0;
    LONGLONG performanceCounterFrequency = 0;
    volatile LONG primaryObjectSubmitObserved = 0;
    volatile LONG primaryRendererStageObserved = 0;
    volatile LONG secondaryRendererStageObserved = 0;
    volatile LONG trailingRendererStageObserved = 0;
    BOOL cleanupRestored = FALSE;
    RendererViewSubpassEvent primaryObjectSubmit = {};
    RendererViewSubpassEvent primaryRendererStage = {};
    RendererViewSubpassEvent secondaryRendererStage = {};
    RendererViewSubpassEvent trailingRendererStage = {};
};

RendererViewSubpassTraceRecord g_rendererViewSubpassTrace = {};
PVOID g_rendererViewSubpassTraceHandler = nullptr;

struct RendererLayerTraceEvent
{
    DWORD tick = 0;
    LONGLONG performanceCounter = 0;
    DWORD instruction = 0;
    DWORD ecx = 0;
    DWORD stackReturnAddress = 0;
    BOOL stackReadable = FALSE;
};

struct RendererLayerTraceRecord
{
    volatile LONG state = 0;
    DWORD threadId = 0;
    void* renderViewTarget = nullptr;
    void* renderViewLoopExitTarget = nullptr;
    void* postViewCallbackListTarget = nullptr;
    void* textOverlayQueueTarget = nullptr;
    DWORD_PTR originalDr0 = 0;
    DWORD_PTR originalDr1 = 0;
    DWORD_PTR originalDr2 = 0;
    DWORD_PTR originalDr3 = 0;
    DWORD_PTR originalDr6 = 0;
    DWORD_PTR originalDr7 = 0;
    volatile LONG renderViewObserved = 0;
    volatile LONG renderViewLoopExitObserved = 0;
    volatile LONG postViewCallbackListObserved = 0;
    volatile LONG textOverlayQueueObserved = 0;
    BOOL cleanupRestored = FALSE;
    RendererLayerTraceEvent renderView = {};
    RendererLayerTraceEvent renderViewLoopExit = {};
    RendererLayerTraceEvent postViewCallbackList = {};
    RendererLayerTraceEvent textOverlayQueue = {};
};

RendererLayerTraceRecord g_rendererLayerTrace = {};
PVOID g_rendererLayerTraceHandler = nullptr;

// The earlier traces prove reachability of individual render boundaries, but
// deliberately disarm after their first hit.  This separate, short-lived
// recorder leaves the original code and D3D8 interfaces untouched while it
// samples their temporal relationship to Present on the known device thread.
constexpr LONG kFrameModelTraceEventMaximum = 512;
constexpr LONG kFrameModelTraceEventsPerKindMaximum =
    kFrameModelTraceEventMaximum / 4;

enum class FrameModelEventKind : DWORD
{
    FrameCoordinator,
    RendererCoordinator,
    RenderView,
    Present
};

struct FrameModelTraceEvent
{
    LONGLONG performanceCounter = 0;
    FrameModelEventKind kind = FrameModelEventKind::FrameCoordinator;
};

struct FrameModelTraceRecord
{
    volatile LONG state = 0;
    DWORD threadId = 0;
    void* frameCoordinatorTarget = nullptr;
    void* rendererCoordinatorTarget = nullptr;
    void* renderViewTarget = nullptr;
    void* presentTarget = nullptr;
    DWORD_PTR originalDr0 = 0;
    DWORD_PTR originalDr1 = 0;
    DWORD_PTR originalDr2 = 0;
    DWORD_PTR originalDr3 = 0;
    DWORD_PTR originalDr6 = 0;
    DWORD_PTR originalDr7 = 0;
    LONGLONG performanceCounterFrequency = 0;
    volatile LONG eventsCaptured = 0;
    volatile LONG droppedEvents = 0;
    volatile LONG frameCoordinatorEvents = 0;
    volatile LONG rendererCoordinatorEvents = 0;
    volatile LONG renderViewEvents = 0;
    volatile LONG presentEvents = 0;
    volatile LONG frameCoordinatorStored = 0;
    volatile LONG rendererCoordinatorStored = 0;
    volatile LONG renderViewStored = 0;
    volatile LONG presentStored = 0;
    volatile LONG suppressedEvents = 0;
    volatile LONG capacityReached = 0;
    BOOL cleanupRestored = FALSE;
    FrameModelTraceEvent events[kFrameModelTraceEventMaximum] = {};
};

FrameModelTraceRecord g_frameModelTrace = {};
PVOID g_frameModelTraceHandler = nullptr;

void CaptureLocalCameraTransformAtProjection(LocalCameraTransformSample& sample, const void* expectedAddRef);

void CaptureRendererTransformCaches(RendererTransformCacheSample& sample, const std::byte* rendererTransactionStateGlobal)
{
    sample.tick = GetTickCount();
    if (rendererTransactionStateGlobal == nullptr)
    {
        return;
    }

    __try
    {
        const auto* rendererTransaction =
            *reinterpret_cast<const std::byte* const*>(rendererTransactionStateGlobal);
        if (rendererTransaction != nullptr)
        {
            sample.rendererTransaction = static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(rendererTransaction));
            std::memcpy(sample.world, rendererTransaction + kRendererWorldTransformOffset, sizeof(sample.world));
            std::memcpy(sample.view, rendererTransaction + kRendererViewTransformOffset, sizeof(sample.view));
            std::memcpy(sample.projection, rendererTransaction + kRendererProjectionTransformOffset, sizeof(sample.projection));
            sample.readable = TRUE;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        sample.readable = FALSE;
    }
}

void AppendRendererTransformCache(const wchar_t* label, const float* matrix)
{
    AppendLog(
        L"Map-gated renderer cache %s m=[%.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f].",
        label,
        static_cast<double>(matrix[0]), static_cast<double>(matrix[1]), static_cast<double>(matrix[2]), static_cast<double>(matrix[3]),
        static_cast<double>(matrix[4]), static_cast<double>(matrix[5]), static_cast<double>(matrix[6]), static_cast<double>(matrix[7]),
        static_cast<double>(matrix[8]), static_cast<double>(matrix[9]), static_cast<double>(matrix[10]), static_cast<double>(matrix[11]),
        static_cast<double>(matrix[12]), static_cast<double>(matrix[13]), static_cast<double>(matrix[14]), static_cast<double>(matrix[15]));
}

void AppendLocalCameraTransformSample(const LocalCameraTransformSample& sample)
{
    AppendLog(
        L"Map-gated local world::Camera at state-3 entry readable=%d player=%08lX camera=%08lX m=[%.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f].",
        sample.readable,
        static_cast<unsigned long>(sample.localPlayer),
        static_cast<unsigned long>(sample.cameraInterface),
        static_cast<double>(sample.matrix[0]), static_cast<double>(sample.matrix[1]), static_cast<double>(sample.matrix[2]), static_cast<double>(sample.matrix[3]),
        static_cast<double>(sample.matrix[4]), static_cast<double>(sample.matrix[5]), static_cast<double>(sample.matrix[6]), static_cast<double>(sample.matrix[7]),
        static_cast<double>(sample.matrix[8]), static_cast<double>(sample.matrix[9]), static_cast<double>(sample.matrix[10]), static_cast<double>(sample.matrix[11]),
        static_cast<double>(sample.matrix[12]), static_cast<double>(sample.matrix[13]), static_cast<double>(sample.matrix[14]), static_cast<double>(sample.matrix[15]));
}


// Legacy diagnostic families share this translation unit so their existing
// vectored-exception and hook state retains identical lifetime and ordering.
#include "client/internal/BFVRClientViewDiagnostics.inl"
#include "client/internal/BFVRClientFrameDiagnostics.inl"
#include "client/internal/BFVRClientSurfaceDiagnostics.inl"
#include "client/internal/BFVRClientEngineDiagnostics.inl"


bool InstallDirect3D8ImportRoute(
    Direct3DCreate8Fn direct3DCreate8Override = nullptr,
    bool observeOverride = false)
{
    const Direct3DCreate8Fn replacement =
        direct3DCreate8Override != nullptr && !observeOverride
        ? direct3DCreate8Override
        : &HookDirect3DCreate8;
    const bfvr::D3D8ImportRouteResult route =
        bfvr::RouteExecutableDirect3DCreate8(replacement);
    if (!route.routed)
    {
        return false;
    }
    if (observeOverride)
    {
        g_originalDirect3DCreate8 = direct3DCreate8Override;
    }
    else if (direct3DCreate8Override == nullptr)
    {
        g_originalDirect3DCreate8 = route.previous;
    }
    return direct3DCreate8Override != nullptr ||
        g_originalDirect3DCreate8 != nullptr;
}
} // namespace

// The loader invokes this only after LoadLibrary has returned in the target.
// Keeping the IAT write out of DllMain avoids mutating BF1942 while its loader
// lock is held.
extern "C" __declspec(dllexport) DWORD WINAPI BFVRInitializeObserver(LPVOID initializerParameter)
{
    if (g_module == nullptr)
    {
        return 0;
    }

    if (InterlockedCompareExchange(&g_direct3D8ObserverInitializationStarted, 1, 0) != 0)
    {
        return 1;
    }

    wchar_t payloadDirectory[MAX_PATH] = {};
    const DWORD modulePathLength = GetModuleFileNameW(
        g_module,
        payloadDirectory,
        static_cast<DWORD>(std::size(payloadDirectory)));
    if (modulePathLength > 0 && modulePathLength < std::size(payloadDirectory))
    {
        wchar_t* const separator = wcsrchr(payloadDirectory, L'\\');
        if (separator != nullptr)
        {
            *separator = L'\0';
        }
        else
        {
            payloadDirectory[0] = L'\0';
        }
    }
    else
    {
        payloadDirectory[0] = L'\0';
    }
    const bfvr::settings::UserSettingsLoadStatus userSettingsStatus =
        bfvr::settings::ProcessUserSettingsRuntime().Initialize(
            payloadDirectory[0] == L'\0' ? nullptr : payloadDirectory);
    AppendLog(
        L"BFVR game-client startup user configuration selected %s at %s.",
        bfvr::settings::UserSettingsLoadStatusName(userSettingsStatus),
        bfvr::settings::ProcessUserSettingsRuntime().Store().Path().empty()
            ? L"<unavailable path>"
            : bfvr::settings::ProcessUserSettingsRuntime()
                  .Store()
                  .Path()
                  .c_str());

    struct ObserverInitializationParameters
    {
        DWORD magic;
        DWORD size;
        ULONG_PTR request;
        DWORD primaryThreadId;
    };

    ULONG_PTR initializationRequest = reinterpret_cast<ULONG_PTR>(initializerParameter);
    __try
    {
        const auto* const parameters =
            static_cast<const ObserverInitializationParameters*>(initializerParameter);
        if (parameters != nullptr &&
            parameters->magic == kObserverInitializationParametersMagic &&
            parameters->size == sizeof(ObserverInitializationParameters))
        {
            initializationRequest = parameters->request;
            g_loaderPrimaryThreadId = parameters->primaryThreadId;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // Preserve the legacy small-integer request ABI for older loaders.
    }

    // This diagnostic hooks two game-owned soldier updates only.  Do not route
    // Direct3DCreate8 or start any inherited graphics/OpenXR diagnostics: those
    // paths are unrelated to native arm ownership and would make the result
    // neither isolated nor useful.
    if (initializationRequest == kObserverInitializationRequestFirstPersonArmProbe)
    {
        bfvr::StartBFSoldierFirstPersonArmProbe(
            GetModuleHandleW(nullptr),
            AppendD3D8ObserverLog,
            SignalD3D8ObserverProbeCompletion);
        return 1;
    }

    Direct3DCreate8Fn direct3DCreate8Override = nullptr;
    const bool isD3D8To9FlatProbe =
        initializationRequest ==
        kObserverInitializationRequestD3D8To9FlatProbe;
    const bool isD3D8To9ObserverProbe =
        initializationRequest ==
        kObserverInitializationRequestD3D8To9ObserverProbe;
    const bool isD3D8To9SharedFrameProbe =
        initializationRequest ==
        kObserverInitializationRequestD3D8To9SharedFrameProbe;
    const bool isD3D8To9OpenXRPresentationProbe =
        initializationRequest ==
        kObserverInitializationRequestD3D8To9OpenXRPresentationProbe;
    if (isD3D8To9OpenXRPresentationProbe)
    {
        bfvr::RequestBF1942FrameLimiterOverride();
        bfvr::StartDSoundListenerProbe(AppendD3D8ObserverLog);
    }
    const bool usesD3D8To9Observer =
        isD3D8To9ObserverProbe ||
        isD3D8To9SharedFrameProbe ||
        isD3D8To9OpenXRPresentationProbe;
    if (isD3D8To9SharedFrameProbe ||
        isD3D8To9OpenXRPresentationProbe)
    {
        InterlockedExchange(
            &g_skipExtendedFrameDiagnostics,
            1);
    }
    if (isD3D8To9FlatProbe || usesD3D8To9Observer)
    {
        const bfvr::D3D8RuntimeRedirectResult redirect =
            bfvr::LoadBundledD3D8To9(g_module);
        if (redirect.module == nullptr ||
            redirect.direct3DCreate8 == nullptr)
        {
            AppendLog(
                L"BFVR d3d8to9 probe could not load %s (error %lu).",
                redirect.path[0] == L'\0'
                    ? L"BFVRD3D8To9.dll"
                    : redirect.path,
                redirect.error);
            return 0;
        }
        g_d3d8To9Module = redirect.module;
        direct3DCreate8Override =
            reinterpret_cast<Direct3DCreate8Fn>(redirect.direct3DCreate8);
        g_d3d8To9GetRuntimeDiagnostics =
            reinterpret_cast<BFVRD3D8To9GetRuntimeDiagnosticsFn>(
                GetProcAddress(
                    redirect.module,
                    "BFVRD3D8To9GetRuntimeDiagnostics"));
        if (g_d3d8To9GetRuntimeDiagnostics == nullptr)
        {
            AppendLog(
                L"BFVR d3d8to9 runtime diagnostics ABI is unavailable "
                L"(error %lu).",
                GetLastError());
            FreeLibrary(g_d3d8To9Module);
            g_d3d8To9Module = nullptr;
            return 0;
        }
        AppendLog(
            L"BFVR d3d8to9 probe loaded the pinned translator by absolute path: %s.",
            redirect.path);
    }

    if (!InstallDirect3D8ImportRoute(
            direct3DCreate8Override,
            usesD3D8To9Observer))
    {
        AppendLog(
            L"Direct3D8 import routing failed%s.",
            direct3DCreate8Override == nullptr
                ? L""
                : L" after loading the d3d8to9 translator");
        if (g_d3d8To9Module != nullptr)
        {
            FreeLibrary(g_d3d8To9Module);
            g_d3d8To9Module = nullptr;
        }
        return 0;
    }
    if (isD3D8To9FlatProbe)
    {
        AppendLog(
            L"BFVR d3d8to9 flat gate routed BF1942 directly to the translator; no D3D8 observer hook, hardware breakpoint, worker, or OpenXR bootstrap was enabled.");
    }
    else if (usesD3D8To9Observer)
    {
        AppendLog(
            L"BFVR d3d8to9 observer gate routed BF1942 through the normal BFVR Direct3DCreate8 observer with the pinned translator as its downstream runtime.");
        StartD3D8To9RuntimeDiagnosticsObserver();
    }

    const bool isSurfaceProbeRequest =
        initializationRequest >= kObserverInitializationRequestPresentBridgeProbe &&
        initializationRequest <= kObserverInitializationRequestSurfaceD3D11UploadProbe;
    if (isSurfaceProbeRequest)
    {
        InterlockedExchange(&g_presentBridgeProbeRequested, 1);
    }
    if (isSurfaceProbeRequest && initializationRequest >= kObserverInitializationRequestSurfaceDescriptorProbe)
    {
        InterlockedExchange(&g_surfaceDescriptorProbeRequested, 1);
    }
    if (initializationRequest >= kObserverInitializationRequestSurfaceCopyProbe &&
        initializationRequest < kObserverInitializationRequestSurfaceReadbackProbe)
    {
        InterlockedExchange(&g_surfaceCopyProbeRequested, 1);
    }
    if (initializationRequest >= kObserverInitializationRequestSurfaceStreamProbe &&
        initializationRequest < kObserverInitializationRequestSurfaceReadbackProbe)
    {
        InterlockedExchange(&g_surfaceStreamProbeRequested, 1);
    }
    if (initializationRequest >= kObserverInitializationRequestSurfaceResetProbe &&
        initializationRequest < kObserverInitializationRequestSurfaceReadbackProbe)
    {
        InterlockedExchange(&g_surfaceResetProbeRequested, 1);
    }
    if (initializationRequest == kObserverInitializationRequestSurfaceReadbackProbe)
    {
        InterlockedExchange(&g_surfaceReadbackProbeRequested, 1);
    }
    if (initializationRequest == kObserverInitializationRequestSurfaceSceneReadbackProbe)
    {
        InterlockedExchange(&g_surfaceSceneReadbackProbeRequested, 1);
    }
    if (initializationRequest == kObserverInitializationRequestSurfaceD3D11UploadProbe)
    {
        InterlockedExchange(&g_surfaceD3D11UploadProbeRequested, 1);
    }
    if (initializationRequest == kObserverInitializationRequestRenderViewTransformProbe)
    {
        InterlockedExchange(&g_renderViewTransformProbeRequested, 1);
    }
    if (initializationRequest == kObserverInitializationRequestRenderViewSetterBaselineProbe)
    {
        InterlockedExchange(&g_renderViewSetterBaselineProbeRequested, 1);
    }
    if (initializationRequest == kObserverInitializationRequestRenderViewSingleEyeProbe)
    {
        InterlockedExchange(&g_renderViewSingleEyeProbeRequested, 1);
    }
    if (initializationRequest == kObserverInitializationRequestConfiguredViewListProbe)
    {
        InterlockedExchange(&g_configuredViewListProbeRequested, 1);
    }
    if (initializationRequest == kObserverInitializationRequestConfiguredViewListWriterProbe)
    {
        InterlockedExchange(&g_configuredViewListWriterProbeRequested, 1);
    }
    if (initializationRequest == kObserverInitializationRequestSceneBatchProbe)
    {
        InterlockedExchange(&g_sceneBatchProbeRequested, 1);
    }
    if (initializationRequest == kObserverInitializationRequestD3D8CallInventoryProbe)
    {
        InterlockedExchange(&g_d3d8CallInventoryProbeRequested, 1);
    }
    if (initializationRequest == kObserverInitializationRequestD3D8StateCensusProbe)
    {
        InterlockedExchange(&g_d3d8StateCensusProbeRequested, 1);
    }
    if (isSurfaceProbeRequest)
    {
        StartPresentBridgeProbe();
    }
    if (initializationRequest == kObserverInitializationRequestRenderViewTransformProbe)
    {
        StartRenderViewTransformProbe();
    }
    if (initializationRequest == kObserverInitializationRequestRenderViewSetterBaselineProbe)
    {
        StartRenderViewSetterBaselineProbe();
    }
    if (initializationRequest == kObserverInitializationRequestRenderViewSingleEyeProbe)
    {
        StartRenderViewSingleEyeProbe();
    }
    if (initializationRequest == kObserverInitializationRequestConfiguredViewListProbe)
    {
        StartConfiguredViewListProbe();
    }
    if (initializationRequest == kObserverInitializationRequestConfiguredViewListWriterProbe)
    {
        StartConfiguredViewListWriterProbe();
    }
    if (initializationRequest == kObserverInitializationRequestSceneBatchProbe)
    {
        StartSceneBatchProbe();
    }
    if (initializationRequest == kObserverInitializationRequestD3D8CallInventoryProbe)
    {
        StartD3D8CallInventoryProbe();
    }
    if (initializationRequest == kObserverInitializationRequestD3D8StateCensusProbe)
    {
        StartD3D8StateCensusProbe();
    }
    if (initializationRequest ==
        kObserverInitializationRequestWeaponViewModelProbe)
    {
        const bfvr::D3D8ObserverCallbacks callbacks = {
            TryGetD3D8ObserverLifecycle,
            IsD3D8ObserverCaptureEligible,
            AppendD3D8ObserverLog,
            SignalD3D8ObserverProbeCompletion};
        bfvr::StartD3D8WeaponViewModelProbe(callbacks);
    }
    if (initializationRequest ==
        kObserverInitializationRequestWeaponTransformOwnershipProbe)
    {
        const bfvr::D3D8ObserverCallbacks callbacks = {
            TryGetD3D8ObserverLifecycle,
            IsD3D8ObserverCaptureEligible,
            AppendD3D8ObserverLog,
            SignalD3D8ObserverProbeCompletion};
        bfvr::StartD3D8WeaponTransformOwnershipProbe(callbacks);
    }
    if (initializationRequest == kObserverInitializationRequestPlayerInputProbe)
    {
        bfvr::StartPlayerInputProbe(
            GetModuleHandleW(nullptr),
            AppendD3D8ObserverLog);
    }
    if (initializationRequest == kObserverInitializationRequestWeaponFireProbe)
    {
        bfvr::StartWeaponFireProbe(
            GetModuleHandleW(nullptr),
            AppendD3D8ObserverLog);
    }
    if (initializationRequest == kObserverInitializationRequestD3D8StereoPairProbe)
    {
        const bfvr::D3D8ObserverCallbacks callbacks = {
            TryGetD3D8ObserverLifecycle,
            IsD3D8ObserverCaptureEligible,
            AppendD3D8ObserverLog,
            SignalD3D8ObserverProbeCompletion};
        bfvr::StartD3D8StereoPairProbe(callbacks);
    }
    if (initializationRequest == kObserverInitializationRequestD3D8StereoFrameProbe)
    {
        const bfvr::D3D8ObserverCallbacks callbacks = {
            TryGetD3D8ObserverLifecycle,
            IsD3D8ObserverCaptureEligible,
            AppendD3D8ObserverLog,
            SignalD3D8ObserverProbeCompletion};
        bfvr::StartD3D8StereoFrameProbe(callbacks);
    }
    if (initializationRequest ==
            kObserverInitializationRequestD3D8StereoFramePresentationProbe ||
        isD3D8To9OpenXRPresentationProbe)
    {
        const bfvr::D3D8ObserverCallbacks callbacks = {
            TryGetD3D8PresentationLifecycle,
            IsD3D8ObserverGameplayActive,
            AppendD3D8ObserverLog,
            SignalD3D8ObserverProbeCompletion};
        bfvr::StartD3D8StereoFramePresentationProbe(callbacks);
    }
    if (isD3D8To9SharedFrameProbe)
    {
        const bfvr::D3D8ObserverCallbacks callbacks = {
            TryGetD3D8ObserverLifecycle,
            IsD3D8ObserverCaptureEligible,
            AppendD3D8ObserverLog,
            SignalD3D8ObserverProbeCompletion};
        bfvr::StartD3D8StereoFrameSharedTransportProbe(callbacks);
    }
    return 1;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = module;
        DisableThreadLibraryCalls(module);
    }

    return TRUE;
}
