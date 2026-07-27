#include "client/D3D8WeaponTransformOwnershipProbe.h"

#include "stereo/D3D8WeaponDrawPolicy.h"
#include "stereo/WeaponPoseMath.h"

#include <MinHook.h>

#include <windows.h>

#include <array>
#include <cstdarg>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <intrin.h>
#include <optional>

namespace
{

constexpr std::size_t kPresentSlot = 15;
constexpr std::size_t kSetTransformSlot = 37;
constexpr std::size_t kSetRenderStateSlot = 50;
constexpr std::size_t kSetVertexShaderSlot = 76;
constexpr std::size_t kDrawIndexedPrimitiveSlot = 71;

constexpr DWORD kTransformView = 2;
constexpr DWORD kTransformProjection = 3;
constexpr DWORD kTransformWorld = 0x100;
constexpr DWORD kRenderStateZEnable = 7;
constexpr DWORD kRenderStateAlphaBlendEnable = 27;
constexpr DWORD kLifecycleReadyTimeoutMs = 60000;
constexpr DWORD kProbeTimeoutMs = 90000;
constexpr DWORD kEligibleInfantrySettleMs = 8000;
constexpr LONG kActiveFrameCount = 180;
constexpr DWORD kGenericMeshDrawReturn = 0x0062B83FU;
constexpr float kDiagnosticViewSpaceRightOffset = 0.25F;

using PresentFn = HRESULT(WINAPI*)(
    void* device,
    const RECT* sourceRectangle,
    const RECT* destinationRectangle,
    HWND destinationWindowOverride,
    const RGNDATA* dirtyRegion);
using SetTransformFn = HRESULT(WINAPI*)(
    void* device,
    DWORD state,
    const void* matrix);
using SetRenderStateFn = HRESULT(WINAPI*)(void* device, DWORD state, DWORD value);
using SetVertexShaderFn = HRESULT(WINAPI*)(void* device, DWORD shaderOrFvf);
using DrawIndexedPrimitiveFn = HRESULT(WINAPI*)(
    void* device,
    DWORD primitiveType,
    UINT minimumVertexIndex,
    UINT vertexCount,
    UINT startIndex,
    UINT primitiveCount);

struct Matrix
{
    float values[4][4] = {};
};
static_assert(sizeof(Matrix) == sizeof(float) * 16);

struct TrackedState
{
    Matrix world = {};
    Matrix view = {};
    Matrix projection = {};
    DWORD vertexShaderOrFvf = 0;
    DWORD zEnable = 0;
    DWORD alphaBlendEnable = 0;
    bool worldKnown = false;
    bool viewKnown = false;
    bool projectionKnown = false;
};

struct TransformOwnershipProbeRecord
{
    volatile LONG state = 0; // 0=idle, 1=armed, 2=active, 3=complete.
    volatile LONG activeFrames = 0;
    volatile LONG candidateDraws = 0;
    volatile LONG offsetDraws = 0;
    volatile LONG offsetSetFailures = 0;
    volatile LONG restoreFailures = 0;
    volatile LONG mathRejections = 0;
    DWORD eligibleSince = 0;
    DWORD executionThreadId = 0;
    void* device = nullptr;
    void* presentTarget = nullptr;
    void* setTransformTarget = nullptr;
    void* setRenderStateTarget = nullptr;
    void* setVertexShaderTarget = nullptr;
    void* drawIndexedPrimitiveTarget = nullptr;
    TrackedState tracked = {};
};

bfvr::D3D8ObserverCallbacks g_callbacks = {};
bfvr::D3D8ObserverLifecycle g_lifecycle = {};
TransformOwnershipProbeRecord g_probe = {};

PresentFn g_originalPresent = nullptr;
SetTransformFn g_originalSetTransform = nullptr;
SetRenderStateFn g_originalSetRenderState = nullptr;
SetVertexShaderFn g_originalSetVertexShader = nullptr;
DrawIndexedPrimitiveFn g_originalDrawIndexedPrimitive = nullptr;

void AppendLog(const wchar_t* format, ...)
{
    if (g_callbacks.appendLog == nullptr)
    {
        return;
    }
    std::array<wchar_t, 900> message = {};
    va_list arguments;
    va_start(arguments, format);
    _vsnwprintf_s(message.data(), message.size(), _TRUNCATE, format, arguments);
    va_end(arguments);
    g_callbacks.appendLog(message.data());
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
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(target),
            &module))
    {
        return false;
    }
    std::array<wchar_t, MAX_PATH> path = {};
    if (GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size())) == 0)
    {
        return false;
    }
    const wchar_t* fileName = wcsrchr(path.data(), L'\\');
    fileName = fileName == nullptr ? path.data() : fileName + 1;
    return _wcsicmp(fileName, L"d3d8.dll") == 0;
}

bool IsTrackedDeviceThread(void* device)
{
    const LONG state = InterlockedCompareExchange(&g_probe.state, 0, 0);
    return (state == 1 || state == 2) && device == g_probe.device &&
        GetCurrentThreadId() == g_probe.executionThreadId;
}

bool IsActiveDeviceThread(void* device)
{
    return InterlockedCompareExchange(&g_probe.state, 0, 0) == 2 &&
        device == g_probe.device && GetCurrentThreadId() == g_probe.executionThreadId;
}

bool SafeCopy(void* destination, const void* source, std::size_t size)
{
    if (destination == nullptr || source == nullptr || size == 0)
    {
        return false;
    }
    __try
    {
        std::memcpy(destination, source, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool StackSlotsContainAddress(
    const void* const* returnAddressSlot,
    DWORD expectedAddress)
{
    if (returnAddressSlot == nullptr)
    {
        return false;
    }
    constexpr std::size_t kMaximumStackSlots = 32;
    for (std::size_t index = 0; index < kMaximumStackSlots; ++index)
    {
        const void* candidate = nullptr;
        if (!SafeCopy(&candidate, returnAddressSlot + index, sizeof(candidate)))
        {
            return false;
        }
        if (reinterpret_cast<DWORD_PTR>(candidate) == expectedAddress)
        {
            return true;
        }
    }
    return false;
}

bfvr::stereo::Matrix4 ToStereoMatrix(const Matrix& source)
{
    bfvr::stereo::Matrix4 result = {};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            result.values[row][column] = source.values[row][column];
        }
    }
    return result;
}

Matrix ToD3D8Matrix(const bfvr::stereo::Matrix4& source)
{
    Matrix result = {};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            result.values[row][column] = source.values[row][column];
        }
    }
    return result;
}

bool IsFirstPersonProjection(const TrackedState& state)
{
    if (!state.projectionKnown)
    {
        return false;
    }
    const float horizontal = state.projection.values[0][0];
    const float vertical = state.projection.values[1][1];
    return std::isfinite(horizontal) && std::isfinite(vertical) &&
        horizontal >= 2.0F && vertical >= 3.5F;
}

bool IsSharedFixedFunctionWeaponCandidate(
    const TrackedState& state,
    const void* const* returnAddressSlot)
{
    bfvr::stereo::WeaponDrawPolicyInput input = {};
    input.indexedDraw = true;
    input.rendererRoute = StackSlotsContainAddress(
        returnAddressSlot,
        kGenericMeshDrawReturn)
        ? bfvr::stereo::WeaponRendererRoute::GenericMesh
        : bfvr::stereo::WeaponRendererRoute::Unknown;
    input.vertexShaderOrFvf = state.vertexShaderOrFvf;
    input.alphaBlendEnabled = state.alphaBlendEnable != 0;
    input.zEnabled = state.zEnable != 0;
    input.firstPersonProjection = IsFirstPersonProjection(state);
    input.worldKnown = state.worldKnown;
    input.viewKnown = state.viewKnown;
    input.world = ToStereoMatrix(state.world);
    input.view = ToStereoMatrix(state.view);
    return bfvr::stereo::ClassifyWeaponDraw(input, 8.0F) ==
        bfvr::stereo::WeaponDrawDisposition::SharedFixedFunctionWeaponCandidate;
}

std::optional<Matrix> MakeOffsetWorld(const TrackedState& state)
{
    bfvr::stereo::Matrix4 viewSpaceOffset = {};
    for (std::size_t index = 0; index < 4; ++index)
    {
        viewSpaceOffset.values[index][index] = 1.0F;
    }
    viewSpaceOffset.values[3][0] = kDiagnosticViewSpaceRightOffset;
    const auto adjusted = bfvr::stereo::ApplyViewSpaceWeaponDeltaToD3D8World(
        ToStereoMatrix(state.world),
        ToStereoMatrix(state.view),
        viewSpaceOffset);
    return adjusted.has_value()
        ? std::optional<Matrix>(ToD3D8Matrix(*adjusted))
        : std::nullopt;
}

HRESULT WINAPI HookPresent(
    void* device,
    const RECT* sourceRectangle,
    const RECT* destinationRectangle,
    HWND destinationWindowOverride,
    const RGNDATA* dirtyRegion)
{
    const PresentFn original = g_originalPresent;
    const HRESULT result = original == nullptr ? E_FAIL : original(
        device,
        sourceRectangle,
        destinationRectangle,
        destinationWindowOverride,
        dirtyRegion);
    const LONG state = InterlockedCompareExchange(&g_probe.state, 0, 0);
    if (state == 1 && SUCCEEDED(result) && device == g_probe.device &&
        GetCurrentThreadId() == g_probe.executionThreadId)
    {
        const bool eligible = g_callbacks.isCaptureEligible != nullptr &&
            g_callbacks.isCaptureEligible();
        if (!eligible)
        {
            g_probe.eligibleSince = 0;
        }
        else if (g_probe.eligibleSince == 0)
        {
            g_probe.eligibleSince = GetTickCount();
            AppendLog(
                L"Weapon transform-ownership probe observed the local-infantry gate. It will wait %lu ms, then offset only the classified fixed-function weapon candidates rightward by %.2f view units for %ld frames; each original World transform is restored before its draw callback returns.",
                static_cast<unsigned long>(kEligibleInfantrySettleMs),
                static_cast<double>(kDiagnosticViewSpaceRightOffset),
                kActiveFrameCount);
        }
        else if (GetTickCount() - g_probe.eligibleSince >= kEligibleInfantrySettleMs &&
                 InterlockedCompareExchange(&g_probe.state, 2, 1) == 1)
        {
            g_probe.activeFrames = 0;
            g_probe.candidateDraws = 0;
            g_probe.offsetDraws = 0;
            g_probe.offsetSetFailures = 0;
            g_probe.restoreFailures = 0;
            g_probe.mathRejections = 0;
            AppendLog(
                L"Weapon transform-ownership probe is active for %ld frames. Observe whether only the ordinary first-person weapon shifts right; gameplay state remains untouched.",
                kActiveFrameCount);
        }
    }
    else if (state == 2 && device == g_probe.device &&
             GetCurrentThreadId() == g_probe.executionThreadId &&
             InterlockedIncrement(&g_probe.activeFrames) >= kActiveFrameCount)
    {
        MemoryBarrier();
        InterlockedExchange(&g_probe.state, 3);
    }
    return result;
}

HRESULT WINAPI HookSetTransform(void* device, DWORD transformState, const void* matrix)
{
    const SetTransformFn original = g_originalSetTransform;
    const HRESULT result = original == nullptr ? E_FAIL : original(device, transformState, matrix);
    if (!SUCCEEDED(result) || !IsTrackedDeviceThread(device))
    {
        return result;
    }
    Matrix copied = {};
    if (!SafeCopy(&copied, matrix, sizeof(copied)))
    {
        return result;
    }
    if (transformState == kTransformWorld)
    {
        g_probe.tracked.world = copied;
        g_probe.tracked.worldKnown = true;
    }
    else if (transformState == kTransformView)
    {
        g_probe.tracked.view = copied;
        g_probe.tracked.viewKnown = true;
    }
    else if (transformState == kTransformProjection)
    {
        g_probe.tracked.projection = copied;
        g_probe.tracked.projectionKnown = true;
    }
    return result;
}

HRESULT WINAPI HookSetRenderState(void* device, DWORD state, DWORD value)
{
    const SetRenderStateFn original = g_originalSetRenderState;
    const HRESULT result = original == nullptr ? E_FAIL : original(device, state, value);
    if (!SUCCEEDED(result) || !IsTrackedDeviceThread(device))
    {
        return result;
    }
    if (state == kRenderStateZEnable)
    {
        g_probe.tracked.zEnable = value;
    }
    else if (state == kRenderStateAlphaBlendEnable)
    {
        g_probe.tracked.alphaBlendEnable = value;
    }
    return result;
}

HRESULT WINAPI HookSetVertexShader(void* device, DWORD shaderOrFvf)
{
    const SetVertexShaderFn original = g_originalSetVertexShader;
    const HRESULT result = original == nullptr ? E_FAIL : original(device, shaderOrFvf);
    if (SUCCEEDED(result) && IsTrackedDeviceThread(device))
    {
        g_probe.tracked.vertexShaderOrFvf = shaderOrFvf;
    }
    return result;
}

HRESULT WINAPI HookDrawIndexedPrimitive(
    void* device,
    DWORD primitiveType,
    UINT minimumVertexIndex,
    UINT vertexCount,
    UINT startIndex,
    UINT primitiveCount)
{
    void** const returnAddressSlot =
        reinterpret_cast<void**>(_AddressOfReturnAddress());
    const DrawIndexedPrimitiveFn originalDraw = g_originalDrawIndexedPrimitive;
    const SetTransformFn originalSetTransform = g_originalSetTransform;
    if (!IsActiveDeviceThread(device) || originalDraw == nullptr ||
        originalSetTransform == nullptr ||
        !IsSharedFixedFunctionWeaponCandidate(g_probe.tracked, returnAddressSlot))
    {
        return originalDraw == nullptr ? E_FAIL : originalDraw(
            device,
            primitiveType,
            minimumVertexIndex,
            vertexCount,
            startIndex,
            primitiveCount);
    }

    InterlockedIncrement(&g_probe.candidateDraws);
    const auto offsetWorld = MakeOffsetWorld(g_probe.tracked);
    if (!offsetWorld.has_value())
    {
        InterlockedIncrement(&g_probe.mathRejections);
        return originalDraw(
            device,
            primitiveType,
            minimumVertexIndex,
            vertexCount,
            startIndex,
            primitiveCount);
    }

    const Matrix sourceWorld = g_probe.tracked.world;
    if (FAILED(originalSetTransform(device, kTransformWorld, &*offsetWorld)))
    {
        InterlockedIncrement(&g_probe.offsetSetFailures);
        return originalDraw(
            device,
            primitiveType,
            minimumVertexIndex,
            vertexCount,
            startIndex,
            primitiveCount);
    }

    const HRESULT drawResult = originalDraw(
        device,
        primitiveType,
        minimumVertexIndex,
        vertexCount,
        startIndex,
        primitiveCount);
    if (FAILED(originalSetTransform(device, kTransformWorld, &sourceWorld)))
    {
        InterlockedIncrement(&g_probe.restoreFailures);
        InterlockedExchange(&g_probe.state, 3);
    }
    InterlockedIncrement(&g_probe.offsetDraws);
    return drawResult;
}

void RemoveHooks()
{
    const std::array<void*, 5> targets = {
        g_probe.presentTarget,
        g_probe.setTransformTarget,
        g_probe.setRenderStateTarget,
        g_probe.setVertexShaderTarget,
        g_probe.drawIndexedPrimitiveTarget};
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
    if (g_lifecycle.device == nullptr || g_lifecycle.deviceThreadId == 0)
    {
        return false;
    }
    __try
    {
        auto** const vtable = *reinterpret_cast<void***>(g_lifecycle.device);
        if (vtable == nullptr)
        {
            return false;
        }
        g_probe.device = g_lifecycle.device;
        g_probe.executionThreadId = g_lifecycle.deviceThreadId;
        g_probe.presentTarget = vtable[kPresentSlot];
        g_probe.setTransformTarget = vtable[kSetTransformSlot];
        g_probe.setRenderStateTarget = vtable[kSetRenderStateSlot];
        g_probe.setVertexShaderTarget = vtable[kSetVertexShaderSlot];
        g_probe.drawIndexedPrimitiveTarget = vtable[kDrawIndexedPrimitiveSlot];
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    const std::array<void*, 5> targets = {
        g_probe.presentTarget,
        g_probe.setTransformTarget,
        g_probe.setRenderStateTarget,
        g_probe.setVertexShaderTarget,
        g_probe.drawIndexedPrimitiveTarget};
    for (void* target : targets)
    {
        if (!IsSystemD3D8Target(target))
        {
            return false;
        }
    }
    if (MH_Initialize() != MH_OK)
    {
        return false;
    }
    const auto create = [](void* target, LPVOID detour, LPVOID* original)
    {
        return MH_CreateHook(target, detour, original) == MH_OK && *original != nullptr;
    };
    const bool created =
        create(g_probe.presentTarget, reinterpret_cast<LPVOID>(&HookPresent), reinterpret_cast<LPVOID*>(&g_originalPresent)) &&
        create(g_probe.setTransformTarget, reinterpret_cast<LPVOID>(&HookSetTransform), reinterpret_cast<LPVOID*>(&g_originalSetTransform)) &&
        create(g_probe.setRenderStateTarget, reinterpret_cast<LPVOID>(&HookSetRenderState), reinterpret_cast<LPVOID*>(&g_originalSetRenderState)) &&
        create(g_probe.setVertexShaderTarget, reinterpret_cast<LPVOID>(&HookSetVertexShader), reinterpret_cast<LPVOID*>(&g_originalSetVertexShader)) &&
        create(g_probe.drawIndexedPrimitiveTarget, reinterpret_cast<LPVOID>(&HookDrawIndexedPrimitive), reinterpret_cast<LPVOID*>(&g_originalDrawIndexedPrimitive));
    if (!created)
    {
        RemoveHooks();
        MH_Uninitialize();
        return false;
    }
    for (void* target : targets)
    {
        if (MH_EnableHook(target) != MH_OK)
        {
            RemoveHooks();
            MH_Uninitialize();
            return false;
        }
    }
    return true;
}

void Report()
{
    AppendLog(
        L"Weapon transform-ownership probe complete: activeFrames=%ld classifiedCandidates=%ld offsetDraws=%ld offsetSetFailures=%ld restoreFailures=%ld mathRejections=%ld. A clean run proves only that the current classified fixed-function draws accept and restore a temporary World transform; visual observation is still required to establish their image ownership.",
        InterlockedCompareExchange(&g_probe.activeFrames, 0, 0),
        InterlockedCompareExchange(&g_probe.candidateDraws, 0, 0),
        InterlockedCompareExchange(&g_probe.offsetDraws, 0, 0),
        InterlockedCompareExchange(&g_probe.offsetSetFailures, 0, 0),
        InterlockedCompareExchange(&g_probe.restoreFailures, 0, 0),
        InterlockedCompareExchange(&g_probe.mathRejections, 0, 0));
}

DWORD WINAPI RunProbe(void*)
{
    const DWORD lifecycleStarted = GetTickCount();
    while (GetTickCount() - lifecycleStarted < kLifecycleReadyTimeoutMs)
    {
        if (g_callbacks.tryGetReadyLifecycle != nullptr &&
            g_callbacks.tryGetReadyLifecycle(&g_lifecycle))
        {
            break;
        }
        Sleep(10);
    }
    if (!InstallHooks())
    {
        AppendLog(L"Weapon transform-ownership probe skipped: verified direct system-D3D8 lifecycle/hooks were unavailable.");
        SignalCompletion();
        return 0;
    }

    InterlockedExchange(&g_probe.state, 1);
    AppendLog(
        L"Weapon transform-ownership probe armed: it waits for a sustained local-infantry gate and an 8000 ms preparation window. It then applies a temporary +0.25 rightward view-space World delta only to the evidence-classified generic FVF-0x112 weapon candidates for 180 frames, restoring BF1942's original World after every draw.");
    const DWORD probeStarted = GetTickCount();
    while (GetTickCount() - probeStarted < kProbeTimeoutMs &&
           InterlockedCompareExchange(&g_probe.state, 0, 0) != 3)
    {
        Sleep(10);
    }
    if (InterlockedCompareExchange(&g_probe.state, 0, 0) == 3)
    {
        Report();
    }
    else
    {
        AppendLog(L"Weapon transform-ownership probe timed out before an eligible bounded test; no World transform was overridden.");
    }
    InterlockedExchange(&g_probe.state, 0);
    RemoveHooks();
    MH_Uninitialize();
    AppendLog(L"Weapon transform-ownership probe removed all hooks.");
    SignalCompletion();
    return 0;
}

} // namespace

namespace bfvr
{

void StartD3D8WeaponTransformOwnershipProbe(
    const D3D8ObserverCallbacks& callbacks)
{
    if (callbacks.tryGetReadyLifecycle == nullptr ||
        callbacks.isCaptureEligible == nullptr ||
        callbacks.appendLog == nullptr ||
        callbacks.signalCompletion == nullptr)
    {
        return;
    }
    g_callbacks = callbacks;
    g_lifecycle = {};
    g_probe = {};
    HANDLE worker = CreateThread(nullptr, 0, RunProbe, nullptr, 0, nullptr);
    if (worker == nullptr)
    {
        AppendLog(L"Weapon transform-ownership probe could not create its worker thread.");
        SignalCompletion();
        return;
    }
    CloseHandle(worker);
}

} // namespace bfvr
