#include "client/D3D8CallInventory.h"

#include <MinHook.h>

#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <intrin.h>

namespace
{

constexpr std::size_t kIUnknownReleaseSlot = 2;
constexpr std::size_t kDirect3DDevice8PresentSlot = 15;
constexpr std::size_t kDirect3DDevice8SetRenderTargetSlot = 31;
constexpr std::size_t kDirect3DDevice8GetRenderTargetSlot = 32;
constexpr std::size_t kDirect3DDevice8BeginSceneSlot = 34;
constexpr std::size_t kDirect3DDevice8EndSceneSlot = 35;
constexpr std::size_t kDirect3DDevice8ClearSlot = 36;
constexpr std::size_t kDirect3DDevice8SetTransformSlot = 37;
constexpr std::size_t kDirect3DDevice8DrawPrimitiveSlot = 70;
constexpr std::size_t kDirect3DDevice8DrawIndexedPrimitiveSlot = 71;
constexpr std::size_t kDirect3DDevice8DrawPrimitiveUPSlot = 72;
constexpr std::size_t kDirect3DDevice8DrawIndexedPrimitiveUPSlot = 73;
constexpr std::size_t kDirect3DSurface8GetDescSlot = 8;
constexpr DWORD kD3DTransformProjection = 3;
constexpr DWORD_PTR kProjectionWrapperReturnAddress = 0x0045FE21;
constexpr DWORD kOrdinaryWorldProjectionCallerReturnAddress = 0x00466F56;

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

using PresentFn = HRESULT(WINAPI*)(void* device, const RECT* sourceRectangle, const RECT* destinationRectangle, HWND destinationWindowOverride, const RGNDATA* dirtyRegion);
using EndSceneFn = HRESULT(WINAPI*)(void* device);
using BeginSceneFn = HRESULT(WINAPI*)(void* device);
using SetTransformFn = HRESULT(WINAPI*)(void* device, DWORD state, const void* matrix);
using ClearFn = HRESULT(WINAPI*)(void* device, DWORD rectangleCount, const void* rectangles, DWORD flags, DWORD color, float z, DWORD stencil);
using SetRenderTargetFn = HRESULT(WINAPI*)(void* device, void* colorRenderTarget, void* depthStencilSurface);
using DrawPrimitiveFn = HRESULT(WINAPI*)(void* device, DWORD primitiveType, UINT startVertex, UINT primitiveCount);
using DrawIndexedPrimitiveFn = HRESULT(WINAPI*)(void* device, DWORD primitiveType, UINT minimumVertexIndex, UINT vertexCount, UINT startIndex, UINT primitiveCount);
using DrawPrimitiveUPFn = HRESULT(WINAPI*)(void* device, DWORD primitiveType, UINT primitiveCount, const void* vertexData, UINT vertexStride);
using DrawIndexedPrimitiveUPFn = HRESULT(WINAPI*)(void* device, DWORD primitiveType, UINT minimumVertexIndex, UINT vertexCount, UINT primitiveCount, const void* indexData, UINT indexFormat, const void* vertexData, UINT vertexStride);
using GetRenderTargetFn = HRESULT(WINAPI*)(void* device, void** returnedSurface);
using GetSurfaceDescriptionFn = HRESULT(WINAPI*)(void* surface, D3DSurfaceDescription* description);
using ReleaseUnknownFn = ULONG(STDMETHODCALLTYPE*)(void* unknown);

bfvr::D3D8ObserverCallbacks g_callbacks = {};
bfvr::D3D8ObserverLifecycle g_lifecycle = {};

void AppendLog(const wchar_t* format, ...)
{
    if (g_callbacks.appendLog == nullptr)
    {
        return;
    }

    wchar_t message[1024] = {};
    va_list arguments;
    va_start(arguments, format);
    _vsnwprintf_s(message, std::size(message), _TRUNCATE, format, arguments);
    va_end(arguments);
    g_callbacks.appendLog(message);
}

void SignalCompletion()
{
    if (g_callbacks.signalCompletion != nullptr)
    {
        g_callbacks.signalCompletion();
    }
}

bool IsSystemD3D8Target(const void* target)
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
    return _wcsicmp(fileName, L"d3d8.dll") == 0;
}

// This is the first diagnostic for the graphics-API stereo route.  It owns no
// D3D resource and changes no game state: each detour invokes the original
// method first, then writes a bounded POD record.  A Present-to-Present window
// includes setup performed before BeginScene as well as the ensuing draws.
// Target descriptions are read only through the existing D3D8 GetDesc ABI.
// The opening target seed uses the separately established same-thread
// GetRenderTarget/GetDesc/balanced-Release transaction once; the probe creates
// no resource and makes no state-setting call.
constexpr LONG kD3D8CallInventoryMaximumEvents = 2048;
constexpr DWORD kD3D8CallInventoryCaptureTimeoutMs = 45000;
constexpr DWORD kD3DTransformView = 2;
constexpr DWORD kD3DTransformWorld = 0x100;

enum class D3D8CallInventoryEventKind : DWORD
{
    PresentStart,
    InitialRenderTarget,
    Clear,
    SetRenderTarget,
    SetTransform,
    DrawPrimitive,
    DrawIndexedPrimitive,
    DrawPrimitiveUP,
    DrawIndexedPrimitiveUP,
    BeginScene,
    EndScene,
    PresentEnd
};

enum class D3D8CallInventoryTargetClass : DWORD
{
    Unknown,
    FullSizeColorCandidate,
    RenderToTextureCandidate,
    OtherSurface
};

struct D3D8CallInventoryEvent
{
    LONG sequence = 0;
    D3D8CallInventoryEventKind kind = D3D8CallInventoryEventKind::PresentStart;
    HRESULT result = E_FAIL;
    DWORD transformState = 0;
    DWORD primitiveType = 0;
    DWORD primitiveCount = 0;
    DWORD auxiliaryValue = 0;
    void* colorRenderTarget = nullptr;
    D3D8CallInventoryTargetClass targetClass = D3D8CallInventoryTargetClass::Unknown;
    BOOL targetDescriptionReadable = FALSE;
    D3DSurfaceDescription targetDescription = {};
    BOOL viewSeen = FALSE;
    BOOL projectionSeen = FALSE;
    BOOL ordinaryWorldProjection = FALSE;
};

struct D3D8CallInventoryRecord
{
    volatile LONG state = 0; // 0=idle, 1=armed, 2=capturing, 3=complete.
    volatile LONG eventCount = 0;
    volatile LONG eventOverflow = 0;
    DWORD executionThreadId = 0;
    void* device = nullptr;
    void* presentTarget = nullptr;
    void* clearTarget = nullptr;
    void* setRenderTargetTarget = nullptr;
    void* setTransformTarget = nullptr;
    void* drawPrimitiveTarget = nullptr;
    void* drawIndexedPrimitiveTarget = nullptr;
    void* drawPrimitiveUPTarget = nullptr;
    void* drawIndexedPrimitiveUPTarget = nullptr;
    void* beginSceneTarget = nullptr;
    void* endSceneTarget = nullptr;
    LONGLONG startCounter = 0;
    LONGLONG endCounter = 0;
    volatile LONG currentTargetClass = static_cast<LONG>(D3D8CallInventoryTargetClass::Unknown);
    volatile LONG viewSeen = 0;
    volatile LONG projectionSeen = 0;
    volatile LONG worldSeen = 0;
    volatile LONG ordinaryWorldProjectionCount = 0;
    D3D8CallInventoryEvent events[kD3D8CallInventoryMaximumEvents] = {};
};

D3D8CallInventoryRecord g_d3d8CallInventory = {};
PresentFn g_originalPresentForD3D8CallInventory = nullptr;
ClearFn g_originalClearForD3D8CallInventory = nullptr;
SetRenderTargetFn g_originalSetRenderTargetForD3D8CallInventory = nullptr;
SetTransformFn g_originalSetTransformForD3D8CallInventory = nullptr;
DrawPrimitiveFn g_originalDrawPrimitiveForD3D8CallInventory = nullptr;
DrawIndexedPrimitiveFn g_originalDrawIndexedPrimitiveForD3D8CallInventory = nullptr;
DrawPrimitiveUPFn g_originalDrawPrimitiveUPForD3D8CallInventory = nullptr;
DrawIndexedPrimitiveUPFn g_originalDrawIndexedPrimitiveUPForD3D8CallInventory = nullptr;
BeginSceneFn g_originalBeginSceneForD3D8CallInventory = nullptr;
EndSceneFn g_originalEndSceneForD3D8CallInventory = nullptr;

const wchar_t* D3D8CallInventoryEventKindName(D3D8CallInventoryEventKind kind)
{
    switch (kind)
    {
    case D3D8CallInventoryEventKind::PresentStart:
        return L"Present(start)";
    case D3D8CallInventoryEventKind::InitialRenderTarget:
        return L"InitialRenderTarget";
    case D3D8CallInventoryEventKind::Clear:
        return L"Clear";
    case D3D8CallInventoryEventKind::SetRenderTarget:
        return L"SetRenderTarget";
    case D3D8CallInventoryEventKind::SetTransform:
        return L"SetTransform";
    case D3D8CallInventoryEventKind::DrawPrimitive:
        return L"DrawPrimitive";
    case D3D8CallInventoryEventKind::DrawIndexedPrimitive:
        return L"DrawIndexedPrimitive";
    case D3D8CallInventoryEventKind::DrawPrimitiveUP:
        return L"DrawPrimitiveUP";
    case D3D8CallInventoryEventKind::DrawIndexedPrimitiveUP:
        return L"DrawIndexedPrimitiveUP";
    case D3D8CallInventoryEventKind::BeginScene:
        return L"BeginScene";
    case D3D8CallInventoryEventKind::EndScene:
        return L"EndScene";
    case D3D8CallInventoryEventKind::PresentEnd:
        return L"Present(end)";
    }
    return L"unknown";
}

const wchar_t* D3D8CallInventoryTargetClassName(D3D8CallInventoryTargetClass targetClass)
{
    switch (targetClass)
    {
    case D3D8CallInventoryTargetClass::FullSizeColorCandidate:
        return L"full-size-color candidate";
    case D3D8CallInventoryTargetClass::RenderToTextureCandidate:
        return L"render-to-texture candidate";
    case D3D8CallInventoryTargetClass::OtherSurface:
        return L"other surface";
    case D3D8CallInventoryTargetClass::Unknown:
    default:
        return L"unknown";
    }
}

bool IsD3D8CallInventoryCaptureOnDeviceThread(void* device)
{
    return InterlockedCompareExchange(&g_d3d8CallInventory.state, 0, 0) == 2 &&
        device == g_d3d8CallInventory.device &&
        GetCurrentThreadId() == g_d3d8CallInventory.executionThreadId;
}

void RecordD3D8CallInventoryEvent(
    D3D8CallInventoryEventKind kind,
    HRESULT result,
    DWORD transformState = 0,
    DWORD primitiveType = 0,
    DWORD primitiveCount = 0,
    DWORD auxiliaryValue = 0,
    void* colorRenderTarget = nullptr,
    D3D8CallInventoryTargetClass targetClass = D3D8CallInventoryTargetClass::Unknown,
    const D3DSurfaceDescription* targetDescription = nullptr,
    BOOL ordinaryWorldProjection = FALSE)
{
    const LONG eventIndex = InterlockedIncrement(&g_d3d8CallInventory.eventCount) - 1;
    if (eventIndex < 0 || eventIndex >= kD3D8CallInventoryMaximumEvents)
    {
        InterlockedExchange(&g_d3d8CallInventory.eventOverflow, 1);
        return;
    }

    D3D8CallInventoryEvent& event = g_d3d8CallInventory.events[eventIndex];
    event = {};
    event.sequence = eventIndex + 1;
    event.kind = kind;
    event.result = result;
    event.transformState = transformState;
    event.primitiveType = primitiveType;
    event.primitiveCount = primitiveCount;
    event.auxiliaryValue = auxiliaryValue;
    event.colorRenderTarget = colorRenderTarget;
    event.targetClass = targetClass;
    event.viewSeen = InterlockedCompareExchange(&g_d3d8CallInventory.viewSeen, 0, 0) != 0;
    event.projectionSeen = InterlockedCompareExchange(&g_d3d8CallInventory.projectionSeen, 0, 0) != 0;
    event.ordinaryWorldProjection = ordinaryWorldProjection;
    if (targetDescription != nullptr)
    {
        event.targetDescriptionReadable = TRUE;
        event.targetDescription = *targetDescription;
    }
}

D3D8CallInventoryTargetClass ClassifyD3D8CallInventoryRenderTarget(
    void* colorRenderTarget,
    D3DSurfaceDescription& description,
    BOOL& descriptionReadable)
{
    description = {};
    descriptionReadable = FALSE;
    if (colorRenderTarget == nullptr)
    {
        return D3D8CallInventoryTargetClass::Unknown;
    }

    __try
    {
        auto** const surfaceVtable = *reinterpret_cast<void***>(colorRenderTarget);
        const auto getDescription = surfaceVtable == nullptr
            ? nullptr
            : reinterpret_cast<GetSurfaceDescriptionFn>(surfaceVtable[kDirect3DSurface8GetDescSlot]);
        if (getDescription == nullptr || !IsSystemD3D8Target(reinterpret_cast<void*>(getDescription)))
        {
            return D3D8CallInventoryTargetClass::Unknown;
        }
        if (FAILED(getDescription(colorRenderTarget, &description)))
        {
            return D3D8CallInventoryTargetClass::Unknown;
        }
        descriptionReadable = TRUE;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return D3D8CallInventoryTargetClass::Unknown;
    }

    if (g_lifecycle.presentationReadable &&
        description.type == 1 &&
        description.width == g_lifecycle.backBufferWidth &&
        description.height == g_lifecycle.backBufferHeight)
    {
        return D3D8CallInventoryTargetClass::FullSizeColorCandidate;
    }
    if (description.type == 1 && description.width != 0 && description.height != 0)
    {
        return D3D8CallInventoryTargetClass::RenderToTextureCandidate;
    }
    return D3D8CallInventoryTargetClass::OtherSurface;
}

// This is the same narrow same-thread reference contract established by the
// earlier post-Present descriptor probe. It is intentionally only the capture
// window's initial-state seed: GetRenderTarget supplies one temporary game
// reference, GetDesc only reads metadata, and Release balances that reference
// before the Present detour returns.
void SeedD3D8CallInventoryInitialRenderTarget(void* device)
{
    void* acquiredSurface = nullptr;
    HRESULT getRenderTargetResult = E_FAIL;
    HRESULT getDescriptionResult = E_FAIL;
    ULONG releaseResult = 0;
    D3DSurfaceDescription description = {};
    BOOL descriptionReadable = FALSE;
    D3D8CallInventoryTargetClass targetClass = D3D8CallInventoryTargetClass::Unknown;
    __try
    {
        auto** const deviceVtable = *reinterpret_cast<void***>(device);
        const auto getRenderTarget = deviceVtable == nullptr
            ? nullptr
            : reinterpret_cast<GetRenderTargetFn>(deviceVtable[kDirect3DDevice8GetRenderTargetSlot]);
        if (getRenderTarget != nullptr && IsSystemD3D8Target(reinterpret_cast<void*>(getRenderTarget)))
        {
            getRenderTargetResult = getRenderTarget(device, &acquiredSurface);
        }
        if (SUCCEEDED(getRenderTargetResult) && acquiredSurface != nullptr)
        {
            auto** const surfaceVtable = *reinterpret_cast<void***>(acquiredSurface);
            const auto getDescription = surfaceVtable == nullptr
                ? nullptr
                : reinterpret_cast<GetSurfaceDescriptionFn>(surfaceVtable[kDirect3DSurface8GetDescSlot]);
            if (getDescription != nullptr && IsSystemD3D8Target(reinterpret_cast<void*>(getDescription)))
            {
                getDescriptionResult = getDescription(acquiredSurface, &description);
                descriptionReadable = SUCCEEDED(getDescriptionResult);
                if (descriptionReadable)
                {
                    if (g_lifecycle.presentationReadable &&
                        description.type == 1 &&
                        description.width == g_lifecycle.backBufferWidth &&
                        description.height == g_lifecycle.backBufferHeight)
                    {
                        targetClass = D3D8CallInventoryTargetClass::FullSizeColorCandidate;
                    }
                    else if (description.type == 1 && description.width != 0 && description.height != 0)
                    {
                        targetClass = D3D8CallInventoryTargetClass::RenderToTextureCandidate;
                    }
                    else
                    {
                        targetClass = D3D8CallInventoryTargetClass::OtherSurface;
                    }
                }
            }
            const auto release = surfaceVtable == nullptr
                ? nullptr
                : reinterpret_cast<ReleaseUnknownFn>(surfaceVtable[kIUnknownReleaseSlot]);
            if (release != nullptr && IsSystemD3D8Target(reinterpret_cast<void*>(release)))
            {
                releaseResult = release(acquiredSurface);
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        targetClass = D3D8CallInventoryTargetClass::Unknown;
    }

    InterlockedExchange(&g_d3d8CallInventory.currentTargetClass, static_cast<LONG>(targetClass));
    RecordD3D8CallInventoryEvent(
        D3D8CallInventoryEventKind::InitialRenderTarget,
        getRenderTargetResult,
        0,
        0,
        0,
        static_cast<DWORD>(getDescriptionResult),
        acquiredSurface,
        targetClass,
        descriptionReadable ? &description : nullptr);
    if (acquiredSurface != nullptr && releaseResult != 1)
    {
        // A known game-owned target normally returns to reference count 1. Do
        // not branch into further D3D work here; the log record is enough to
        // reject this seed if that established accounting no longer holds.
        InterlockedExchange(&g_d3d8CallInventory.currentTargetClass, static_cast<LONG>(D3D8CallInventoryTargetClass::Unknown));
    }
}

HRESULT WINAPI HookPresentD3D8CallInventory(
    void* device,
    const RECT* sourceRectangle,
    const RECT* destinationRectangle,
    HWND destinationWindowOverride,
    const RGNDATA* dirtyRegion)
{
    const PresentFn originalPresent = g_originalPresentForD3D8CallInventory;
    const HRESULT result = originalPresent == nullptr
        ? E_FAIL
        : originalPresent(device, sourceRectangle, destinationRectangle, destinationWindowOverride, dirtyRegion);

    const LONG state = InterlockedCompareExchange(&g_d3d8CallInventory.state, 0, 0);
    if (state == 1 &&
        SUCCEEDED(result) &&
        device == g_d3d8CallInventory.device &&
        GetCurrentThreadId() == g_d3d8CallInventory.executionThreadId &&
        // The client admits a frame only after its passive local-BFPlayer
        // isAlive debounce confirms a sustained spawned state. This callback
        // does not invoke the game or depend on a camera-transform candidate.
        g_callbacks.isCaptureEligible != nullptr &&
        g_callbacks.isCaptureEligible() &&
        InterlockedCompareExchange(&g_d3d8CallInventory.state, 2, 1) == 1)
    {
        InterlockedExchange(&g_d3d8CallInventory.eventCount, 0);
        InterlockedExchange(&g_d3d8CallInventory.eventOverflow, 0);
        InterlockedExchange(&g_d3d8CallInventory.currentTargetClass, static_cast<LONG>(D3D8CallInventoryTargetClass::Unknown));
        InterlockedExchange(&g_d3d8CallInventory.viewSeen, 0);
        InterlockedExchange(&g_d3d8CallInventory.projectionSeen, 0);
        InterlockedExchange(&g_d3d8CallInventory.worldSeen, 0);
        InterlockedExchange(&g_d3d8CallInventory.ordinaryWorldProjectionCount, 0);
        LARGE_INTEGER counter = {};
        if (QueryPerformanceCounter(&counter))
        {
            g_d3d8CallInventory.startCounter = counter.QuadPart;
        }
        RecordD3D8CallInventoryEvent(D3D8CallInventoryEventKind::PresentStart, result);
        SeedD3D8CallInventoryInitialRenderTarget(device);
    }
    else if (IsD3D8CallInventoryCaptureOnDeviceThread(device))
    {
        RecordD3D8CallInventoryEvent(D3D8CallInventoryEventKind::PresentEnd, result);
        LARGE_INTEGER counter = {};
        if (QueryPerformanceCounter(&counter))
        {
            g_d3d8CallInventory.endCounter = counter.QuadPart;
        }
        MemoryBarrier();
        InterlockedExchange(&g_d3d8CallInventory.state, 3);
    }
    return result;
}

HRESULT WINAPI HookClearD3D8CallInventory(
    void* device,
    DWORD rectangleCount,
    const void* rectangles,
    DWORD flags,
    DWORD color,
    float z,
    DWORD stencil)
{
    const ClearFn originalClear = g_originalClearForD3D8CallInventory;
    const HRESULT result = originalClear == nullptr
        ? E_FAIL
        : originalClear(device, rectangleCount, rectangles, flags, color, z, stencil);
    if (IsD3D8CallInventoryCaptureOnDeviceThread(device))
    {
        RecordD3D8CallInventoryEvent(
            D3D8CallInventoryEventKind::Clear,
            result,
            0,
            0,
            rectangleCount,
            flags,
            nullptr,
            static_cast<D3D8CallInventoryTargetClass>(InterlockedCompareExchange(&g_d3d8CallInventory.currentTargetClass, 0, 0)));
    }
    return result;
}

HRESULT WINAPI HookSetRenderTargetD3D8CallInventory(void* device, void* colorRenderTarget, void* depthStencilSurface)
{
    const SetRenderTargetFn originalSetRenderTarget = g_originalSetRenderTargetForD3D8CallInventory;
    const HRESULT result = originalSetRenderTarget == nullptr
        ? E_FAIL
        : originalSetRenderTarget(device, colorRenderTarget, depthStencilSurface);
    if (IsD3D8CallInventoryCaptureOnDeviceThread(device))
    {
        D3DSurfaceDescription description = {};
        BOOL descriptionReadable = FALSE;
        const D3D8CallInventoryTargetClass targetClass = SUCCEEDED(result)
            ? ClassifyD3D8CallInventoryRenderTarget(colorRenderTarget, description, descriptionReadable)
            : D3D8CallInventoryTargetClass::Unknown;
        InterlockedExchange(&g_d3d8CallInventory.currentTargetClass, static_cast<LONG>(targetClass));
        RecordD3D8CallInventoryEvent(
            D3D8CallInventoryEventKind::SetRenderTarget,
            result,
            0,
            0,
            0,
            depthStencilSurface != nullptr ? 1 : 0,
            colorRenderTarget,
            targetClass,
            descriptionReadable ? &description : nullptr);
    }
    return result;
}

HRESULT WINAPI HookSetTransformD3D8CallInventory(void* device, DWORD transformState, const void* matrix)
{
    const void* const callerReturnAddress = _ReturnAddress();
    DWORD externalCallerReturnAddress = 0;
    BOOL externalStackReadable = FALSE;
    __try
    {
        const auto* const stack = reinterpret_cast<const DWORD*>(_AddressOfReturnAddress());
        externalCallerReturnAddress = stack[6];
        externalStackReadable = TRUE;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        externalStackReadable = FALSE;
    }

    const SetTransformFn originalSetTransform = g_originalSetTransformForD3D8CallInventory;
    const HRESULT result = originalSetTransform == nullptr ? E_FAIL : originalSetTransform(device, transformState, matrix);
    if (IsD3D8CallInventoryCaptureOnDeviceThread(device))
    {
        if (transformState == kD3DTransformWorld)
        {
            InterlockedExchange(&g_d3d8CallInventory.worldSeen, 1);
        }
        else if (transformState == kD3DTransformView)
        {
            InterlockedExchange(&g_d3d8CallInventory.viewSeen, 1);
        }
        else if (transformState == kD3DTransformProjection)
        {
            InterlockedExchange(&g_d3d8CallInventory.projectionSeen, 1);
        }
        const BOOL ordinaryWorldProjection =
            transformState == kD3DTransformProjection &&
            callerReturnAddress == reinterpret_cast<void*>(kProjectionWrapperReturnAddress) &&
            externalStackReadable &&
            externalCallerReturnAddress == kOrdinaryWorldProjectionCallerReturnAddress;
        if (ordinaryWorldProjection)
        {
            InterlockedIncrement(&g_d3d8CallInventory.ordinaryWorldProjectionCount);
        }
        RecordD3D8CallInventoryEvent(
            D3D8CallInventoryEventKind::SetTransform,
            result,
            transformState,
            0,
            0,
            ordinaryWorldProjection ? 1 : 0,
            nullptr,
            static_cast<D3D8CallInventoryTargetClass>(InterlockedCompareExchange(&g_d3d8CallInventory.currentTargetClass, 0, 0)),
            nullptr,
            ordinaryWorldProjection);
    }
    return result;
}

void RecordD3D8CallInventoryDraw(D3D8CallInventoryEventKind kind, void* device, HRESULT result, DWORD primitiveType, UINT primitiveCount)
{
    if (!IsD3D8CallInventoryCaptureOnDeviceThread(device))
    {
        return;
    }
    RecordD3D8CallInventoryEvent(
        kind,
        result,
        0,
        primitiveType,
        primitiveCount,
        0,
        nullptr,
        static_cast<D3D8CallInventoryTargetClass>(InterlockedCompareExchange(&g_d3d8CallInventory.currentTargetClass, 0, 0)));
}

HRESULT WINAPI HookDrawPrimitiveD3D8CallInventory(void* device, DWORD primitiveType, UINT startVertex, UINT primitiveCount)
{
    const DrawPrimitiveFn originalDraw = g_originalDrawPrimitiveForD3D8CallInventory;
    const HRESULT result = originalDraw == nullptr ? E_FAIL : originalDraw(device, primitiveType, startVertex, primitiveCount);
    RecordD3D8CallInventoryDraw(D3D8CallInventoryEventKind::DrawPrimitive, device, result, primitiveType, primitiveCount);
    return result;
}

HRESULT WINAPI HookDrawIndexedPrimitiveD3D8CallInventory(void* device, DWORD primitiveType, UINT minimumVertexIndex, UINT vertexCount, UINT startIndex, UINT primitiveCount)
{
    const DrawIndexedPrimitiveFn originalDraw = g_originalDrawIndexedPrimitiveForD3D8CallInventory;
    const HRESULT result = originalDraw == nullptr ? E_FAIL : originalDraw(device, primitiveType, minimumVertexIndex, vertexCount, startIndex, primitiveCount);
    RecordD3D8CallInventoryDraw(D3D8CallInventoryEventKind::DrawIndexedPrimitive, device, result, primitiveType, primitiveCount);
    return result;
}

HRESULT WINAPI HookDrawPrimitiveUPD3D8CallInventory(void* device, DWORD primitiveType, UINT primitiveCount, const void* vertexData, UINT vertexStride)
{
    const DrawPrimitiveUPFn originalDraw = g_originalDrawPrimitiveUPForD3D8CallInventory;
    const HRESULT result = originalDraw == nullptr ? E_FAIL : originalDraw(device, primitiveType, primitiveCount, vertexData, vertexStride);
    RecordD3D8CallInventoryDraw(D3D8CallInventoryEventKind::DrawPrimitiveUP, device, result, primitiveType, primitiveCount);
    return result;
}

HRESULT WINAPI HookDrawIndexedPrimitiveUPD3D8CallInventory(void* device, DWORD primitiveType, UINT minimumVertexIndex, UINT vertexCount, UINT primitiveCount, const void* indexData, UINT indexFormat, const void* vertexData, UINT vertexStride)
{
    const DrawIndexedPrimitiveUPFn originalDraw = g_originalDrawIndexedPrimitiveUPForD3D8CallInventory;
    const HRESULT result = originalDraw == nullptr ? E_FAIL : originalDraw(device, primitiveType, minimumVertexIndex, vertexCount, primitiveCount, indexData, indexFormat, vertexData, vertexStride);
    RecordD3D8CallInventoryDraw(D3D8CallInventoryEventKind::DrawIndexedPrimitiveUP, device, result, primitiveType, primitiveCount);
    return result;
}

HRESULT WINAPI HookBeginSceneD3D8CallInventory(void* device)
{
    const BeginSceneFn originalBeginScene = g_originalBeginSceneForD3D8CallInventory;
    const HRESULT result = originalBeginScene == nullptr ? E_FAIL : originalBeginScene(device);
    if (IsD3D8CallInventoryCaptureOnDeviceThread(device))
    {
        RecordD3D8CallInventoryEvent(
            D3D8CallInventoryEventKind::BeginScene,
            result,
            0,
            0,
            0,
            0,
            nullptr,
            static_cast<D3D8CallInventoryTargetClass>(InterlockedCompareExchange(&g_d3d8CallInventory.currentTargetClass, 0, 0)));
    }
    return result;
}

HRESULT WINAPI HookEndSceneD3D8CallInventory(void* device)
{
    const EndSceneFn originalEndScene = g_originalEndSceneForD3D8CallInventory;
    const HRESULT result = originalEndScene == nullptr ? E_FAIL : originalEndScene(device);
    if (IsD3D8CallInventoryCaptureOnDeviceThread(device))
    {
        RecordD3D8CallInventoryEvent(
            D3D8CallInventoryEventKind::EndScene,
            result,
            0,
            0,
            0,
            0,
            nullptr,
            static_cast<D3D8CallInventoryTargetClass>(InterlockedCompareExchange(&g_d3d8CallInventory.currentTargetClass, 0, 0)));
    }
    return result;
}

void RemoveD3D8CallInventoryHooks()
{
    void* const targets[] = {
        g_d3d8CallInventory.presentTarget,
        g_d3d8CallInventory.clearTarget,
        g_d3d8CallInventory.setRenderTargetTarget,
        g_d3d8CallInventory.setTransformTarget,
        g_d3d8CallInventory.drawPrimitiveTarget,
        g_d3d8CallInventory.drawIndexedPrimitiveTarget,
        g_d3d8CallInventory.drawPrimitiveUPTarget,
        g_d3d8CallInventory.drawIndexedPrimitiveUPTarget,
        g_d3d8CallInventory.beginSceneTarget,
        g_d3d8CallInventory.endSceneTarget};
    for (void* target : targets)
    {
        if (target != nullptr)
        {
            MH_DisableHook(target);
            MH_RemoveHook(target);
        }
    }
}

bool InstallD3D8CallInventoryHooks()
{
    void* const device = g_lifecycle.device;
    if (device == nullptr)
    {
        return false;
    }

    __try
    {
        auto** const deviceVtable = *reinterpret_cast<void***>(device);
        if (deviceVtable == nullptr)
        {
            return false;
        }
        g_d3d8CallInventory.device = device;
        g_d3d8CallInventory.executionThreadId = g_lifecycle.deviceThreadId;
        g_d3d8CallInventory.presentTarget = deviceVtable[kDirect3DDevice8PresentSlot];
        g_d3d8CallInventory.clearTarget = deviceVtable[kDirect3DDevice8ClearSlot];
        g_d3d8CallInventory.setRenderTargetTarget = deviceVtable[kDirect3DDevice8SetRenderTargetSlot];
        g_d3d8CallInventory.setTransformTarget = deviceVtable[kDirect3DDevice8SetTransformSlot];
        g_d3d8CallInventory.drawPrimitiveTarget = deviceVtable[kDirect3DDevice8DrawPrimitiveSlot];
        g_d3d8CallInventory.drawIndexedPrimitiveTarget = deviceVtable[kDirect3DDevice8DrawIndexedPrimitiveSlot];
        g_d3d8CallInventory.drawPrimitiveUPTarget = deviceVtable[kDirect3DDevice8DrawPrimitiveUPSlot];
        g_d3d8CallInventory.drawIndexedPrimitiveUPTarget = deviceVtable[kDirect3DDevice8DrawIndexedPrimitiveUPSlot];
        g_d3d8CallInventory.beginSceneTarget = deviceVtable[kDirect3DDevice8BeginSceneSlot];
        g_d3d8CallInventory.endSceneTarget = deviceVtable[kDirect3DDevice8EndSceneSlot];
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    void* const targets[] = {
        g_d3d8CallInventory.presentTarget,
        g_d3d8CallInventory.clearTarget,
        g_d3d8CallInventory.setRenderTargetTarget,
        g_d3d8CallInventory.setTransformTarget,
        g_d3d8CallInventory.drawPrimitiveTarget,
        g_d3d8CallInventory.drawIndexedPrimitiveTarget,
        g_d3d8CallInventory.drawPrimitiveUPTarget,
        g_d3d8CallInventory.drawIndexedPrimitiveUPTarget,
        g_d3d8CallInventory.beginSceneTarget,
        g_d3d8CallInventory.endSceneTarget};
    for (void* target : targets)
    {
        if (target == nullptr || !IsSystemD3D8Target(target))
        {
            return false;
        }
    }

    const MH_STATUS initializeStatus = MH_Initialize();
    if (initializeStatus != MH_OK)
    {
        AppendLog(L"D3D8 call inventory skipped: MH_Initialize failed (%d).", static_cast<int>(initializeStatus));
        return false;
    }

    const auto createHook = [](void* target, LPVOID detour, LPVOID* original) -> bool
    {
        return MH_CreateHook(target, detour, original) == MH_OK && *original != nullptr;
    };
    const bool created =
        createHook(g_d3d8CallInventory.presentTarget, reinterpret_cast<LPVOID>(&HookPresentD3D8CallInventory), reinterpret_cast<LPVOID*>(&g_originalPresentForD3D8CallInventory)) &&
        createHook(g_d3d8CallInventory.clearTarget, reinterpret_cast<LPVOID>(&HookClearD3D8CallInventory), reinterpret_cast<LPVOID*>(&g_originalClearForD3D8CallInventory)) &&
        createHook(g_d3d8CallInventory.setRenderTargetTarget, reinterpret_cast<LPVOID>(&HookSetRenderTargetD3D8CallInventory), reinterpret_cast<LPVOID*>(&g_originalSetRenderTargetForD3D8CallInventory)) &&
        createHook(g_d3d8CallInventory.setTransformTarget, reinterpret_cast<LPVOID>(&HookSetTransformD3D8CallInventory), reinterpret_cast<LPVOID*>(&g_originalSetTransformForD3D8CallInventory)) &&
        createHook(g_d3d8CallInventory.drawPrimitiveTarget, reinterpret_cast<LPVOID>(&HookDrawPrimitiveD3D8CallInventory), reinterpret_cast<LPVOID*>(&g_originalDrawPrimitiveForD3D8CallInventory)) &&
        createHook(g_d3d8CallInventory.drawIndexedPrimitiveTarget, reinterpret_cast<LPVOID>(&HookDrawIndexedPrimitiveD3D8CallInventory), reinterpret_cast<LPVOID*>(&g_originalDrawIndexedPrimitiveForD3D8CallInventory)) &&
        createHook(g_d3d8CallInventory.drawPrimitiveUPTarget, reinterpret_cast<LPVOID>(&HookDrawPrimitiveUPD3D8CallInventory), reinterpret_cast<LPVOID*>(&g_originalDrawPrimitiveUPForD3D8CallInventory)) &&
        createHook(g_d3d8CallInventory.drawIndexedPrimitiveUPTarget, reinterpret_cast<LPVOID>(&HookDrawIndexedPrimitiveUPD3D8CallInventory), reinterpret_cast<LPVOID*>(&g_originalDrawIndexedPrimitiveUPForD3D8CallInventory)) &&
        createHook(g_d3d8CallInventory.beginSceneTarget, reinterpret_cast<LPVOID>(&HookBeginSceneD3D8CallInventory), reinterpret_cast<LPVOID*>(&g_originalBeginSceneForD3D8CallInventory)) &&
        createHook(g_d3d8CallInventory.endSceneTarget, reinterpret_cast<LPVOID>(&HookEndSceneD3D8CallInventory), reinterpret_cast<LPVOID*>(&g_originalEndSceneForD3D8CallInventory));
    if (!created)
    {
        AppendLog(L"D3D8 call inventory skipped: at least one forwarding hook could not be created; all created inventory hooks will be removed.");
        RemoveD3D8CallInventoryHooks();
        MH_Uninitialize();
        return false;
    }

    bool enabled = true;
    for (void* target : targets)
    {
        if (MH_EnableHook(target) != MH_OK)
        {
            enabled = false;
            break;
        }
    }
    if (!enabled)
    {
        AppendLog(L"D3D8 call inventory skipped: at least one forwarding hook could not be enabled; all inventory hooks will be removed.");
        RemoveD3D8CallInventoryHooks();
        MH_Uninitialize();
        return false;
    }
    return true;
}

void ReportD3D8CallInventory()
{
    const LONG requestedEventCount = InterlockedCompareExchange(&g_d3d8CallInventory.eventCount, 0, 0);
    const LONG eventCount = std::clamp<LONG>(requestedEventCount, 0, kD3D8CallInventoryMaximumEvents);
    LONG beginSceneCount = 0;
    LONG endSceneCount = 0;
    LONG clearCount = 0;
    LONG initialRenderTargetCount = 0;
    LONG renderTargetChangeCount = 0;
    LONG worldTransformCount = 0;
    LONG viewTransformCount = 0;
    LONG projectionTransformCount = 0;
    LONG otherTransformCount = 0;
    LONG drawCount = 0;
    LONG indexedDrawCount = 0;
    LONG upDrawCount = 0;
    LONG fullSizeWorldCandidateDrawCount = 0;
    LONG fullSizeOverlayCandidateDrawCount = 0;
    LONG renderToTextureDrawCount = 0;
    LONG unknownTargetDrawCount = 0;
    for (LONG index = 0; index < eventCount; ++index)
    {
        const D3D8CallInventoryEvent& event = g_d3d8CallInventory.events[index];
        switch (event.kind)
        {
        case D3D8CallInventoryEventKind::BeginScene:
            ++beginSceneCount;
            break;
        case D3D8CallInventoryEventKind::EndScene:
            ++endSceneCount;
            break;
        case D3D8CallInventoryEventKind::Clear:
            ++clearCount;
            break;
        case D3D8CallInventoryEventKind::InitialRenderTarget:
            ++initialRenderTargetCount;
            break;
        case D3D8CallInventoryEventKind::SetRenderTarget:
            ++renderTargetChangeCount;
            break;
        case D3D8CallInventoryEventKind::SetTransform:
            if (event.transformState == kD3DTransformWorld)
            {
                ++worldTransformCount;
            }
            else if (event.transformState == kD3DTransformView)
            {
                ++viewTransformCount;
            }
            else if (event.transformState == kD3DTransformProjection)
            {
                ++projectionTransformCount;
            }
            else
            {
                ++otherTransformCount;
            }
            break;
        case D3D8CallInventoryEventKind::DrawPrimitive:
        case D3D8CallInventoryEventKind::DrawIndexedPrimitive:
        case D3D8CallInventoryEventKind::DrawPrimitiveUP:
        case D3D8CallInventoryEventKind::DrawIndexedPrimitiveUP:
            ++drawCount;
            if (event.kind == D3D8CallInventoryEventKind::DrawIndexedPrimitive ||
                event.kind == D3D8CallInventoryEventKind::DrawIndexedPrimitiveUP)
            {
                ++indexedDrawCount;
            }
            if (event.kind == D3D8CallInventoryEventKind::DrawPrimitiveUP ||
                event.kind == D3D8CallInventoryEventKind::DrawIndexedPrimitiveUP)
            {
                ++upDrawCount;
            }
            if (event.targetClass == D3D8CallInventoryTargetClass::RenderToTextureCandidate)
            {
                ++renderToTextureDrawCount;
            }
            else if (event.targetClass == D3D8CallInventoryTargetClass::FullSizeColorCandidate &&
                     event.viewSeen && event.projectionSeen)
            {
                ++fullSizeWorldCandidateDrawCount;
            }
            else if (event.targetClass == D3D8CallInventoryTargetClass::FullSizeColorCandidate)
            {
                ++fullSizeOverlayCandidateDrawCount;
            }
            else
            {
                ++unknownTargetDrawCount;
            }
            break;
        default:
            break;
        }
    }

    LARGE_INTEGER frequency = {};
    QueryPerformanceFrequency(&frequency);
    const double durationMilliseconds = frequency.QuadPart != 0 && g_d3d8CallInventory.endCounter >= g_d3d8CallInventory.startCounter
        ? static_cast<double>(g_d3d8CallInventory.endCounter - g_d3d8CallInventory.startCounter) * 1000.0 / static_cast<double>(frequency.QuadPart)
        : 0.0;
    AppendLog(
        L"D3D8 one-frame call inventory complete: device=%p thread=%lu events=%ld/%ld overflow=%ld span=%.3f ms initialTarget=%ld BeginScene=%ld EndScene=%ld Clear=%ld SetRenderTarget=%ld transforms[World=%ld View=%ld Projection=%ld Other=%ld] draws=%ld indexed=%ld UP=%ld ordinaryWorldProjection=%ld.",
        g_d3d8CallInventory.device,
        g_d3d8CallInventory.executionThreadId,
        eventCount,
        kD3D8CallInventoryMaximumEvents,
        InterlockedCompareExchange(&g_d3d8CallInventory.eventOverflow, 0, 0),
        durationMilliseconds,
        initialRenderTargetCount,
        beginSceneCount,
        endSceneCount,
        clearCount,
        renderTargetChangeCount,
        worldTransformCount,
        viewTransformCount,
        projectionTransformCount,
        otherTransformCount,
        drawCount,
        indexedDrawCount,
        upDrawCount,
        InterlockedCompareExchange(&g_d3d8CallInventory.ordinaryWorldProjectionCount, 0, 0));
    AppendLog(
        L"D3D8 one-frame provisional pass classification: full-size with View+Projection=%ld draw(s) (world candidate); render-to-texture=%ld draw(s); full-size without both transforms=%ld draw(s) (UI/overlay candidate); unknown/other target=%ld draw(s). This is call-structure classification only, not image-content proof.",
        fullSizeWorldCandidateDrawCount,
        renderToTextureDrawCount,
        fullSizeOverlayCandidateDrawCount,
        unknownTargetDrawCount);

    for (LONG index = 0; index < eventCount; ++index)
    {
        const D3D8CallInventoryEvent& event = g_d3d8CallInventory.events[index];
        if (event.targetDescriptionReadable)
        {
            AppendLog(
                L"D3D8 inventory [%ld] %s result=0x%08lX targetClass=%s target=%p desc[format=%u type=%u usage=0x%08lX pool=%u multisample=%u size=%ux%u] transform=%lu primitive[type=%lu count=%lu] aux=%lu view=%d projection=%d ordinaryWorldProjection=%d.",
                event.sequence,
                D3D8CallInventoryEventKindName(event.kind),
                static_cast<unsigned long>(event.result),
                D3D8CallInventoryTargetClassName(event.targetClass),
                event.colorRenderTarget,
                event.targetDescription.format,
                event.targetDescription.type,
                static_cast<unsigned long>(event.targetDescription.usage),
                event.targetDescription.pool,
                event.targetDescription.multiSampleType,
                event.targetDescription.width,
                event.targetDescription.height,
                static_cast<unsigned long>(event.transformState),
                static_cast<unsigned long>(event.primitiveType),
                static_cast<unsigned long>(event.primitiveCount),
                static_cast<unsigned long>(event.auxiliaryValue),
                event.viewSeen,
                event.projectionSeen,
                event.ordinaryWorldProjection);
        }
        else
        {
            AppendLog(
                L"D3D8 inventory [%ld] %s result=0x%08lX targetClass=%s transform=%lu primitive[type=%lu count=%lu] aux=%lu view=%d projection=%d ordinaryWorldProjection=%d.",
                event.sequence,
                D3D8CallInventoryEventKindName(event.kind),
                static_cast<unsigned long>(event.result),
                D3D8CallInventoryTargetClassName(event.targetClass),
                static_cast<unsigned long>(event.transformState),
                static_cast<unsigned long>(event.primitiveType),
                static_cast<unsigned long>(event.primitiveCount),
                static_cast<unsigned long>(event.auxiliaryValue),
                event.viewSeen,
                event.projectionSeen,
                event.ordinaryWorldProjection);
        }
    }
}

DWORD WINAPI RunD3D8CallInventoryProbe(void*)
{
    constexpr DWORD kLifecycleReadyTimeoutMs = 60000;
    const DWORD lifecycleStartedAt = GetTickCount();
    while (GetTickCount() - lifecycleStartedAt < kLifecycleReadyTimeoutMs)
    {
        if (g_callbacks.tryGetReadyLifecycle != nullptr &&
            g_callbacks.tryGetReadyLifecycle(&g_lifecycle))
        {
            break;
        }
        Sleep(10);
    }
    if (g_lifecycle.device == nullptr || g_lifecycle.deviceThreadId == 0)
    {
        AppendLog(L"D3D8 call inventory skipped: the verified CreateDevice/Reset/Present lifecycle did not complete within %lu ms.", kLifecycleReadyTimeoutMs);
        SignalCompletion();
        return 0;
    }
    if (!InstallD3D8CallInventoryHooks())
    {
        AppendLog(L"D3D8 call inventory skipped: required device methods were not all direct system-d3d8 forwarding targets.");
        SignalCompletion();
        return 0;
    }

    InterlockedExchange(&g_d3d8CallInventory.state, 1);
    AppendLog(
        L"Enabled D3D8 one-frame call inventory: Present=%p Clear=%p SetRenderTarget=%p SetTransform=%p DrawPrimitive=%p DrawIndexedPrimitive=%p DrawPrimitiveUP=%p DrawIndexedPrimitiveUP=%p BeginScene=%p EndScene=%p. It waits for the caller's sustained passive local-BFPlayer isAlive gate, then records exactly one following Present-to-Present frame. It forwards every game call unchanged, creates no resource, and creates no OpenXR object.",
        g_d3d8CallInventory.presentTarget,
        g_d3d8CallInventory.clearTarget,
        g_d3d8CallInventory.setRenderTargetTarget,
        g_d3d8CallInventory.setTransformTarget,
        g_d3d8CallInventory.drawPrimitiveTarget,
        g_d3d8CallInventory.drawIndexedPrimitiveTarget,
        g_d3d8CallInventory.drawPrimitiveUPTarget,
        g_d3d8CallInventory.drawIndexedPrimitiveUPTarget,
        g_d3d8CallInventory.beginSceneTarget,
        g_d3d8CallInventory.endSceneTarget);

    const DWORD captureStartedAt = GetTickCount();
    while (GetTickCount() - captureStartedAt < kD3D8CallInventoryCaptureTimeoutMs &&
           InterlockedCompareExchange(&g_d3d8CallInventory.state, 0, 0) != 3)
    {
        Sleep(10);
    }
    if (InterlockedCompareExchange(&g_d3d8CallInventory.state, 0, 0) != 3)
    {
        AppendLog(L"D3D8 call inventory did not observe a sustained-local-BFPlayer-isAlive-gated Present-to-Present frame within %lu ms; no D3D8 state or resource was changed.", kD3D8CallInventoryCaptureTimeoutMs);
        SignalCompletion();
        return 0;
    }
    ReportD3D8CallInventory();
    SignalCompletion();
    return 0;
}

void StartD3D8CallInventoryProbeImpl(const bfvr::D3D8ObserverCallbacks& callbacks)
{
    g_callbacks = callbacks;
    g_lifecycle = {};
    HANDLE worker = CreateThread(nullptr, 0, RunD3D8CallInventoryProbe, nullptr, 0, nullptr);
    if (worker == nullptr)
    {
        AppendLog(L"D3D8 call inventory worker could not start (%lu); no D3D8 code was patched.", GetLastError());
        SignalCompletion();
        return;
    }
    CloseHandle(worker);
    AppendLog(L"Requested the explicit one-frame, no-HMD D3D8 call inventory; it is a forwarding-only diagnostic and will not run until the verified lifecycle and sustained local-BFPlayer isAlive gate are both available.");
}


} // namespace

namespace bfvr
{

void StartD3D8CallInventoryProbe(const D3D8ObserverCallbacks& callbacks)
{
    if (callbacks.tryGetReadyLifecycle == nullptr ||
        callbacks.isCaptureEligible == nullptr ||
        callbacks.appendLog == nullptr ||
        callbacks.signalCompletion == nullptr)
    {
        return;
    }
    StartD3D8CallInventoryProbeImpl(callbacks);
}

} // namespace bfvr
