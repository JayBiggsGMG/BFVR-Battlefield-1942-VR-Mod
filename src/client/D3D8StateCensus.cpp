#include "client/D3D8StateCensus.h"

#include <MinHook.h>

#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cwchar>

namespace
{

constexpr std::size_t kDirect3DDevice8PresentSlot = 15;
constexpr std::size_t kDirect3DDevice8SetViewportSlot = 40;
constexpr std::size_t kDirect3DDevice8SetRenderStateSlot = 50;
constexpr std::size_t kDirect3DDevice8SetTextureSlot = 61;
constexpr std::size_t kDirect3DDevice8SetTextureStageStateSlot = 63;
constexpr std::size_t kDirect3DDevice8SetVertexShaderSlot = 76;
constexpr std::size_t kDirect3DDevice8SetVertexShaderConstantSlot = 79;
constexpr std::size_t kDirect3DDevice8SetStreamSourceSlot = 83;
constexpr std::size_t kDirect3DDevice8SetIndicesSlot = 85;
constexpr std::size_t kDirect3DDevice8SetPixelShaderSlot = 88;
constexpr std::size_t kDirect3DDevice8SetPixelShaderConstantSlot = 91;

using PresentFn = HRESULT(WINAPI*)(void* device, const RECT* sourceRectangle, const RECT* destinationRectangle, HWND destinationWindowOverride, const RGNDATA* dirtyRegion);
using SetViewportFn = HRESULT(WINAPI*)(void* device, const void* viewport);
using SetRenderStateFn = HRESULT(WINAPI*)(void* device, DWORD state, DWORD value);
using SetTextureFn = HRESULT(WINAPI*)(void* device, DWORD stage, void* texture);
using SetTextureStageStateFn = HRESULT(WINAPI*)(void* device, DWORD stage, DWORD type, DWORD value);
using SetVertexShaderFn = HRESULT(WINAPI*)(void* device, DWORD vertexShaderOrFvf);
using SetVertexShaderConstantFn = HRESULT(WINAPI*)(void* device, DWORD registerIndex, const void* constantData, DWORD vector4Count);
using SetStreamSourceFn = HRESULT(WINAPI*)(void* device, UINT streamNumber, void* streamData, UINT stride);
using SetIndicesFn = HRESULT(WINAPI*)(void* device, void* indexData, UINT baseVertexIndex);
using SetPixelShaderFn = HRESULT(WINAPI*)(void* device, DWORD pixelShader);
using SetPixelShaderConstantFn = HRESULT(WINAPI*)(void* device, DWORD registerIndex, const void* constantData, DWORD vector4Count);

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
    _vsnwprintf_s(message, _countof(message), _TRUNCATE, format, arguments);
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
    if (GetModuleFileNameW(module, modulePath, _countof(modulePath)) == 0)
    {
        return false;
    }

    const wchar_t* fileName = wcsrchr(modulePath, L'\\');
    fileName = fileName == nullptr ? modulePath : fileName + 1;
    return _wcsicmp(fileName, L"d3d8.dll") == 0;
}

// Direct3D 8 exposes the fixed setter sequence above in IDirect3DDevice8.
// Each hook calls that original system-D3D8 entry before writing its POD event.
// No detour dereferences a game-owned resource, obtains state, or writes state.
constexpr LONG kD3D8StateCensusMaximumEvents = 4096;
constexpr DWORD kD3D8StateCensusCaptureTimeoutMs = 45000;

enum class D3D8StateCensusEventKind : DWORD
{
    SetViewport,
    SetRenderState,
    SetTexture,
    SetTextureStageState,
    SetVertexShaderOrFvf,
    SetVertexShaderConstant,
    SetStreamSource,
    SetIndices,
    SetPixelShader,
    SetPixelShaderConstant
};

struct D3D8StateCensusEvent
{
    LONG sequence = 0;
    D3D8StateCensusEventKind kind = D3D8StateCensusEventKind::SetViewport;
    HRESULT result = E_FAIL;
    DWORD first = 0;
    DWORD second = 0;
    DWORD third = 0;
    const void* opaquePointer = nullptr;
};

struct D3D8StateCensusRecord
{
    volatile LONG state = 0; // 0=idle, 1=armed, 2=capturing, 3=complete.
    volatile LONG eventCount = 0;
    volatile LONG eventOverflow = 0;
    DWORD executionThreadId = 0;
    void* device = nullptr;
    void* presentTarget = nullptr;
    void* setViewportTarget = nullptr;
    void* setRenderStateTarget = nullptr;
    void* setTextureTarget = nullptr;
    void* setTextureStageStateTarget = nullptr;
    void* setVertexShaderTarget = nullptr;
    void* setVertexShaderConstantTarget = nullptr;
    void* setStreamSourceTarget = nullptr;
    void* setIndicesTarget = nullptr;
    void* setPixelShaderTarget = nullptr;
    void* setPixelShaderConstantTarget = nullptr;
    LONGLONG startCounter = 0;
    LONGLONG endCounter = 0;
    D3D8StateCensusEvent events[kD3D8StateCensusMaximumEvents] = {};
};

D3D8StateCensusRecord g_d3d8StateCensus = {};
PresentFn g_originalPresentForD3D8StateCensus = nullptr;
SetViewportFn g_originalSetViewportForD3D8StateCensus = nullptr;
SetRenderStateFn g_originalSetRenderStateForD3D8StateCensus = nullptr;
SetTextureFn g_originalSetTextureForD3D8StateCensus = nullptr;
SetTextureStageStateFn g_originalSetTextureStageStateForD3D8StateCensus = nullptr;
SetVertexShaderFn g_originalSetVertexShaderForD3D8StateCensus = nullptr;
SetVertexShaderConstantFn g_originalSetVertexShaderConstantForD3D8StateCensus = nullptr;
SetStreamSourceFn g_originalSetStreamSourceForD3D8StateCensus = nullptr;
SetIndicesFn g_originalSetIndicesForD3D8StateCensus = nullptr;
SetPixelShaderFn g_originalSetPixelShaderForD3D8StateCensus = nullptr;
SetPixelShaderConstantFn g_originalSetPixelShaderConstantForD3D8StateCensus = nullptr;

const wchar_t* D3D8StateCensusEventKindName(D3D8StateCensusEventKind kind)
{
    switch (kind)
    {
    case D3D8StateCensusEventKind::SetViewport:
        return L"SetViewport";
    case D3D8StateCensusEventKind::SetRenderState:
        return L"SetRenderState";
    case D3D8StateCensusEventKind::SetTexture:
        return L"SetTexture";
    case D3D8StateCensusEventKind::SetTextureStageState:
        return L"SetTextureStageState";
    case D3D8StateCensusEventKind::SetVertexShaderOrFvf:
        return L"SetVertexShader/FVF";
    case D3D8StateCensusEventKind::SetVertexShaderConstant:
        return L"SetVertexShaderConstant";
    case D3D8StateCensusEventKind::SetStreamSource:
        return L"SetStreamSource";
    case D3D8StateCensusEventKind::SetIndices:
        return L"SetIndices";
    case D3D8StateCensusEventKind::SetPixelShader:
        return L"SetPixelShader";
    case D3D8StateCensusEventKind::SetPixelShaderConstant:
        return L"SetPixelShaderConstant";
    }
    return L"unknown";
}

bool IsD3D8StateCensusCaptureOnDeviceThread(void* device)
{
    return InterlockedCompareExchange(&g_d3d8StateCensus.state, 0, 0) == 2 &&
        device == g_d3d8StateCensus.device &&
        GetCurrentThreadId() == g_d3d8StateCensus.executionThreadId;
}

void RecordD3D8StateCensusEvent(
    D3D8StateCensusEventKind kind,
    void* device,
    HRESULT result,
    DWORD first = 0,
    DWORD second = 0,
    DWORD third = 0,
    const void* opaquePointer = nullptr)
{
    if (!IsD3D8StateCensusCaptureOnDeviceThread(device))
    {
        return;
    }

    const LONG eventIndex = InterlockedIncrement(&g_d3d8StateCensus.eventCount) - 1;
    if (eventIndex < 0 || eventIndex >= kD3D8StateCensusMaximumEvents)
    {
        InterlockedExchange(&g_d3d8StateCensus.eventOverflow, 1);
        return;
    }

    D3D8StateCensusEvent& event = g_d3d8StateCensus.events[eventIndex];
    event.sequence = eventIndex + 1;
    event.kind = kind;
    event.result = result;
    event.first = first;
    event.second = second;
    event.third = third;
    event.opaquePointer = opaquePointer;
}

HRESULT WINAPI HookPresentD3D8StateCensus(
    void* device,
    const RECT* sourceRectangle,
    const RECT* destinationRectangle,
    HWND destinationWindowOverride,
    const RGNDATA* dirtyRegion)
{
    const PresentFn originalPresent = g_originalPresentForD3D8StateCensus;
    const HRESULT result = originalPresent == nullptr
        ? E_FAIL
        : originalPresent(device, sourceRectangle, destinationRectangle, destinationWindowOverride, dirtyRegion);

    const LONG priorState = InterlockedCompareExchange(&g_d3d8StateCensus.state, 0, 0);
    if (priorState == 1 &&
        SUCCEEDED(result) &&
        device == g_d3d8StateCensus.device &&
        GetCurrentThreadId() == g_d3d8StateCensus.executionThreadId &&
        g_callbacks.isCaptureEligible != nullptr &&
        g_callbacks.isCaptureEligible() &&
        InterlockedCompareExchange(&g_d3d8StateCensus.state, 2, 1) == 1)
    {
        InterlockedExchange(&g_d3d8StateCensus.eventCount, 0);
        InterlockedExchange(&g_d3d8StateCensus.eventOverflow, 0);
        LARGE_INTEGER counter = {};
        if (QueryPerformanceCounter(&counter))
        {
            g_d3d8StateCensus.startCounter = counter.QuadPart;
        }
    }
    else if (priorState == 2 &&
             device == g_d3d8StateCensus.device &&
             GetCurrentThreadId() == g_d3d8StateCensus.executionThreadId)
    {
        LARGE_INTEGER counter = {};
        if (QueryPerformanceCounter(&counter))
        {
            g_d3d8StateCensus.endCounter = counter.QuadPart;
        }
        MemoryBarrier();
        InterlockedExchange(&g_d3d8StateCensus.state, 3);
    }
    return result;
}

HRESULT WINAPI HookSetViewportD3D8StateCensus(void* device, const void* viewport)
{
    const SetViewportFn original = g_originalSetViewportForD3D8StateCensus;
    const HRESULT result = original == nullptr ? E_FAIL : original(device, viewport);
    RecordD3D8StateCensusEvent(D3D8StateCensusEventKind::SetViewport, device, result, 0, 0, 0, viewport);
    return result;
}

HRESULT WINAPI HookSetRenderStateD3D8StateCensus(void* device, DWORD state, DWORD value)
{
    const SetRenderStateFn original = g_originalSetRenderStateForD3D8StateCensus;
    const HRESULT result = original == nullptr ? E_FAIL : original(device, state, value);
    RecordD3D8StateCensusEvent(D3D8StateCensusEventKind::SetRenderState, device, result, state, value);
    return result;
}

HRESULT WINAPI HookSetTextureD3D8StateCensus(void* device, DWORD stage, void* texture)
{
    const SetTextureFn original = g_originalSetTextureForD3D8StateCensus;
    const HRESULT result = original == nullptr ? E_FAIL : original(device, stage, texture);
    RecordD3D8StateCensusEvent(D3D8StateCensusEventKind::SetTexture, device, result, stage, 0, 0, texture);
    return result;
}

HRESULT WINAPI HookSetTextureStageStateD3D8StateCensus(void* device, DWORD stage, DWORD type, DWORD value)
{
    const SetTextureStageStateFn original = g_originalSetTextureStageStateForD3D8StateCensus;
    const HRESULT result = original == nullptr ? E_FAIL : original(device, stage, type, value);
    RecordD3D8StateCensusEvent(D3D8StateCensusEventKind::SetTextureStageState, device, result, stage, type, value);
    return result;
}

HRESULT WINAPI HookSetVertexShaderD3D8StateCensus(void* device, DWORD vertexShaderOrFvf)
{
    const SetVertexShaderFn original = g_originalSetVertexShaderForD3D8StateCensus;
    const HRESULT result = original == nullptr ? E_FAIL : original(device, vertexShaderOrFvf);
    RecordD3D8StateCensusEvent(D3D8StateCensusEventKind::SetVertexShaderOrFvf, device, result, vertexShaderOrFvf);
    return result;
}

HRESULT WINAPI HookSetVertexShaderConstantD3D8StateCensus(void* device, DWORD registerIndex, const void* constantData, DWORD vector4Count)
{
    const SetVertexShaderConstantFn original = g_originalSetVertexShaderConstantForD3D8StateCensus;
    const HRESULT result = original == nullptr ? E_FAIL : original(device, registerIndex, constantData, vector4Count);
    RecordD3D8StateCensusEvent(D3D8StateCensusEventKind::SetVertexShaderConstant, device, result, registerIndex, vector4Count, 0, constantData);
    return result;
}

HRESULT WINAPI HookSetStreamSourceD3D8StateCensus(void* device, UINT streamNumber, void* streamData, UINT stride)
{
    const SetStreamSourceFn original = g_originalSetStreamSourceForD3D8StateCensus;
    const HRESULT result = original == nullptr ? E_FAIL : original(device, streamNumber, streamData, stride);
    RecordD3D8StateCensusEvent(D3D8StateCensusEventKind::SetStreamSource, device, result, streamNumber, stride, 0, streamData);
    return result;
}

HRESULT WINAPI HookSetIndicesD3D8StateCensus(void* device, void* indexData, UINT baseVertexIndex)
{
    const SetIndicesFn original = g_originalSetIndicesForD3D8StateCensus;
    const HRESULT result = original == nullptr ? E_FAIL : original(device, indexData, baseVertexIndex);
    RecordD3D8StateCensusEvent(D3D8StateCensusEventKind::SetIndices, device, result, baseVertexIndex, 0, 0, indexData);
    return result;
}

HRESULT WINAPI HookSetPixelShaderD3D8StateCensus(void* device, DWORD pixelShader)
{
    const SetPixelShaderFn original = g_originalSetPixelShaderForD3D8StateCensus;
    const HRESULT result = original == nullptr ? E_FAIL : original(device, pixelShader);
    RecordD3D8StateCensusEvent(D3D8StateCensusEventKind::SetPixelShader, device, result, pixelShader);
    return result;
}

HRESULT WINAPI HookSetPixelShaderConstantD3D8StateCensus(void* device, DWORD registerIndex, const void* constantData, DWORD vector4Count)
{
    const SetPixelShaderConstantFn original = g_originalSetPixelShaderConstantForD3D8StateCensus;
    const HRESULT result = original == nullptr ? E_FAIL : original(device, registerIndex, constantData, vector4Count);
    RecordD3D8StateCensusEvent(D3D8StateCensusEventKind::SetPixelShaderConstant, device, result, registerIndex, vector4Count, 0, constantData);
    return result;
}

void RemoveD3D8StateCensusHooks()
{
    void* const targets[] = {
        g_d3d8StateCensus.presentTarget,
        g_d3d8StateCensus.setViewportTarget,
        g_d3d8StateCensus.setRenderStateTarget,
        g_d3d8StateCensus.setTextureTarget,
        g_d3d8StateCensus.setTextureStageStateTarget,
        g_d3d8StateCensus.setVertexShaderTarget,
        g_d3d8StateCensus.setVertexShaderConstantTarget,
        g_d3d8StateCensus.setStreamSourceTarget,
        g_d3d8StateCensus.setIndicesTarget,
        g_d3d8StateCensus.setPixelShaderTarget,
        g_d3d8StateCensus.setPixelShaderConstantTarget};
    for (void* target : targets)
    {
        if (target != nullptr)
        {
            MH_DisableHook(target);
            MH_RemoveHook(target);
        }
    }
}

bool InstallD3D8StateCensusHooks()
{
    if (g_lifecycle.device == nullptr)
    {
        return false;
    }

    __try
    {
        auto** const deviceVtable = *reinterpret_cast<void***>(g_lifecycle.device);
        if (deviceVtable == nullptr)
        {
            return false;
        }
        g_d3d8StateCensus.device = g_lifecycle.device;
        g_d3d8StateCensus.executionThreadId = g_lifecycle.deviceThreadId;
        g_d3d8StateCensus.presentTarget = deviceVtable[kDirect3DDevice8PresentSlot];
        g_d3d8StateCensus.setViewportTarget = deviceVtable[kDirect3DDevice8SetViewportSlot];
        g_d3d8StateCensus.setRenderStateTarget = deviceVtable[kDirect3DDevice8SetRenderStateSlot];
        g_d3d8StateCensus.setTextureTarget = deviceVtable[kDirect3DDevice8SetTextureSlot];
        g_d3d8StateCensus.setTextureStageStateTarget = deviceVtable[kDirect3DDevice8SetTextureStageStateSlot];
        g_d3d8StateCensus.setVertexShaderTarget = deviceVtable[kDirect3DDevice8SetVertexShaderSlot];
        g_d3d8StateCensus.setVertexShaderConstantTarget = deviceVtable[kDirect3DDevice8SetVertexShaderConstantSlot];
        g_d3d8StateCensus.setStreamSourceTarget = deviceVtable[kDirect3DDevice8SetStreamSourceSlot];
        g_d3d8StateCensus.setIndicesTarget = deviceVtable[kDirect3DDevice8SetIndicesSlot];
        g_d3d8StateCensus.setPixelShaderTarget = deviceVtable[kDirect3DDevice8SetPixelShaderSlot];
        g_d3d8StateCensus.setPixelShaderConstantTarget = deviceVtable[kDirect3DDevice8SetPixelShaderConstantSlot];
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    void* const targets[] = {
        g_d3d8StateCensus.presentTarget,
        g_d3d8StateCensus.setViewportTarget,
        g_d3d8StateCensus.setRenderStateTarget,
        g_d3d8StateCensus.setTextureTarget,
        g_d3d8StateCensus.setTextureStageStateTarget,
        g_d3d8StateCensus.setVertexShaderTarget,
        g_d3d8StateCensus.setVertexShaderConstantTarget,
        g_d3d8StateCensus.setStreamSourceTarget,
        g_d3d8StateCensus.setIndicesTarget,
        g_d3d8StateCensus.setPixelShaderTarget,
        g_d3d8StateCensus.setPixelShaderConstantTarget};
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
        AppendLog(L"D3D8 state census skipped: MH_Initialize failed (%d).", static_cast<int>(initializeStatus));
        return false;
    }

    const auto createHook = [](void* target, LPVOID detour, LPVOID* original) -> bool
    {
        return MH_CreateHook(target, detour, original) == MH_OK && *original != nullptr;
    };
    const bool created =
        createHook(g_d3d8StateCensus.presentTarget, reinterpret_cast<LPVOID>(&HookPresentD3D8StateCensus), reinterpret_cast<LPVOID*>(&g_originalPresentForD3D8StateCensus)) &&
        createHook(g_d3d8StateCensus.setViewportTarget, reinterpret_cast<LPVOID>(&HookSetViewportD3D8StateCensus), reinterpret_cast<LPVOID*>(&g_originalSetViewportForD3D8StateCensus)) &&
        createHook(g_d3d8StateCensus.setRenderStateTarget, reinterpret_cast<LPVOID>(&HookSetRenderStateD3D8StateCensus), reinterpret_cast<LPVOID*>(&g_originalSetRenderStateForD3D8StateCensus)) &&
        createHook(g_d3d8StateCensus.setTextureTarget, reinterpret_cast<LPVOID>(&HookSetTextureD3D8StateCensus), reinterpret_cast<LPVOID*>(&g_originalSetTextureForD3D8StateCensus)) &&
        createHook(g_d3d8StateCensus.setTextureStageStateTarget, reinterpret_cast<LPVOID>(&HookSetTextureStageStateD3D8StateCensus), reinterpret_cast<LPVOID*>(&g_originalSetTextureStageStateForD3D8StateCensus)) &&
        createHook(g_d3d8StateCensus.setVertexShaderTarget, reinterpret_cast<LPVOID>(&HookSetVertexShaderD3D8StateCensus), reinterpret_cast<LPVOID*>(&g_originalSetVertexShaderForD3D8StateCensus)) &&
        createHook(g_d3d8StateCensus.setVertexShaderConstantTarget, reinterpret_cast<LPVOID>(&HookSetVertexShaderConstantD3D8StateCensus), reinterpret_cast<LPVOID*>(&g_originalSetVertexShaderConstantForD3D8StateCensus)) &&
        createHook(g_d3d8StateCensus.setStreamSourceTarget, reinterpret_cast<LPVOID>(&HookSetStreamSourceD3D8StateCensus), reinterpret_cast<LPVOID*>(&g_originalSetStreamSourceForD3D8StateCensus)) &&
        createHook(g_d3d8StateCensus.setIndicesTarget, reinterpret_cast<LPVOID>(&HookSetIndicesD3D8StateCensus), reinterpret_cast<LPVOID*>(&g_originalSetIndicesForD3D8StateCensus)) &&
        createHook(g_d3d8StateCensus.setPixelShaderTarget, reinterpret_cast<LPVOID>(&HookSetPixelShaderD3D8StateCensus), reinterpret_cast<LPVOID*>(&g_originalSetPixelShaderForD3D8StateCensus)) &&
        createHook(g_d3d8StateCensus.setPixelShaderConstantTarget, reinterpret_cast<LPVOID>(&HookSetPixelShaderConstantD3D8StateCensus), reinterpret_cast<LPVOID*>(&g_originalSetPixelShaderConstantForD3D8StateCensus));
    if (!created)
    {
        AppendLog(L"D3D8 state census skipped: at least one forwarding hook could not be created; all created census hooks will be removed.");
        RemoveD3D8StateCensusHooks();
        MH_Uninitialize();
        return false;
    }

    for (void* target : targets)
    {
        if (MH_EnableHook(target) != MH_OK)
        {
            AppendLog(L"D3D8 state census skipped: at least one forwarding hook could not be enabled; all census hooks will be removed.");
            RemoveD3D8StateCensusHooks();
            MH_Uninitialize();
            return false;
        }
    }
    return true;
}

void ReportD3D8StateCensus()
{
    const LONG requestedEventCount = InterlockedCompareExchange(&g_d3d8StateCensus.eventCount, 0, 0);
    const LONG eventCount = std::clamp<LONG>(requestedEventCount, 0, kD3D8StateCensusMaximumEvents);
    LONG counts[10] = {};
    for (LONG index = 0; index < eventCount; ++index)
    {
        const DWORD kindIndex = static_cast<DWORD>(g_d3d8StateCensus.events[index].kind);
        if (kindIndex < _countof(counts))
        {
            ++counts[kindIndex];
        }
    }

    LARGE_INTEGER frequency = {};
    QueryPerformanceFrequency(&frequency);
    const double durationMilliseconds = frequency.QuadPart != 0 && g_d3d8StateCensus.endCounter >= g_d3d8StateCensus.startCounter
        ? static_cast<double>(g_d3d8StateCensus.endCounter - g_d3d8StateCensus.startCounter) * 1000.0 / static_cast<double>(frequency.QuadPart)
        : 0.0;
    AppendLog(
        L"D3D8 one-frame state census complete: device=%p thread=%lu events=%ld/%ld overflow=%ld span=%.3f ms viewport=%ld renderState=%ld texture=%ld textureStageState=%ld vertexShaderOrFvf=%ld vertexConstants=%ld streamSource=%ld indices=%ld pixelShader=%ld pixelConstants=%ld. Every record is emitted below with only forwarded-call HRESULTs, values, opaque pointers, and counts.",
        g_d3d8StateCensus.device,
        g_d3d8StateCensus.executionThreadId,
        eventCount,
        kD3D8StateCensusMaximumEvents,
        InterlockedCompareExchange(&g_d3d8StateCensus.eventOverflow, 0, 0),
        durationMilliseconds,
        counts[0], counts[1], counts[2], counts[3], counts[4], counts[5], counts[6], counts[7], counts[8], counts[9]);

    for (LONG index = 0; index < eventCount; ++index)
    {
        const D3D8StateCensusEvent& event = g_d3d8StateCensus.events[index];
        AppendLog(
            L"D3D8 state census [%ld] %s result=0x%08lX first=%lu second=%lu third=%lu pointer=%p.",
            event.sequence,
            D3D8StateCensusEventKindName(event.kind),
            static_cast<unsigned long>(event.result),
            static_cast<unsigned long>(event.first),
            static_cast<unsigned long>(event.second),
            static_cast<unsigned long>(event.third),
            event.opaquePointer);
    }
}

DWORD WINAPI RunD3D8StateCensusProbe(void*)
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
        AppendLog(L"D3D8 state census skipped: the verified CreateDevice/Reset/Present lifecycle did not complete within %lu ms.", kLifecycleReadyTimeoutMs);
        SignalCompletion();
        return 0;
    }
    if (!InstallD3D8StateCensusHooks())
    {
        AppendLog(L"D3D8 state census skipped: required device methods were not all direct system-d3d8 forwarding targets.");
        SignalCompletion();
        return 0;
    }

    InterlockedExchange(&g_d3d8StateCensus.state, 1);
    AppendLog(
        L"Enabled D3D8 one-frame state census: Present=%p SetViewport=%p SetRenderState=%p SetTexture=%p SetTextureStageState=%p SetVertexShader=%p SetVertexShaderConstant=%p SetStreamSource=%p SetIndices=%p SetPixelShader=%p SetPixelShaderConstant=%p. It waits for the caller's sustained passive local-BFPlayer isAlive gate, forwards every game call unchanged, reads no D3D state or resource, and creates no D3D or OpenXR object.",
        g_d3d8StateCensus.presentTarget,
        g_d3d8StateCensus.setViewportTarget,
        g_d3d8StateCensus.setRenderStateTarget,
        g_d3d8StateCensus.setTextureTarget,
        g_d3d8StateCensus.setTextureStageStateTarget,
        g_d3d8StateCensus.setVertexShaderTarget,
        g_d3d8StateCensus.setVertexShaderConstantTarget,
        g_d3d8StateCensus.setStreamSourceTarget,
        g_d3d8StateCensus.setIndicesTarget,
        g_d3d8StateCensus.setPixelShaderTarget,
        g_d3d8StateCensus.setPixelShaderConstantTarget);

    const DWORD captureStartedAt = GetTickCount();
    while (GetTickCount() - captureStartedAt < kD3D8StateCensusCaptureTimeoutMs &&
           InterlockedCompareExchange(&g_d3d8StateCensus.state, 0, 0) != 3)
    {
        Sleep(10);
    }
    if (InterlockedCompareExchange(&g_d3d8StateCensus.state, 0, 0) == 3)
    {
        ReportD3D8StateCensus();
    }
    else
    {
        AppendLog(L"D3D8 state census did not observe a sustained-local-BFPlayer-isAlive-gated Present-to-Present frame within %lu ms; no D3D8 state or resource was changed.", kD3D8StateCensusCaptureTimeoutMs);
    }

    InterlockedExchange(&g_d3d8StateCensus.state, 0);
    RemoveD3D8StateCensusHooks();
    MH_Uninitialize();
    AppendLog(L"D3D8 state census removed all forwarding hooks after its bounded window.");
    SignalCompletion();
    return 0;
}

void StartD3D8StateCensusProbeImpl(const bfvr::D3D8ObserverCallbacks& callbacks)
{
    g_callbacks = callbacks;
    g_lifecycle = {};
    HANDLE worker = CreateThread(nullptr, 0, RunD3D8StateCensusProbe, nullptr, 0, nullptr);
    if (worker == nullptr)
    {
        AppendLog(L"D3D8 state census worker could not start (%lu); no D3D8 code was patched.", GetLastError());
        SignalCompletion();
        return;
    }
    CloseHandle(worker);
    AppendLog(L"Requested the explicit one-frame, no-HMD D3D8 state census; it is forwarding-only and will not run until the verified lifecycle and sustained local-BFPlayer isAlive gate are both available.");
}

} // namespace

namespace bfvr
{

void StartD3D8StateCensusProbe(const D3D8ObserverCallbacks& callbacks)
{
    if (callbacks.tryGetReadyLifecycle == nullptr ||
        callbacks.isCaptureEligible == nullptr ||
        callbacks.appendLog == nullptr ||
        callbacks.signalCompletion == nullptr)
    {
        return;
    }
    StartD3D8StateCensusProbeImpl(callbacks);
}

} // namespace bfvr
