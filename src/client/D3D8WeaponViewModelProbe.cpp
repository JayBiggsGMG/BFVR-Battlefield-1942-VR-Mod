#include "client/D3D8WeaponViewModelProbe.h"
#include "stereo/D3D8WeaponDrawPolicy.h"

#include <MinHook.h>

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <intrin.h>
#include <iterator>

namespace
{

constexpr std::size_t kPresentSlot = 15;
constexpr std::size_t kSetTransformSlot = 37;
constexpr std::size_t kSetRenderStateSlot = 50;
constexpr std::size_t kSetTextureSlot = 61;
constexpr std::size_t kSetVertexShaderSlot = 76;
constexpr std::size_t kSetVertexShaderConstantSlot = 79;
constexpr std::size_t kSetStreamSourceSlot = 83;
constexpr std::size_t kSetIndicesSlot = 85;
constexpr std::size_t kSetPixelShaderSlot = 88;
constexpr std::size_t kDrawPrimitiveSlot = 70;
constexpr std::size_t kDrawIndexedPrimitiveSlot = 71;
constexpr std::size_t kDrawPrimitiveUpSlot = 72;
constexpr std::size_t kDrawIndexedPrimitiveUpSlot = 73;

constexpr DWORD kTransformView = 2;
constexpr DWORD kTransformProjection = 3;
constexpr DWORD kTransformWorld = 0x100;
constexpr DWORD kRenderStateZEnable = 7;
constexpr DWORD kRenderStateZWriteEnable = 14;
constexpr DWORD kRenderStateAlphaBlendEnable = 27;
constexpr DWORD kRenderStateLighting = 137;
constexpr DWORD kLifecycleReadyTimeoutMs = 60000;
constexpr DWORD kCaptureTimeoutMs = 90000;
constexpr DWORD kEligibleInfantrySettleMs = 8000;
constexpr LONG kCapturedFrameCount = 240;
constexpr LONG kMaximumSignatureGroups = 384;
constexpr DWORD kRendererRouteUnknown = 0;
constexpr DWORD kRendererRouteAnimatedMesh = 1;
constexpr DWORD kRendererRouteGenericMesh = 2;
constexpr DWORD kAnimatedMeshDrawReturn = 0x005AF40FU;
constexpr DWORD kGenericMeshDrawReturn = 0x0062B83FU;

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
using SetTextureFn = HRESULT(WINAPI*)(void* device, DWORD stage, void* texture);
using SetVertexShaderFn = HRESULT(WINAPI*)(void* device, DWORD shaderOrFvf);
using SetVertexShaderConstantFn = HRESULT(WINAPI*)(
    void* device,
    DWORD firstRegister,
    const void* data,
    DWORD registerCount);
using SetStreamSourceFn = HRESULT(WINAPI*)(
    void* device,
    UINT streamNumber,
    void* streamData,
    UINT stride);
using SetIndicesFn = HRESULT(WINAPI*)(
    void* device,
    void* indexData,
    UINT baseVertexIndex);
using SetPixelShaderFn = HRESULT(WINAPI*)(void* device, DWORD shader);
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
using DrawPrimitiveUpFn = HRESULT(WINAPI*)(
    void* device,
    DWORD primitiveType,
    UINT primitiveCount,
    const void* vertexData,
    UINT vertexStride);
using DrawIndexedPrimitiveUpFn = HRESULT(WINAPI*)(
    void* device,
    DWORD primitiveType,
    UINT minimumVertexIndex,
    UINT vertexCount,
    UINT primitiveCount,
    const void* indexData,
    UINT indexFormat,
    const void* vertexData,
    UINT vertexStride);

struct Matrix
{
    float values[4][4] = {};
};
static_assert(sizeof(Matrix) == sizeof(float) * 16);

enum class DrawKind : DWORD
{
    Primitive,
    IndexedPrimitive,
    PrimitiveUp,
    IndexedPrimitiveUp
};

struct TrackedState
{
    Matrix world = {};
    Matrix view = {};
    Matrix projection = {};
    float vertexConstants[4][4] = {};
    DWORD vertexConstantMask = 0;
    DWORD vertexShaderOrFvf = 0;
    DWORD pixelShader = 0;
    void* texture0 = nullptr;
    void* stream0 = nullptr;
    UINT stream0Stride = 0;
    void* indices = nullptr;
    UINT baseVertexIndex = 0;
    DWORD zEnable = 0;
    DWORD zWriteEnable = 0;
    DWORD alphaBlendEnable = 0;
    DWORD lighting = 0;
    bool worldKnown = false;
    bool viewKnown = false;
    bool projectionKnown = false;
};

struct DrawKey
{
    DrawKind kind = DrawKind::Primitive;
    DWORD callerReturn = 0;
    DWORD primitiveType = 0;
    UINT primitiveCount = 0;
    UINT vertexCount = 0;
    DWORD vertexShaderOrFvf = 0;
    DWORD pixelShader = 0;
    UINT stream0Stride = 0;
    void* stream0 = nullptr;
    void* indices = nullptr;
    void* texture0 = nullptr;
    DWORD zEnable = 0;
    DWORD alphaBlendEnable = 0;
    DWORD rendererRoute = kRendererRouteUnknown;
    bool firstPersonProjection = false;
};

struct DrawSignatureGroup
{
    bool occupied = false;
    DrawKey key = {};
    DWORD priority = 0;
    bool sharedFixedFunctionWeaponCandidate = false;
    LONG calls = 0;
    LONG firstFrame = 0;
    LONG lastFrame = 0;
    DWORD sourceWvpHash = 0;
    DWORD sourceWvpMask = 0;
    Matrix world = {};
    Matrix view = {};
    Matrix projection = {};
    float vertexConstants[4][4] = {};
    std::array<DWORD, 6> gameStack = {};
    UINT gameStackDepth = 0;
    bool worldKnown = false;
    bool viewKnown = false;
    bool projectionKnown = false;
};

struct WeaponViewModelProbeRecord
{
    volatile LONG state = 0; // 0=idle, 1=armed, 2=capturing, 3=complete.
    volatile LONG captureFrame = 0;
    volatile LONG drawCalls = 0;
    volatile LONG groupOverflow = 0;
    volatile LONG groupEvictions = 0;
    DWORD eligibleSince = 0;
    DWORD executionThreadId = 0;
    void* device = nullptr;
    void* presentTarget = nullptr;
    void* setTransformTarget = nullptr;
    void* setRenderStateTarget = nullptr;
    void* setTextureTarget = nullptr;
    void* setVertexShaderTarget = nullptr;
    void* setVertexShaderConstantTarget = nullptr;
    void* setStreamSourceTarget = nullptr;
    void* setIndicesTarget = nullptr;
    void* setPixelShaderTarget = nullptr;
    void* drawPrimitiveTarget = nullptr;
    void* drawIndexedPrimitiveTarget = nullptr;
    void* drawPrimitiveUpTarget = nullptr;
    void* drawIndexedPrimitiveUpTarget = nullptr;
    TrackedState tracked = {};
    std::array<DrawSignatureGroup, kMaximumSignatureGroups> groups = {};
};

bfvr::D3D8ObserverCallbacks g_callbacks = {};
bfvr::D3D8ObserverLifecycle g_lifecycle = {};
WeaponViewModelProbeRecord g_probe = {};
DWORD_PTR g_gameImageBegin = 0;
DWORD_PTR g_gameImageEnd = 0;

PresentFn g_originalPresent = nullptr;
SetTransformFn g_originalSetTransform = nullptr;
SetRenderStateFn g_originalSetRenderState = nullptr;
SetTextureFn g_originalSetTexture = nullptr;
SetVertexShaderFn g_originalSetVertexShader = nullptr;
SetVertexShaderConstantFn g_originalSetVertexShaderConstant = nullptr;
SetStreamSourceFn g_originalSetStreamSource = nullptr;
SetIndicesFn g_originalSetIndices = nullptr;
SetPixelShaderFn g_originalSetPixelShader = nullptr;
DrawPrimitiveFn g_originalDrawPrimitive = nullptr;
DrawIndexedPrimitiveFn g_originalDrawIndexedPrimitive = nullptr;
DrawPrimitiveUpFn g_originalDrawPrimitiveUp = nullptr;
DrawIndexedPrimitiveUpFn g_originalDrawIndexedPrimitiveUp = nullptr;

void AppendLog(const wchar_t* format, ...)
{
    if (g_callbacks.appendLog == nullptr)
    {
        return;
    }
    std::array<wchar_t, 1400> message = {};
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

bool IsCaptureOnDeviceThread(void* device)
{
    return InterlockedCompareExchange(&g_probe.state, 0, 0) == 2 &&
        device == g_probe.device &&
        GetCurrentThreadId() == g_probe.executionThreadId;
}

bool SafeCopy(void* destination, const void* source, std::size_t size);

bool IsGameExecutableAddress(DWORD_PTR address)
{
    if (address < g_gameImageBegin || address >= g_gameImageEnd)
    {
        return false;
    }
    MEMORY_BASIC_INFORMATION information = {};
    if (VirtualQuery(
            reinterpret_cast<const void*>(address),
            &information,
            sizeof(information)) != sizeof(information) ||
        information.State != MEM_COMMIT)
    {
        return false;
    }
    switch (information.Protect & 0xFFU)
    {
    case PAGE_EXECUTE:
    case PAGE_EXECUTE_READ:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
        return true;
    default:
        return false;
    }
}

void AppendGameStackAddress(DrawSignatureGroup& group, const void* candidate)
{
    const DWORD_PTR address = reinterpret_cast<DWORD_PTR>(candidate);
    if (!IsGameExecutableAddress(address))
    {
        return;
    }
    const DWORD gameAddress = static_cast<DWORD>(address);
    for (UINT prior = 0; prior < group.gameStackDepth; ++prior)
    {
        if (group.gameStack[prior] == gameAddress)
        {
            return;
        }
    }
    if (group.gameStackDepth < group.gameStack.size())
    {
        group.gameStack[group.gameStackDepth++] = gameAddress;
    }
}

void InitializeGameImageRange()
{
    g_gameImageBegin = 0;
    g_gameImageEnd = 0;
    const HMODULE module = GetModuleHandleW(nullptr);
    if (module == nullptr)
    {
        return;
    }
    __try
    {
        const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        {
            return;
        }
        const auto* const nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            reinterpret_cast<const std::uint8_t*>(module) + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->OptionalHeader.SizeOfImage == 0)
        {
            return;
        }
        g_gameImageBegin = reinterpret_cast<DWORD_PTR>(module);
        g_gameImageEnd = g_gameImageBegin + nt->OptionalHeader.SizeOfImage;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_gameImageBegin = 0;
        g_gameImageEnd = 0;
    }
}

void CaptureGameStack(DrawSignatureGroup& group)
{
    if (g_gameImageBegin == 0 || g_gameImageEnd <= g_gameImageBegin)
    {
        return;
    }
    void* frames[16] = {};
    const USHORT captured = RtlCaptureStackBackTrace(
        0,
        static_cast<ULONG>(std::size(frames)),
        frames,
        nullptr);
    for (USHORT index = 0;
         index < captured && group.gameStackDepth < group.gameStack.size();
         ++index)
    {
        AppendGameStackAddress(group, frames[index]);
    }
}

void CaptureGameStackSlots(
    DrawSignatureGroup& group,
    const void* const* returnAddressSlot)
{
    if (returnAddressSlot == nullptr)
    {
        return;
    }
    constexpr std::size_t kMaximumStackSlots = 32;
    for (std::size_t index = 0;
         index < kMaximumStackSlots && group.gameStackDepth < group.gameStack.size();
         ++index)
    {
        const void* candidate = nullptr;
        if (!SafeCopy(
                &candidate,
                returnAddressSlot + index,
                sizeof(candidate)))
        {
            break;
        }
        AppendGameStackAddress(group, candidate);
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
        if (!SafeCopy(
                &candidate,
                returnAddressSlot + index,
                sizeof(candidate)))
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

DWORD ClassifyRendererRoute(const void* const* returnAddressSlot)
{
    if (StackSlotsContainAddress(returnAddressSlot, kAnimatedMeshDrawReturn))
    {
        return kRendererRouteAnimatedMesh;
    }
    if (StackSlotsContainAddress(returnAddressSlot, kGenericMeshDrawReturn))
    {
        return kRendererRouteGenericMesh;
    }
    return kRendererRouteUnknown;
}

bfvr::stereo::WeaponRendererRoute ToWeaponRendererRoute(
    DWORD rendererRoute)
{
    switch (rendererRoute)
    {
    case kRendererRouteAnimatedMesh:
        return bfvr::stereo::WeaponRendererRoute::AnimatedMesh;
    case kRendererRouteGenericMesh:
        return bfvr::stereo::WeaponRendererRoute::GenericMesh;
    default:
        return bfvr::stereo::WeaponRendererRoute::Unknown;
    }
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

bool IsSharedFixedFunctionWeaponCandidate(
    DrawKind kind,
    const TrackedState& state,
    const DrawKey& key)
{
    bfvr::stereo::WeaponDrawPolicyInput input = {};
    input.indexedDraw = kind == DrawKind::IndexedPrimitive;
    input.rendererRoute = ToWeaponRendererRoute(key.rendererRoute);
    input.vertexShaderOrFvf = key.vertexShaderOrFvf;
    input.alphaBlendEnabled = key.alphaBlendEnable != 0;
    input.zEnabled = key.zEnable != 0;
    input.firstPersonProjection = key.firstPersonProjection;
    input.worldKnown = state.worldKnown;
    input.viewKnown = state.viewKnown;
    input.world = ToStereoMatrix(state.world);
    input.view = ToStereoMatrix(state.view);
    return bfvr::stereo::ClassifyWeaponDraw(input, 8.0F) ==
        bfvr::stereo::WeaponDrawDisposition::SharedFixedFunctionWeaponCandidate;
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

DWORD HashBytes(const void* data, std::size_t size)
{
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    DWORD hash = 2166136261U;
    for (std::size_t index = 0; index < size; ++index)
    {
        hash ^= bytes[index];
        hash *= 16777619U;
    }
    return hash;
}

bool KeysEqual(const DrawKey& lhs, const DrawKey& rhs)
{
    return lhs.kind == rhs.kind &&
        lhs.callerReturn == rhs.callerReturn &&
        lhs.primitiveType == rhs.primitiveType &&
        lhs.primitiveCount == rhs.primitiveCount &&
        lhs.vertexCount == rhs.vertexCount &&
        lhs.vertexShaderOrFvf == rhs.vertexShaderOrFvf &&
        lhs.pixelShader == rhs.pixelShader &&
        lhs.stream0Stride == rhs.stream0Stride &&
        lhs.stream0 == rhs.stream0 &&
        lhs.indices == rhs.indices &&
        lhs.texture0 == rhs.texture0 &&
        lhs.zEnable == rhs.zEnable &&
        lhs.alphaBlendEnable == rhs.alphaBlendEnable &&
        lhs.rendererRoute == rhs.rendererRoute &&
        lhs.firstPersonProjection == rhs.firstPersonProjection;
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
        horizontal >= 2.0f && vertical >= 3.5f;
}

DWORD SignaturePriority(const DrawKey& key)
{
    if (!key.firstPersonProjection)
    {
        return 0;
    }
    if (key.rendererRoute == kRendererRouteAnimatedMesh &&
        key.vertexShaderOrFvf == 0x17U)
    {
        return 3;
    }
    if (key.rendererRoute == kRendererRouteGenericMesh &&
        key.vertexShaderOrFvf == 0x112U &&
        key.alphaBlendEnable == 0)
    {
        return 2;
    }
    return 1;
}

const wchar_t* DrawKindName(DrawKind kind)
{
    switch (kind)
    {
    case DrawKind::Primitive:
        return L"DrawPrimitive";
    case DrawKind::IndexedPrimitive:
        return L"DrawIndexedPrimitive";
    case DrawKind::PrimitiveUp:
        return L"DrawPrimitiveUP";
    case DrawKind::IndexedPrimitiveUp:
        return L"DrawIndexedPrimitiveUP";
    }
    return L"unknown";
}

void ResetCapture()
{
    g_probe.captureFrame = 0;
    g_probe.drawCalls = 0;
    g_probe.groupOverflow = 0;
    g_probe.groupEvictions = 0;
    g_probe.eligibleSince = 0;
    g_probe.tracked = {};
    g_probe.groups = {};
}

void RecordDraw(
    DrawKind kind,
    void* device,
    DWORD callerReturn,
    const void* const* returnAddressSlot,
    DWORD primitiveType,
    UINT vertexCount,
    UINT primitiveCount)
{
    if (!IsCaptureOnDeviceThread(device))
    {
        return;
    }
    const TrackedState& state = g_probe.tracked;
    const DrawKey key = {
        kind,
        callerReturn,
        primitiveType,
        primitiveCount,
        vertexCount,
        state.vertexShaderOrFvf,
        state.pixelShader,
        state.stream0Stride,
        state.stream0,
        state.indices,
        state.texture0,
        state.zEnable,
        state.alphaBlendEnable,
        ClassifyRendererRoute(returnAddressSlot),
        IsFirstPersonProjection(state)};
    DrawSignatureGroup* group = nullptr;
    for (DrawSignatureGroup& candidate : g_probe.groups)
    {
        if (candidate.occupied && KeysEqual(candidate.key, key))
        {
            group = &candidate;
            break;
        }
    }
    if (group == nullptr)
    {
        for (DrawSignatureGroup& candidate : g_probe.groups)
        {
            if (!candidate.occupied)
            {
                group = &candidate;
                break;
            }
        }
    }
    if (group == nullptr)
    {
        const DWORD requiredPriority = SignaturePriority(key);
        DrawSignatureGroup* replacement = nullptr;
        for (DrawSignatureGroup& candidate : g_probe.groups)
        {
            if (candidate.priority < requiredPriority &&
                (replacement == nullptr || candidate.priority < replacement->priority))
            {
                replacement = &candidate;
            }
        }
        if (replacement != nullptr)
        {
            *replacement = {};
            group = replacement;
            InterlockedIncrement(&g_probe.groupEvictions);
        }
    }
    if (group == nullptr)
    {
        InterlockedExchange(&g_probe.groupOverflow, 1);
        return;
    }
    if (!group->occupied)
    {
        group->occupied = true;
        group->key = key;
        group->priority = SignaturePriority(key);
        group->sharedFixedFunctionWeaponCandidate =
            IsSharedFixedFunctionWeaponCandidate(kind, state, key);
        group->firstFrame = g_probe.captureFrame;
        group->sourceWvpMask = state.vertexConstantMask;
        group->sourceWvpHash = state.vertexConstantMask == 0xFU
            ? HashBytes(state.vertexConstants, sizeof(state.vertexConstants))
            : 0;
        group->world = state.world;
        group->view = state.view;
        group->projection = state.projection;
        std::memcpy(
            group->vertexConstants,
            state.vertexConstants,
            sizeof(group->vertexConstants));
        group->worldKnown = state.worldKnown;
        group->viewKnown = state.viewKnown;
        group->projectionKnown = state.projectionKnown;
        CaptureGameStack(*group);
        CaptureGameStackSlots(*group, returnAddressSlot);
    }
    ++group->calls;
    group->lastFrame = g_probe.captureFrame;
    InterlockedIncrement(&g_probe.drawCalls);
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
                L"Weapon view-model probe observed the local-infantry gate. It will wait %lu ms before its %ld-frame capture; select the contrasting ordinary weapon now and begin any desired fire/reload transition. Every game call remains forwarded unchanged.",
                static_cast<unsigned long>(kEligibleInfantrySettleMs),
                kCapturedFrameCount);
        }
        else if (GetTickCount() - g_probe.eligibleSince >= kEligibleInfantrySettleMs &&
                 InterlockedCompareExchange(&g_probe.state, 2, 1) == 1)
        {
            ResetCapture();
            AppendLog(
                L"Weapon view-model probe began its %ld-frame local-infantry draw capture after the %lu ms preparation window. Every D3D8 call will forward unchanged; this records only setter arguments and draw signatures after those original calls return.",
                kCapturedFrameCount,
                static_cast<unsigned long>(kEligibleInfantrySettleMs));
        }
    }
    else if (state == 2 && device == g_probe.device &&
             GetCurrentThreadId() == g_probe.executionThreadId &&
             InterlockedIncrement(&g_probe.captureFrame) >= kCapturedFrameCount)
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
    if (!SUCCEEDED(result) || !IsCaptureOnDeviceThread(device))
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
    if (!SUCCEEDED(result) || !IsCaptureOnDeviceThread(device))
    {
        return result;
    }
    if (state == kRenderStateZEnable)
    {
        g_probe.tracked.zEnable = value;
    }
    else if (state == kRenderStateZWriteEnable)
    {
        g_probe.tracked.zWriteEnable = value;
    }
    else if (state == kRenderStateAlphaBlendEnable)
    {
        g_probe.tracked.alphaBlendEnable = value;
    }
    else if (state == kRenderStateLighting)
    {
        g_probe.tracked.lighting = value;
    }
    return result;
}

HRESULT WINAPI HookSetTexture(void* device, DWORD stage, void* texture)
{
    const SetTextureFn original = g_originalSetTexture;
    const HRESULT result = original == nullptr ? E_FAIL : original(device, stage, texture);
    if (SUCCEEDED(result) && IsCaptureOnDeviceThread(device) && stage == 0)
    {
        g_probe.tracked.texture0 = texture;
    }
    return result;
}

HRESULT WINAPI HookSetVertexShader(void* device, DWORD shaderOrFvf)
{
    const SetVertexShaderFn original = g_originalSetVertexShader;
    const HRESULT result = original == nullptr ? E_FAIL : original(device, shaderOrFvf);
    if (SUCCEEDED(result) && IsCaptureOnDeviceThread(device))
    {
        g_probe.tracked.vertexShaderOrFvf = shaderOrFvf;
    }
    return result;
}

HRESULT WINAPI HookSetVertexShaderConstant(
    void* device,
    DWORD firstRegister,
    const void* data,
    DWORD registerCount)
{
    const SetVertexShaderConstantFn original = g_originalSetVertexShaderConstant;
    const HRESULT result = original == nullptr ? E_FAIL : original(
        device,
        firstRegister,
        data,
        registerCount);
    if (!SUCCEEDED(result) || !IsCaptureOnDeviceThread(device) ||
        firstRegister >= 4 || data == nullptr)
    {
        return result;
    }
    const DWORD copiedCount = std::min<DWORD>(registerCount, 4 - firstRegister);
    if (copiedCount == 0 || !SafeCopy(
            &g_probe.tracked.vertexConstants[firstRegister][0],
            data,
            static_cast<std::size_t>(copiedCount) * sizeof(float) * 4))
    {
        return result;
    }
    g_probe.tracked.vertexConstantMask |= ((1U << copiedCount) - 1U) << firstRegister;
    return result;
}

HRESULT WINAPI HookSetStreamSource(
    void* device,
    UINT streamNumber,
    void* streamData,
    UINT stride)
{
    const SetStreamSourceFn original = g_originalSetStreamSource;
    const HRESULT result = original == nullptr ? E_FAIL : original(
        device,
        streamNumber,
        streamData,
        stride);
    if (SUCCEEDED(result) && IsCaptureOnDeviceThread(device) && streamNumber == 0)
    {
        g_probe.tracked.stream0 = streamData;
        g_probe.tracked.stream0Stride = stride;
    }
    return result;
}

HRESULT WINAPI HookSetIndices(void* device, void* indexData, UINT baseVertexIndex)
{
    const SetIndicesFn original = g_originalSetIndices;
    const HRESULT result = original == nullptr ? E_FAIL : original(
        device,
        indexData,
        baseVertexIndex);
    if (SUCCEEDED(result) && IsCaptureOnDeviceThread(device))
    {
        g_probe.tracked.indices = indexData;
        g_probe.tracked.baseVertexIndex = baseVertexIndex;
    }
    return result;
}

HRESULT WINAPI HookSetPixelShader(void* device, DWORD shader)
{
    const SetPixelShaderFn original = g_originalSetPixelShader;
    const HRESULT result = original == nullptr ? E_FAIL : original(device, shader);
    if (SUCCEEDED(result) && IsCaptureOnDeviceThread(device))
    {
        g_probe.tracked.pixelShader = shader;
    }
    return result;
}

HRESULT WINAPI HookDrawPrimitive(
    void* device,
    DWORD primitiveType,
    UINT startVertex,
    UINT primitiveCount)
{
    const DWORD caller = static_cast<DWORD>(
        reinterpret_cast<DWORD_PTR>(_ReturnAddress()));
    void** const returnAddressSlot =
        reinterpret_cast<void**>(_AddressOfReturnAddress());
    const DrawPrimitiveFn original = g_originalDrawPrimitive;
    const HRESULT result = original == nullptr ? E_FAIL : original(
        device,
        primitiveType,
        startVertex,
        primitiveCount);
    if (SUCCEEDED(result))
    {
        RecordDraw(
            DrawKind::Primitive,
            device,
            caller,
            returnAddressSlot,
            primitiveType,
            0,
            primitiveCount);
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
    const DWORD caller = static_cast<DWORD>(
        reinterpret_cast<DWORD_PTR>(_ReturnAddress()));
    void** const returnAddressSlot =
        reinterpret_cast<void**>(_AddressOfReturnAddress());
    const DrawIndexedPrimitiveFn original = g_originalDrawIndexedPrimitive;
    const HRESULT result = original == nullptr ? E_FAIL : original(
        device,
        primitiveType,
        minimumVertexIndex,
        vertexCount,
        startIndex,
        primitiveCount);
    if (SUCCEEDED(result))
    {
        RecordDraw(
            DrawKind::IndexedPrimitive,
            device,
            caller,
            returnAddressSlot,
            primitiveType,
            vertexCount,
            primitiveCount);
    }
    return result;
}

HRESULT WINAPI HookDrawPrimitiveUp(
    void* device,
    DWORD primitiveType,
    UINT primitiveCount,
    const void* vertexData,
    UINT vertexStride)
{
    const DWORD caller = static_cast<DWORD>(
        reinterpret_cast<DWORD_PTR>(_ReturnAddress()));
    void** const returnAddressSlot =
        reinterpret_cast<void**>(_AddressOfReturnAddress());
    const DrawPrimitiveUpFn original = g_originalDrawPrimitiveUp;
    const HRESULT result = original == nullptr ? E_FAIL : original(
        device,
        primitiveType,
        primitiveCount,
        vertexData,
        vertexStride);
    if (SUCCEEDED(result))
    {
        RecordDraw(
            DrawKind::PrimitiveUp,
            device,
            caller,
            returnAddressSlot,
            primitiveType,
            0,
            primitiveCount);
    }
    return result;
}

HRESULT WINAPI HookDrawIndexedPrimitiveUp(
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
    const DWORD caller = static_cast<DWORD>(
        reinterpret_cast<DWORD_PTR>(_ReturnAddress()));
    void** const returnAddressSlot =
        reinterpret_cast<void**>(_AddressOfReturnAddress());
    const DrawIndexedPrimitiveUpFn original = g_originalDrawIndexedPrimitiveUp;
    const HRESULT result = original == nullptr ? E_FAIL : original(
        device,
        primitiveType,
        minimumVertexIndex,
        vertexCount,
        primitiveCount,
        indexData,
        indexFormat,
        vertexData,
        vertexStride);
    if (SUCCEEDED(result))
    {
        RecordDraw(
            DrawKind::IndexedPrimitiveUp,
            device,
            caller,
            returnAddressSlot,
            primitiveType,
            vertexCount,
            primitiveCount);
    }
    return result;
}

void RemoveHooks()
{
    const std::array<void*, 13> targets = {
        g_probe.presentTarget,
        g_probe.setTransformTarget,
        g_probe.setRenderStateTarget,
        g_probe.setTextureTarget,
        g_probe.setVertexShaderTarget,
        g_probe.setVertexShaderConstantTarget,
        g_probe.setStreamSourceTarget,
        g_probe.setIndicesTarget,
        g_probe.setPixelShaderTarget,
        g_probe.drawPrimitiveTarget,
        g_probe.drawIndexedPrimitiveTarget,
        g_probe.drawPrimitiveUpTarget,
        g_probe.drawIndexedPrimitiveUpTarget};
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
        g_probe.setTextureTarget = vtable[kSetTextureSlot];
        g_probe.setVertexShaderTarget = vtable[kSetVertexShaderSlot];
        g_probe.setVertexShaderConstantTarget = vtable[kSetVertexShaderConstantSlot];
        g_probe.setStreamSourceTarget = vtable[kSetStreamSourceSlot];
        g_probe.setIndicesTarget = vtable[kSetIndicesSlot];
        g_probe.setPixelShaderTarget = vtable[kSetPixelShaderSlot];
        g_probe.drawPrimitiveTarget = vtable[kDrawPrimitiveSlot];
        g_probe.drawIndexedPrimitiveTarget = vtable[kDrawIndexedPrimitiveSlot];
        g_probe.drawPrimitiveUpTarget = vtable[kDrawPrimitiveUpSlot];
        g_probe.drawIndexedPrimitiveUpTarget = vtable[kDrawIndexedPrimitiveUpSlot];
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    const std::array<void*, 13> targets = {
        g_probe.presentTarget,
        g_probe.setTransformTarget,
        g_probe.setRenderStateTarget,
        g_probe.setTextureTarget,
        g_probe.setVertexShaderTarget,
        g_probe.setVertexShaderConstantTarget,
        g_probe.setStreamSourceTarget,
        g_probe.setIndicesTarget,
        g_probe.setPixelShaderTarget,
        g_probe.drawPrimitiveTarget,
        g_probe.drawIndexedPrimitiveTarget,
        g_probe.drawPrimitiveUpTarget,
        g_probe.drawIndexedPrimitiveUpTarget};
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
        create(g_probe.setTextureTarget, reinterpret_cast<LPVOID>(&HookSetTexture), reinterpret_cast<LPVOID*>(&g_originalSetTexture)) &&
        create(g_probe.setVertexShaderTarget, reinterpret_cast<LPVOID>(&HookSetVertexShader), reinterpret_cast<LPVOID*>(&g_originalSetVertexShader)) &&
        create(g_probe.setVertexShaderConstantTarget, reinterpret_cast<LPVOID>(&HookSetVertexShaderConstant), reinterpret_cast<LPVOID*>(&g_originalSetVertexShaderConstant)) &&
        create(g_probe.setStreamSourceTarget, reinterpret_cast<LPVOID>(&HookSetStreamSource), reinterpret_cast<LPVOID*>(&g_originalSetStreamSource)) &&
        create(g_probe.setIndicesTarget, reinterpret_cast<LPVOID>(&HookSetIndices), reinterpret_cast<LPVOID*>(&g_originalSetIndices)) &&
        create(g_probe.setPixelShaderTarget, reinterpret_cast<LPVOID>(&HookSetPixelShader), reinterpret_cast<LPVOID*>(&g_originalSetPixelShader)) &&
        create(g_probe.drawPrimitiveTarget, reinterpret_cast<LPVOID>(&HookDrawPrimitive), reinterpret_cast<LPVOID*>(&g_originalDrawPrimitive)) &&
        create(g_probe.drawIndexedPrimitiveTarget, reinterpret_cast<LPVOID>(&HookDrawIndexedPrimitive), reinterpret_cast<LPVOID*>(&g_originalDrawIndexedPrimitive)) &&
        create(g_probe.drawPrimitiveUpTarget, reinterpret_cast<LPVOID>(&HookDrawPrimitiveUp), reinterpret_cast<LPVOID*>(&g_originalDrawPrimitiveUp)) &&
        create(g_probe.drawIndexedPrimitiveUpTarget, reinterpret_cast<LPVOID>(&HookDrawIndexedPrimitiveUp), reinterpret_cast<LPVOID*>(&g_originalDrawIndexedPrimitiveUp));
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
    LONG groupCount = 0;
    for (const DrawSignatureGroup& group : g_probe.groups)
    {
        if (group.occupied)
        {
            ++groupCount;
        }
    }
    AppendLog(
        L"Weapon view-model probe complete: frames=%ld draws=%ld groups=%ld/%ld overflow=%ld evicted=%ld. Groups below are stable D3D8 signatures; first-person-projection candidates can replace lower-priority raw groups, and no group yet names a weapon draw.",
        InterlockedCompareExchange(&g_probe.captureFrame, 0, 0),
        InterlockedCompareExchange(&g_probe.drawCalls, 0, 0),
        groupCount,
        kMaximumSignatureGroups,
        InterlockedCompareExchange(&g_probe.groupOverflow, 0, 0),
        InterlockedCompareExchange(&g_probe.groupEvictions, 0, 0));
    LONG ordinal = 0;
    for (const DrawSignatureGroup& group : g_probe.groups)
    {
        if (!group.occupied)
        {
            continue;
        }
        ++ordinal;
        Matrix c0to3 = {};
        std::memcpy(&c0to3, group.vertexConstants, sizeof(c0to3));
        std::array<wchar_t, 80> stackText = {};
        std::size_t stackOffset = 0;
        for (UINT index = 0;
             index < group.gameStackDepth && stackOffset + 9 < stackText.size();
             ++index)
        {
            const int written = _snwprintf_s(
                stackText.data() + stackOffset,
                stackText.size() - stackOffset,
                _TRUNCATE,
                index == 0 ? L"%08lX" : L">%08lX",
                static_cast<unsigned long>(group.gameStack[index]));
            if (written < 0)
            {
                break;
            }
            stackOffset += static_cast<std::size_t>(written);
        }
        if (group.gameStackDepth == 0)
        {
            wcscpy_s(stackText.data(), stackText.size(), L"none");
        }
        AppendLog(
            L"Weapon draw [%ld] calls=%ld frames=%ld..%ld kind=%s caller=%08lX stack=%ls prim=%lu count=%u vertices=%u vsOrFvf=%08lX ps=%08lX stream=%p/%u indices=%p tex0=%p z=%lu alpha=%lu rendererRoute=%lu firstPersonProjection=%d priority=%lu sharedFixedWeaponCandidate=%d c0to3Mask=%X c0to3Hash=%08lX W=%d V=%d P=%d Wrow3=[%.3f %.3f %.3f %.3f] Vrow3=[%.3f %.3f %.3f %.3f] Pdiag=[%.3f %.3f] c0=[%.3f %.3f %.3f %.3f] c3=[%.3f %.3f %.3f %.3f].",
            ordinal,
            group.calls,
            group.firstFrame,
            group.lastFrame,
            DrawKindName(group.key.kind),
            static_cast<unsigned long>(group.key.callerReturn),
            stackText.data(),
            static_cast<unsigned long>(group.key.primitiveType),
            group.key.primitiveCount,
            group.key.vertexCount,
            static_cast<unsigned long>(group.key.vertexShaderOrFvf),
            static_cast<unsigned long>(group.key.pixelShader),
            group.key.stream0,
            group.key.stream0Stride,
            group.key.indices,
            group.key.texture0,
            static_cast<unsigned long>(group.key.zEnable),
            static_cast<unsigned long>(group.key.alphaBlendEnable),
            static_cast<unsigned long>(group.key.rendererRoute),
            group.key.firstPersonProjection,
            static_cast<unsigned long>(group.priority),
            group.sharedFixedFunctionWeaponCandidate,
            static_cast<unsigned long>(group.sourceWvpMask),
            static_cast<unsigned long>(group.sourceWvpHash),
            group.worldKnown,
            group.viewKnown,
            group.projectionKnown,
            static_cast<double>(group.world.values[3][0]),
            static_cast<double>(group.world.values[3][1]),
            static_cast<double>(group.world.values[3][2]),
            static_cast<double>(group.world.values[3][3]),
            static_cast<double>(group.view.values[3][0]),
            static_cast<double>(group.view.values[3][1]),
            static_cast<double>(group.view.values[3][2]),
            static_cast<double>(group.view.values[3][3]),
            static_cast<double>(group.projection.values[0][0]),
            static_cast<double>(group.projection.values[1][1]),
            static_cast<double>(c0to3.values[0][0]),
            static_cast<double>(c0to3.values[0][1]),
            static_cast<double>(c0to3.values[0][2]),
            static_cast<double>(c0to3.values[0][3]),
            static_cast<double>(c0to3.values[3][0]),
            static_cast<double>(c0to3.values[3][1]),
            static_cast<double>(c0to3.values[3][2]),
            static_cast<double>(c0to3.values[3][3]));
    }
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
        AppendLog(L"Weapon view-model probe skipped: verified direct system-D3D8 lifecycle/hooks were unavailable.");
        SignalCompletion();
        return 0;
    }
    InterlockedExchange(&g_probe.state, 1);
    AppendLog(
        L"Weapon view-model probe armed: it waits for the sustained local-BFPlayer isAlive gate, then gives an 8000 ms preparation window before recording 240 Present-to-Present frames. Manually remain on foot, select a contrasting ordinary infantry weapon, and begin any desired fire/reload transition. No weapon, input, camera, resource, D3D state, or rendering output is changed.");
    const DWORD captureStarted = GetTickCount();
    while (GetTickCount() - captureStarted < kCaptureTimeoutMs &&
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
        AppendLog(L"Weapon view-model probe timed out before completing a spawned local-infantry capture; no D3D8 state or game state was changed.");
    }
    InterlockedExchange(&g_probe.state, 0);
    RemoveHooks();
    MH_Uninitialize();
    AppendLog(L"Weapon view-model probe removed all forwarding hooks.");
    SignalCompletion();
    return 0;
}

} // namespace

namespace bfvr
{

void StartD3D8WeaponViewModelProbe(const D3D8ObserverCallbacks& callbacks)
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
    InitializeGameImageRange();
    HANDLE worker = CreateThread(nullptr, 0, RunProbe, nullptr, 0, nullptr);
    if (worker == nullptr)
    {
        AppendLog(L"Weapon view-model probe could not start worker (error %lu); no D3D8 code was patched.", GetLastError());
        SignalCompletion();
        return;
    }
    CloseHandle(worker);
    AppendLog(L"Requested the read-only weapon view-model draw probe. It only records original D3D8 setter arguments and draws after the game has issued them.");
}

} // namespace bfvr
