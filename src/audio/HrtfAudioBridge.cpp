#include "audio/HrtfAudioBridge.h"

#include "audio/HrtfBufferPolicy.h"
#include "audio/HrtfListenerMath.h"
#include "audio/HrtfMenuSoundPolicy.h"
#include "audio/OpenALHrtfRouterDiagnostics.h"
#include "client/MenuPointerOverlay.h"
#include "client/DirectSoundImportRoute.h"

#define DIRECTSOUND_VERSION 0x0800
#include <initguid.h>
#include <mmreg.h>
#include <dsound.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>

namespace bfvr::audio
{
namespace
{
constexpr long kAlcHrtfEnabledSoft = 1;
constexpr ULONGLONG kPoseExpiryMilliseconds = 250;
constexpr float kWorldUnitsPerMeter = 1.0F;
constexpr std::size_t kMaximumBufferVtableRoutes = 8;
constexpr std::size_t kMaximumMenuBufferIdentities = 128;
constexpr ULONGLONG kMenuLayerWindowMilliseconds = 750;

using CreateSoundBufferFunction = HRESULT (STDMETHODCALLTYPE*)(
    IDirectSound8* directSound,
    LPCDSBUFFERDESC description,
    LPDIRECTSOUNDBUFFER* buffer,
    LPUNKNOWN outerUnknown);
using QueryInterfaceFunction = HRESULT (STDMETHODCALLTYPE*)(
    IUnknown* object,
    REFIID interfaceId,
    void** interfacePointer);
using ReleaseFunction = ULONG (STDMETHODCALLTYPE*)(IUnknown* object);
using BufferPlayFunction = HRESULT (STDMETHODCALLTYPE*)(
    IDirectSoundBuffer* buffer,
    DWORD reserved1,
    DWORD priority,
    DWORD flags);
using BufferSetPanFunction = HRESULT (STDMETHODCALLTYPE*)(
    IDirectSoundBuffer* buffer,
    LONG pan);
using BufferUnlockFunction = HRESULT (STDMETHODCALLTYPE*)(
    IDirectSoundBuffer* buffer,
    LPVOID audioPointer1,
    DWORD audioBytes1,
    LPVOID audioPointer2,
    DWORD audioBytes2);
using ListenerGetAllFunction = HRESULT (STDMETHODCALLTYPE*)(
    IDirectSound3DListener* listener,
    LPDS3DLISTENER parameters);
using ListenerGetOrientationFunction = HRESULT (STDMETHODCALLTYPE*)(
    IDirectSound3DListener* listener,
    D3DVALUE* frontX,
    D3DVALUE* frontY,
    D3DVALUE* frontZ,
    D3DVALUE* topX,
    D3DVALUE* topY,
    D3DVALUE* topZ);
using ListenerGetPositionFunction = HRESULT (STDMETHODCALLTYPE*)(
    IDirectSound3DListener* listener,
    D3DVECTOR* position);
using ListenerSetAllFunction = HRESULT (STDMETHODCALLTYPE*)(
    IDirectSound3DListener* listener,
    LPCDS3DLISTENER parameters,
    DWORD apply);
using ListenerSetOrientationFunction = HRESULT (STDMETHODCALLTYPE*)(
    IDirectSound3DListener* listener,
    D3DVALUE frontX,
    D3DVALUE frontY,
    D3DVALUE frontZ,
    D3DVALUE topX,
    D3DVALUE topY,
    D3DVALUE topZ,
    DWORD apply);
using ListenerSetPositionFunction = HRESULT (STDMETHODCALLTYPE*)(
    IDirectSound3DListener* listener,
    D3DVALUE x,
    D3DVALUE y,
    D3DVALUE z,
    DWORD apply);

HrtfAudioLogCallback g_logCallback = nullptr;
HMODULE g_actualOpenAL = nullptr;
HMODULE g_openALRouter = nullptr;
HMODULE g_dsoal = nullptr;
DirectSoundCreate8Function g_dsoalCreate = nullptr;
DirectSoundCreate8Function g_originalCreate = nullptr;
GetOpenALHrtfRouterDiagnosticsFunction g_getRouterDiagnostics = nullptr;

CreateSoundBufferFunction g_originalCreateSoundBuffer = nullptr;
ListenerGetAllFunction g_originalListenerGetAll = nullptr;
ListenerGetOrientationFunction g_originalListenerGetOrientation = nullptr;
ListenerGetPositionFunction g_originalListenerGetPosition = nullptr;
ListenerSetAllFunction g_originalListenerSetAll = nullptr;
ListenerSetOrientationFunction g_originalListenerSetOrientation = nullptr;
ListenerSetPositionFunction g_originalListenerSetPosition = nullptr;

SRWLOCK g_poseLock = SRWLOCK_INIT;
stereo::Pose g_headPose = {};
ULONGLONG g_headPoseTick = 0;
bool g_headPoseTracked = false;

SRWLOCK g_listenerLock = SRWLOCK_INIT;
DS3DLISTENER g_nativeListener = {sizeof(DS3DLISTENER)};
bool g_nativeListenerValid = false;

struct BufferVtableRoute
{
    void** vtable = nullptr;
    QueryInterfaceFunction queryInterface = nullptr;
    ReleaseFunction release = nullptr;
    BufferPlayFunction play = nullptr;
    BufferSetPanFunction setPan = nullptr;
    BufferUnlockFunction unlock = nullptr;
};

SRWLOCK g_bufferVtableLock = SRWLOCK_INIT;
std::array<BufferVtableRoute, kMaximumBufferVtableRoutes>
    g_bufferVtableRoutes = {};
std::size_t g_bufferVtableRouteCount = 0;
bool g_centerMonoInterfaceSounds = true;
bool g_centerStockMenuSounds = true;

struct MenuBufferIdentity
{
    IDirectSoundBuffer* buffer = nullptr;
    HrtfMenuSoundKind kind = HrtfMenuSoundKind::none;
    DWORD originalMode = DS3DMODE_NORMAL;
    bool modeForced = false;
};

SRWLOCK g_menuBufferLock = SRWLOCK_INIT;
std::array<MenuBufferIdentity, kMaximumMenuBufferIdentities>
    g_menuBufferIdentities = {};
std::size_t g_menuBufferIdentityCount = 0;
std::atomic<ULONGLONG> g_menuTriggerTick = 0;

std::atomic<unsigned long> g_backendCreateCalls = 0;
std::atomic<unsigned long> g_backendFallbackCalls = 0;
std::atomic<unsigned long> g_primaryBuffers = 0;
std::atomic<unsigned long> g_threeDimensionalBuffers = 0;
std::atomic<unsigned long> g_listenerQueries = 0;
std::atomic<unsigned long> g_adjustedListenerWrites = 0;
std::atomic<unsigned long> g_nativeListenerWrites = 0;
std::atomic<unsigned long> g_centeredMonoBuffers = 0;
std::atomic<bool> g_loggedListenerHooks = false;
std::atomic<bool> g_loggedAdjustedListener = false;
std::atomic<bool> g_loggedNativeListener = false;
std::atomic<bool> g_loggedMalformedListener = false;
std::atomic<bool> g_loggedBufferVtableCapacity = false;
std::atomic<bool> g_loggedMonoBuffer = false;
std::atomic<bool> g_loggedCenteredMonoBuffer = false;
std::atomic<bool> g_loggedMenuBufferIdentity = false;
std::atomic<bool> g_loggedMenuBufferCapacity = false;
std::atomic<bool> g_loggedMenuSpatialDisable = false;

void Log(const wchar_t* format, ...) noexcept
{
    if (g_logCallback == nullptr)
    {
        return;
    }
    std::array<wchar_t, 768> message = {};
    va_list arguments;
    va_start(arguments, format);
    _vsnwprintf_s(
        message.data(),
        message.size(),
        _TRUNCATE,
        format,
        arguments);
    va_end(arguments);
    g_logCallback(message.data());
}

bool BuildRuntimePath(
    HMODULE client,
    const wchar_t* fileName,
    std::array<wchar_t, MAX_PATH>& path) noexcept
{
    const DWORD length = GetModuleFileNameW(
        client,
        path.data(),
        static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
    {
        return false;
    }
    wchar_t* separator = wcsrchr(path.data(), L'\\');
    if (separator == nullptr)
    {
        return false;
    }
    *separator = L'\0';
    return wcscat_s(
            path.data(),
            path.size(),
            L"\\runtime\\audio\\win32\\") == 0 &&
        wcscat_s(path.data(), path.size(), fileName) == 0;
}

HMODULE LoadRuntimeModule(HMODULE client, const wchar_t* fileName) noexcept
{
    std::array<wchar_t, MAX_PATH> path = {};
    if (!BuildRuntimePath(client, fileName, path))
    {
        Log(L"BFVR HRTF could not construct the private runtime path for %s.", fileName);
        return nullptr;
    }
    HMODULE module = LoadLibraryExW(
        path.data(),
        nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
            LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (module == nullptr)
    {
        Log(
            L"BFVR HRTF could not load %s by absolute path (error %lu).",
            path.data(),
            GetLastError());
    }
    return module;
}

template<typename Function>
bool PatchVtableSlot(
    void** vtable,
    std::size_t slot,
    Function replacement,
    Function& original) noexcept
{
    if (vtable == nullptr || replacement == nullptr)
    {
        return false;
    }
    void** const target = vtable + slot;
    if (*target == reinterpret_cast<void*>(replacement))
    {
        return original != nullptr;
    }
    if (original != nullptr && *target != reinterpret_cast<void*>(original))
    {
        return false;
    }

    DWORD previousProtection = 0;
    if (!VirtualProtect(
            target,
            sizeof(void*),
            PAGE_READWRITE,
            &previousProtection))
    {
        return false;
    }
    if (original == nullptr)
    {
        original = reinterpret_cast<Function>(*target);
    }
    *target = reinterpret_cast<void*>(replacement);
    FlushInstructionCache(GetCurrentProcess(), target, sizeof(void*));
    DWORD ignoredProtection = 0;
    VirtualProtect(
        target,
        sizeof(void*),
        previousProtection,
        &ignoredProtection);
    return true;
}

bool ReplaceVtableSlot(
    void** vtable,
    std::size_t slot,
    const void* replacement) noexcept
{
    if (vtable == nullptr || replacement == nullptr)
    {
        return false;
    }
    void** const target = vtable + slot;
    if (*target == replacement)
    {
        return true;
    }

    DWORD previousProtection = 0;
    if (!VirtualProtect(
            target,
            sizeof(void*),
            PAGE_READWRITE,
            &previousProtection))
    {
        return false;
    }
    *target = const_cast<void*>(replacement);
    FlushInstructionCache(GetCurrentProcess(), target, sizeof(void*));
    DWORD ignoredProtection = 0;
    VirtualProtect(
        target,
        sizeof(void*),
        previousProtection,
        &ignoredProtection);
    return true;
}

bool ReadFreshHeadPose(stereo::Pose& pose) noexcept
{
    AcquireSRWLockShared(&g_poseLock);
    pose = g_headPose;
    const ULONGLONG tick = g_headPoseTick;
    const bool tracked = g_headPoseTracked;
    ReleaseSRWLockShared(&g_poseLock);
    return tracked &&
        tick != 0 &&
        GetTickCount64() - tick <= kPoseExpiryMilliseconds;
}

bool AdjustListener(DS3DLISTENER& parameters) noexcept
{
    stereo::Pose head = {};
    if (!ReadFreshHeadPose(head))
    {
        return false;
    }
    const ListenerTransform native = {
        {
            parameters.vPosition.x,
            parameters.vPosition.y,
            parameters.vPosition.z},
        {
            parameters.vOrientFront.x,
            parameters.vOrientFront.y,
            parameters.vOrientFront.z},
        {
            parameters.vOrientTop.x,
            parameters.vOrientTop.y,
            parameters.vOrientTop.z}};
    const auto adjusted = ComposeHrtfListener(
        native,
        head,
        kWorldUnitsPerMeter);
    if (!adjusted)
    {
        return false;
    }
    parameters.vPosition = {
        adjusted->position.x,
        adjusted->position.y,
        adjusted->position.z};
    parameters.vOrientFront = {
        adjusted->front.x,
        adjusted->front.y,
        adjusted->front.z};
    parameters.vOrientTop = {
        adjusted->top.x,
        adjusted->top.y,
        adjusted->top.z};
    return true;
}

void RecordListenerWrite(
    bool adjusted,
    const DS3DLISTENER& parameters) noexcept
{
    std::atomic<unsigned long>& count = adjusted
        ? g_adjustedListenerWrites
        : g_nativeListenerWrites;
    std::atomic<bool>& logged = adjusted
        ? g_loggedAdjustedListener
        : g_loggedNativeListener;
    ++count;
    if (!logged.exchange(true))
    {
        Log(
            adjusted
                ? L"BFVR HRTF observed its first tracked listener write: position=(%.3f, %.3f, %.3f) front=(%.3f, %.3f, %.3f) top=(%.3f, %.3f, %.3f)."
                : L"BFVR HRTF forwarded its first native listener write because no fresh tracked pose was available: position=(%.3f, %.3f, %.3f) front=(%.3f, %.3f, %.3f) top=(%.3f, %.3f, %.3f).",
            static_cast<double>(parameters.vPosition.x),
            static_cast<double>(parameters.vPosition.y),
            static_cast<double>(parameters.vPosition.z),
            static_cast<double>(parameters.vOrientFront.x),
            static_cast<double>(parameters.vOrientFront.y),
            static_cast<double>(parameters.vOrientFront.z),
            static_cast<double>(parameters.vOrientTop.x),
            static_cast<double>(parameters.vOrientTop.y),
            static_cast<double>(parameters.vOrientTop.z));
    }
}

ListenerTransform ToListenerTransform(
    const DS3DLISTENER& listener) noexcept
{
    return {
        {
            listener.vPosition.x,
            listener.vPosition.y,
            listener.vPosition.z},
        {
            listener.vOrientFront.x,
            listener.vOrientFront.y,
            listener.vOrientFront.z},
        {
            listener.vOrientTop.x,
            listener.vOrientTop.y,
            listener.vOrientTop.z}};
}

bool IsFiniteVector(const D3DVECTOR& value) noexcept
{
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

bool IsValidListenerTransform(const DS3DLISTENER& listener) noexcept
{
    return ComposeHrtfListener(
        ToListenerTransform(listener),
        {},
        kWorldUnitsPerMeter).has_value();
}

DS3DLISTENER ReadNativeListener() noexcept
{
    AcquireSRWLockShared(&g_listenerLock);
    const DS3DLISTENER listener = g_nativeListener;
    ReleaseSRWLockShared(&g_listenerLock);
    return listener;
}

bool StoreNativeListener(const DS3DLISTENER& listener) noexcept
{
    if (!IsValidListenerTransform(listener))
    {
        return false;
    }
    AcquireSRWLockExclusive(&g_listenerLock);
    g_nativeListener = listener;
    g_nativeListener.dwSize = sizeof(DS3DLISTENER);
    g_nativeListenerValid = true;
    ReleaseSRWLockExclusive(&g_listenerLock);
    return true;
}

bool HasNativeListener() noexcept
{
    AcquireSRWLockShared(&g_listenerLock);
    const bool valid = g_nativeListenerValid;
    ReleaseSRWLockShared(&g_listenerLock);
    return valid;
}

bool RepairListenerTransform(DS3DLISTENER& listener) noexcept
{
    if (IsValidListenerTransform(listener))
    {
        return true;
    }
    const bool hasPrevious = HasNativeListener();
    if (!g_loggedMalformedListener.exchange(true))
    {
        Log(
            hasPrevious
                ? L"BFVR HRTF rejected a malformed native listener transform and reused the last valid transform: position=(%.3f, %.3f, %.3f)."
                : L"BFVR HRTF observed a malformed native listener transform before any valid transform was available: position=(%.3f, %.3f, %.3f).",
            static_cast<double>(listener.vPosition.x),
            static_cast<double>(listener.vPosition.y),
            static_cast<double>(listener.vPosition.z));
    }
    if (!hasPrevious)
    {
        return false;
    }

    const DS3DLISTENER previous = ReadNativeListener();
    if (!IsFiniteVector(listener.vPosition))
    {
        listener.vPosition = previous.vPosition;
    }
    if (!IsValidListenerTransform(listener))
    {
        listener.vOrientFront = previous.vOrientFront;
        listener.vOrientTop = previous.vOrientTop;
    }
    return IsValidListenerTransform(listener);
}

HRESULT STDMETHODCALLTYPE HookListenerGetAll(
    IDirectSound3DListener* listener,
    LPDS3DLISTENER parameters)
{
    if (parameters != nullptr &&
        parameters->dwSize == sizeof(DS3DLISTENER) &&
        HasNativeListener())
    {
        *parameters = ReadNativeListener();
        return DS_OK;
    }
    return g_originalListenerGetAll(listener, parameters);
}

HRESULT STDMETHODCALLTYPE HookListenerGetOrientation(
    IDirectSound3DListener* listener,
    D3DVALUE* frontX,
    D3DVALUE* frontY,
    D3DVALUE* frontZ,
    D3DVALUE* topX,
    D3DVALUE* topY,
    D3DVALUE* topZ)
{
    if (frontX != nullptr && frontY != nullptr && frontZ != nullptr &&
        topX != nullptr && topY != nullptr && topZ != nullptr &&
        HasNativeListener())
    {
        const DS3DLISTENER native = ReadNativeListener();
        *frontX = native.vOrientFront.x;
        *frontY = native.vOrientFront.y;
        *frontZ = native.vOrientFront.z;
        *topX = native.vOrientTop.x;
        *topY = native.vOrientTop.y;
        *topZ = native.vOrientTop.z;
        return DS_OK;
    }
    return g_originalListenerGetOrientation(
        listener,
        frontX,
        frontY,
        frontZ,
        topX,
        topY,
        topZ);
}

HRESULT STDMETHODCALLTYPE HookListenerGetPosition(
    IDirectSound3DListener* listener,
    D3DVECTOR* position)
{
    if (position != nullptr && HasNativeListener())
    {
        *position = ReadNativeListener().vPosition;
        return DS_OK;
    }
    return g_originalListenerGetPosition(listener, position);
}

HRESULT STDMETHODCALLTYPE HookListenerSetAll(
    IDirectSound3DListener* listener,
    LPCDS3DLISTENER parameters,
    DWORD apply)
{
    if (parameters == nullptr)
    {
        return g_originalListenerSetAll(listener, parameters, apply);
    }
    if (parameters->dwSize != sizeof(DS3DLISTENER))
    {
        return g_originalListenerSetAll(listener, parameters, apply);
    }
    DS3DLISTENER native = *parameters;
    if (!RepairListenerTransform(native))
    {
        return g_originalListenerSetAll(listener, parameters, apply);
    }
    StoreNativeListener(native);
    DS3DLISTENER adjusted = native;
    RecordListenerWrite(AdjustListener(adjusted), adjusted);
    return g_originalListenerSetAll(listener, &adjusted, apply);
}

HRESULT STDMETHODCALLTYPE HookListenerSetOrientation(
    IDirectSound3DListener* listener,
    D3DVALUE frontX,
    D3DVALUE frontY,
    D3DVALUE frontZ,
    D3DVALUE topX,
    D3DVALUE topY,
    D3DVALUE topZ,
    DWORD apply)
{
    DS3DLISTENER native = ReadNativeListener();
    native.vOrientFront = {frontX, frontY, frontZ};
    native.vOrientTop = {topX, topY, topZ};
    if (!RepairListenerTransform(native))
    {
        return g_originalListenerSetOrientation(
            listener,
            frontX,
            frontY,
            frontZ,
            topX,
            topY,
            topZ,
            apply);
    }
    StoreNativeListener(native);
    DS3DLISTENER adjusted = native;
    RecordListenerWrite(AdjustListener(adjusted), adjusted);
    return g_originalListenerSetOrientation(
        listener,
        adjusted.vOrientFront.x,
        adjusted.vOrientFront.y,
        adjusted.vOrientFront.z,
        adjusted.vOrientTop.x,
        adjusted.vOrientTop.y,
        adjusted.vOrientTop.z,
        apply);
}

HRESULT STDMETHODCALLTYPE HookListenerSetPosition(
    IDirectSound3DListener* listener,
    D3DVALUE x,
    D3DVALUE y,
    D3DVALUE z,
    DWORD apply)
{
    DS3DLISTENER native = ReadNativeListener();
    native.vPosition = {x, y, z};
    if (!RepairListenerTransform(native))
    {
        return g_originalListenerSetPosition(listener, x, y, z, apply);
    }
    StoreNativeListener(native);
    DS3DLISTENER adjusted = native;
    RecordListenerWrite(AdjustListener(adjusted), adjusted);
    return g_originalListenerSetPosition(
        listener,
        adjusted.vPosition.x,
        adjusted.vPosition.y,
        adjusted.vPosition.z,
        apply);
}

bool InstallListenerHooks(IDirectSound3DListener* listener) noexcept
{
    if (listener == nullptr)
    {
        return false;
    }
    auto** const vtable = *reinterpret_cast<void***>(listener);

    DS3DLISTENER initial = {sizeof(DS3DLISTENER)};
    auto getAll = reinterpret_cast<ListenerGetAllFunction>(vtable[3]);
    if (SUCCEEDED(getAll(listener, &initial)))
    {
        StoreNativeListener(initial);
    }

    bool installed = PatchVtableSlot(
        vtable,
        3,
        &HookListenerGetAll,
        g_originalListenerGetAll);
    installed = PatchVtableSlot(
        vtable,
        6,
        &HookListenerGetOrientation,
        g_originalListenerGetOrientation) && installed;
    installed = PatchVtableSlot(
        vtable,
        7,
        &HookListenerGetPosition,
        g_originalListenerGetPosition) && installed;
    installed = PatchVtableSlot(
        vtable,
        10,
        &HookListenerSetAll,
        g_originalListenerSetAll) && installed;
    installed = PatchVtableSlot(
        vtable,
        13,
        &HookListenerSetOrientation,
        g_originalListenerSetOrientation) && installed;
    installed = PatchVtableSlot(
        vtable,
        14,
        &HookListenerSetPosition,
        g_originalListenerSetPosition) && installed;
    if (installed && !g_loggedListenerHooks.exchange(true))
    {
        const DS3DLISTENER native = ReadNativeListener();
        Log(
            L"BFVR HRTF installed the DSOAL listener bridge: native position=(%.3f, %.3f, %.3f) front=(%.3f, %.3f, %.3f) top=(%.3f, %.3f, %.3f).",
            static_cast<double>(native.vPosition.x),
            static_cast<double>(native.vPosition.y),
            static_cast<double>(native.vPosition.z),
            static_cast<double>(native.vOrientFront.x),
            static_cast<double>(native.vOrientFront.y),
            static_cast<double>(native.vOrientFront.z),
            static_cast<double>(native.vOrientTop.x),
            static_cast<double>(native.vOrientTop.y),
            static_cast<double>(native.vOrientTop.z));
    }
    return installed;
}

bool FindBufferVtableRoute(
    const void* object,
    BufferVtableRoute& route) noexcept
{
    if (object == nullptr)
    {
        return false;
    }
    auto** const vtable = *reinterpret_cast<void***>(
        const_cast<void*>(object));
    AcquireSRWLockShared(&g_bufferVtableLock);
    for (std::size_t index = 0;
         index < g_bufferVtableRouteCount;
         ++index)
    {
        if (g_bufferVtableRoutes[index].vtable == vtable)
        {
            route = g_bufferVtableRoutes[index];
            ReleaseSRWLockShared(&g_bufferVtableLock);
            return true;
        }
    }
    ReleaseSRWLockShared(&g_bufferVtableLock);
    return false;
}

bool ReadBufferPolicyInput(
    IDirectSoundBuffer* buffer,
    LONG pan,
    HrtfBufferPolicyInput& input,
    DWORD& flags) noexcept
{
    if (buffer == nullptr)
    {
        return false;
    }
    DSBCAPS capabilities = {};
    capabilities.dwSize = sizeof(capabilities);
    if (FAILED(buffer->GetCaps(&capabilities)))
    {
        return false;
    }
    flags = capabilities.dwFlags;
    input.primary = (flags & DSBCAPS_PRIMARYBUFFER) != 0;
    input.threeDimensional = (flags & DSBCAPS_CTRL3D) != 0;
    input.panControl = (flags & DSBCAPS_CTRLPAN) != 0;
    input.pan = pan;
    if (input.primary || input.threeDimensional || !input.panControl)
    {
        return true;
    }

    WAVEFORMATEXTENSIBLE format = {};
    DWORD written = 0;
    if (FAILED(buffer->GetFormat(
            &format.Format,
            sizeof(format),
            &written)) ||
        written < sizeof(WAVEFORMATEX))
    {
        return false;
    }
    input.channels = format.Format.nChannels;
    if (input.channels == 1 && !g_loggedMonoBuffer.exchange(true))
    {
        Log(
            L"BFVR HRTF observed its first mono non-3D DirectSound pan buffer: flags=0x%08lX pan=%ld.",
            flags,
            pan);
    }
    return true;
}

void RecordCenteredMonoBuffer(
    DWORD flags,
    LONG originalPan) noexcept
{
    ++g_centeredMonoBuffers;
    if (!g_loggedCenteredMonoBuffer.exchange(true))
    {
        Log(
            L"BFVR HRTF centred its first panned mono non-3D sound (flags=0x%08lX original-pan=%ld); stereo and DSBCAPS_CTRL3D buffers remain unchanged.",
            flags,
            originalPan);
    }
}

bool ReadMenuBufferIdentity(
    IDirectSoundBuffer* buffer,
    MenuBufferIdentity& identity) noexcept
{
    AcquireSRWLockShared(&g_menuBufferLock);
    for (std::size_t index = 0;
         index < g_menuBufferIdentityCount;
         ++index)
    {
        if (g_menuBufferIdentities[index].buffer == buffer)
        {
            identity = g_menuBufferIdentities[index];
            ReleaseSRWLockShared(&g_menuBufferLock);
            return true;
        }
    }
    ReleaseSRWLockShared(&g_menuBufferLock);
    return false;
}

bool StoreMenuBufferIdentity(
    IDirectSoundBuffer* buffer,
    HrtfMenuSoundKind kind) noexcept
{
    AcquireSRWLockExclusive(&g_menuBufferLock);
    for (std::size_t index = 0;
         index < g_menuBufferIdentityCount;
         ++index)
    {
        if (g_menuBufferIdentities[index].buffer == buffer)
        {
            g_menuBufferIdentities[index].kind = kind;
            ReleaseSRWLockExclusive(&g_menuBufferLock);
            return true;
        }
    }
    if (g_menuBufferIdentityCount >= g_menuBufferIdentities.size())
    {
        ReleaseSRWLockExclusive(&g_menuBufferLock);
        if (!g_loggedMenuBufferCapacity.exchange(true))
        {
            Log(L"BFVR HRTF reached its bounded menu-audio identity capacity; later buffers remain unchanged.");
        }
        return false;
    }
    g_menuBufferIdentities[g_menuBufferIdentityCount] = {
        buffer,
        kind,
        DS3DMODE_NORMAL,
        false};
    ++g_menuBufferIdentityCount;
    ReleaseSRWLockExclusive(&g_menuBufferLock);
    return true;
}

const wchar_t* MenuSoundKindName(HrtfMenuSoundKind kind) noexcept
{
    switch (kind)
    {
    case HrtfMenuSoundKind::menuContext:
        return L"visible-menu-context";
    case HrtfMenuSoundKind::dedicated:
        return L"dedicated";
    case HrtfMenuSoundKind::sharedWeaponLayer:
        return L"shared-weapon-layer";
    default:
        return L"none";
    }
}

void SetMenuBufferForcedMode(
    IDirectSoundBuffer* buffer,
    bool forced,
    DWORD originalMode) noexcept
{
    AcquireSRWLockExclusive(&g_menuBufferLock);
    for (std::size_t index = 0;
         index < g_menuBufferIdentityCount;
         ++index)
    {
        if (g_menuBufferIdentities[index].buffer == buffer)
        {
            g_menuBufferIdentities[index].modeForced = forced;
            if (forced)
            {
                g_menuBufferIdentities[index].originalMode = originalMode;
            }
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_menuBufferLock);
}

void RemoveMenuBufferIdentity(IDirectSoundBuffer* buffer) noexcept
{
    AcquireSRWLockExclusive(&g_menuBufferLock);
    for (std::size_t index = 0;
         index < g_menuBufferIdentityCount;
         ++index)
    {
        if (g_menuBufferIdentities[index].buffer == buffer)
        {
            --g_menuBufferIdentityCount;
            g_menuBufferIdentities[index] =
                g_menuBufferIdentities[g_menuBufferIdentityCount];
            g_menuBufferIdentities[g_menuBufferIdentityCount] = {};
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_menuBufferLock);
}

void IdentifyUploadedMenuPcm(
    IDirectSoundBuffer* buffer,
    const void* audioPointer1,
    DWORD audioBytes1,
    const void* audioPointer2,
    DWORD audioBytes2) noexcept
{
    if (!g_centerStockMenuSounds || buffer == nullptr ||
        (audioPointer1 == nullptr && audioBytes1 != 0) ||
        (audioPointer2 == nullptr && audioBytes2 != 0))
    {
        return;
    }

    DSBCAPS capabilities = {};
    capabilities.dwSize = sizeof(capabilities);
    if (FAILED(buffer->GetCaps(&capabilities)) ||
        static_cast<std::uint64_t>(audioBytes1) + audioBytes2 !=
            capabilities.dwBufferBytes)
    {
        return;
    }
    WAVEFORMATEXTENSIBLE format = {};
    DWORD written = 0;
    if (FAILED(buffer->GetFormat(
            &format.Format,
            sizeof(format),
            &written)) ||
        written < sizeof(WAVEFORMATEX) ||
        format.Format.wFormatTag != WAVE_FORMAT_PCM)
    {
        return;
    }

    std::uint64_t hash = ContinueHrtfPcmHash(
        kHrtfFnv1aOffset,
        audioPointer1,
        audioBytes1);
    hash = ContinueHrtfPcmHash(
        hash,
        audioPointer2,
        audioBytes2);
    const HrtfPcmSignature signature = {
        hash,
        capabilities.dwBufferBytes,
        format.Format.nSamplesPerSec,
        format.Format.nChannels,
        format.Format.wBitsPerSample};
    const HrtfMenuSoundKind kind = ClassifyHrtfMenuSound(signature);
    if (kind == HrtfMenuSoundKind::none ||
        !StoreMenuBufferIdentity(buffer, kind))
    {
        return;
    }
    if (!g_loggedMenuBufferIdentity.exchange(true))
    {
        Log(
            L"BFVR HRTF identified its first exact stock menu PCM buffer: kind=%s bytes=%lu rate=%lu flags=0x%08lX.",
            MenuSoundKindName(kind),
            signature.bytes,
            signature.samplesPerSecond,
            capabilities.dwFlags);
    }
}

bool IsMonoSpatialBuffer(IDirectSoundBuffer* buffer) noexcept
{
    if (buffer == nullptr)
    {
        return false;
    }
    DSBCAPS capabilities = {};
    capabilities.dwSize = sizeof(capabilities);
    if (FAILED(buffer->GetCaps(&capabilities)) ||
        (capabilities.dwFlags & DSBCAPS_PRIMARYBUFFER) != 0 ||
        (capabilities.dwFlags & DSBCAPS_CTRL3D) == 0)
    {
        return false;
    }
    WAVEFORMATEXTENSIBLE format = {};
    DWORD written = 0;
    return SUCCEEDED(buffer->GetFormat(
               &format.Format,
               sizeof(format),
               &written)) &&
        written >= sizeof(WAVEFORMATEX) &&
        format.Format.nChannels == 1;
}

bool SetMenuBufferSpatialDisabled(
    IDirectSoundBuffer* buffer,
    bool disabled) noexcept
{
    MenuBufferIdentity identity = {};
    if (!ReadMenuBufferIdentity(buffer, identity))
    {
        return false;
    }
    IDirectSound3DBuffer* spatialBuffer = nullptr;
    if (FAILED(buffer->QueryInterface(
            IID_IDirectSound3DBuffer,
            reinterpret_cast<void**>(&spatialBuffer))) ||
        spatialBuffer == nullptr)
    {
        return false;
    }

    bool changed = false;
    DWORD currentMode = DS3DMODE_NORMAL;
    if (disabled)
    {
        if (SUCCEEDED(spatialBuffer->GetMode(&currentMode)) &&
            currentMode != DS3DMODE_DISABLE &&
            SUCCEEDED(spatialBuffer->SetMode(
                DS3DMODE_DISABLE,
                DS3D_IMMEDIATE)))
        {
            SetMenuBufferForcedMode(buffer, true, currentMode);
            changed = true;
        }
    }
    else if (identity.modeForced &&
        SUCCEEDED(spatialBuffer->SetMode(
            identity.originalMode,
            DS3D_IMMEDIATE)))
    {
        SetMenuBufferForcedMode(buffer, false, identity.originalMode);
        changed = true;
    }
    spatialBuffer->Release();
    return changed;
}

void ApplyMenuSoundPolicyBeforePlay(IDirectSoundBuffer* buffer) noexcept
{
    if (!g_centerStockMenuSounds)
    {
        return;
    }
    const bool visibleMainMenu =
        bfvr::GetMainMenuOverlayInteractionState().visible;
    MenuBufferIdentity identity = {};
    bool identified = ReadMenuBufferIdentity(buffer, identity);
    if (!identified && visibleMainMenu && IsMonoSpatialBuffer(buffer) &&
        StoreMenuBufferIdentity(buffer, HrtfMenuSoundKind::menuContext))
    {
        identified = ReadMenuBufferIdentity(buffer, identity);
    }
    if (!identified)
    {
        return;
    }

    const ULONGLONG now = GetTickCount64();
    if (identity.kind == HrtfMenuSoundKind::dedicated)
    {
        g_menuTriggerTick.store(now);
    }
    const bool disableSpatial = ShouldDisableHrtfMenuSpatialization(
        identity.kind,
        visibleMainMenu,
        now,
        g_menuTriggerTick.load(),
        kMenuLayerWindowMilliseconds);

    const bool changed = SetMenuBufferSpatialDisabled(
        buffer,
        disableSpatial);
    if (disableSpatial && changed &&
        !g_loggedMenuSpatialDisable.exchange(true))
    {
        Log(
            L"BFVR HRTF made its first menu-gated sound non-spatial: kind=%s stock-layer-window=%llu-ms. Sounds outside the visible menu and unmatched gameplay 3D buffers remain unchanged.",
            MenuSoundKindName(identity.kind),
            static_cast<unsigned long long>(
                kMenuLayerWindowMilliseconds));
    }
}

HRESULT STDMETHODCALLTYPE HookBufferQueryInterface(
    IUnknown* object,
    REFIID interfaceId,
    void** interfacePointer)
{
    BufferVtableRoute route = {};
    if (!FindBufferVtableRoute(object, route) ||
        route.queryInterface == nullptr)
    {
        return E_UNEXPECTED;
    }
    const HRESULT result = route.queryInterface(
        object,
        interfaceId,
        interfacePointer);
    if (SUCCEEDED(result) &&
        interfacePointer != nullptr &&
        *interfacePointer != nullptr &&
        IsEqualGUID(interfaceId, IID_IDirectSound3DListener))
    {
        ++g_listenerQueries;
        if (!InstallListenerHooks(
                static_cast<IDirectSound3DListener*>(*interfacePointer)))
        {
            Log(L"BFVR HRTF could not install the DSOAL listener vtable bridge; native game listener values will be used.");
        }
    }
    return result;
}

ULONG STDMETHODCALLTYPE HookBufferRelease(IUnknown* object)
{
    BufferVtableRoute route = {};
    if (!FindBufferVtableRoute(object, route) || route.release == nullptr)
    {
        return 0;
    }
    const ULONG references = route.release(object);
    if (references == 0)
    {
        RemoveMenuBufferIdentity(
            reinterpret_cast<IDirectSoundBuffer*>(object));
    }
    return references;
}

HRESULT STDMETHODCALLTYPE HookBufferUnlock(
    IDirectSoundBuffer* buffer,
    LPVOID audioPointer1,
    DWORD audioBytes1,
    LPVOID audioPointer2,
    DWORD audioBytes2)
{
    BufferVtableRoute route = {};
    if (!FindBufferVtableRoute(buffer, route) || route.unlock == nullptr)
    {
        return DSERR_GENERIC;
    }
    const HRESULT result = route.unlock(
        buffer,
        audioPointer1,
        audioBytes1,
        audioPointer2,
        audioBytes2);
    if (SUCCEEDED(result))
    {
        IdentifyUploadedMenuPcm(
            buffer,
            audioPointer1,
            audioBytes1,
            audioPointer2,
            audioBytes2);
    }
    return result;
}

HRESULT STDMETHODCALLTYPE HookBufferSetPan(
    IDirectSoundBuffer* buffer,
    LONG pan)
{
    BufferVtableRoute route = {};
    if (!FindBufferVtableRoute(buffer, route) || route.setPan == nullptr)
    {
        return DSERR_GENERIC;
    }

    HrtfBufferPolicyInput input = {};
    DWORD flags = 0;
    const bool center = g_centerMonoInterfaceSounds &&
        ReadBufferPolicyInput(buffer, pan, input, flags) &&
        ShouldCenterHrtfBuffer(input);
    const HRESULT result = route.setPan(buffer, center ? 0 : pan);
    if (center && SUCCEEDED(result))
    {
        RecordCenteredMonoBuffer(flags, pan);
    }
    return result;
}

HRESULT STDMETHODCALLTYPE HookBufferPlay(
    IDirectSoundBuffer* buffer,
    DWORD reserved1,
    DWORD priority,
    DWORD flags)
{
    BufferVtableRoute route = {};
    if (!FindBufferVtableRoute(buffer, route) || route.play == nullptr)
    {
        return DSERR_GENERIC;
    }

    ApplyMenuSoundPolicyBeforePlay(buffer);

    LONG pan = 0;
    HrtfBufferPolicyInput input = {};
    DWORD capabilityFlags = 0;
    if (g_centerMonoInterfaceSounds &&
        SUCCEEDED(buffer->GetPan(&pan)) &&
        ReadBufferPolicyInput(
            buffer,
            pan,
            input,
            capabilityFlags) &&
        ShouldCenterHrtfBuffer(input) &&
        route.setPan != nullptr &&
        SUCCEEDED(route.setPan(buffer, 0)))
    {
        RecordCenteredMonoBuffer(capabilityFlags, pan);
    }
    return route.play(buffer, reserved1, priority, flags);
}

bool InstallBufferVtableHooks(IDirectSoundBuffer* buffer) noexcept
{
    if (buffer == nullptr)
    {
        return false;
    }
    auto** const vtable = *reinterpret_cast<void***>(buffer);
    AcquireSRWLockExclusive(&g_bufferVtableLock);
    for (std::size_t index = 0;
         index < g_bufferVtableRouteCount;
         ++index)
    {
        if (g_bufferVtableRoutes[index].vtable == vtable)
        {
            ReleaseSRWLockExclusive(&g_bufferVtableLock);
            return true;
        }
    }
    if (g_bufferVtableRouteCount >= g_bufferVtableRoutes.size())
    {
        ReleaseSRWLockExclusive(&g_bufferVtableLock);
        if (!g_loggedBufferVtableCapacity.exchange(true))
        {
            Log(L"BFVR HRTF reached its bounded DSOAL buffer-vtable route capacity; later buffer classes remain unmodified.");
        }
        return false;
    }

    BufferVtableRoute route = {};
    route.vtable = vtable;
    route.queryInterface = reinterpret_cast<QueryInterfaceFunction>(vtable[0]);
    route.release = reinterpret_cast<ReleaseFunction>(vtable[2]);
    route.play = reinterpret_cast<BufferPlayFunction>(vtable[12]);
    route.setPan = reinterpret_cast<BufferSetPanFunction>(vtable[16]);
    route.unlock = reinterpret_cast<BufferUnlockFunction>(vtable[19]);
    if (route.queryInterface == nullptr || route.release == nullptr ||
        route.play == nullptr || route.setPan == nullptr ||
        route.unlock == nullptr)
    {
        ReleaseSRWLockExclusive(&g_bufferVtableLock);
        return false;
    }
    g_bufferVtableRoutes[g_bufferVtableRouteCount] = route;
    ++g_bufferVtableRouteCount;

    const bool installedQuery = ReplaceVtableSlot(
        vtable,
        0,
        reinterpret_cast<const void*>(&HookBufferQueryInterface));
    const bool installedRelease = ReplaceVtableSlot(
        vtable,
        2,
        reinterpret_cast<const void*>(&HookBufferRelease));
    const bool installedPlay = ReplaceVtableSlot(
        vtable,
        12,
        reinterpret_cast<const void*>(&HookBufferPlay));
    const bool installedSetPan = ReplaceVtableSlot(
        vtable,
        16,
        reinterpret_cast<const void*>(&HookBufferSetPan));
    const bool installedUnlock = ReplaceVtableSlot(
        vtable,
        19,
        reinterpret_cast<const void*>(&HookBufferUnlock));
    const std::size_t routeCount = g_bufferVtableRouteCount;
    ReleaseSRWLockExclusive(&g_bufferVtableLock);

    if (installedQuery && installedRelease && installedPlay &&
        installedSetPan && installedUnlock)
    {
        Log(
            L"BFVR HRTF registered DSOAL buffer vtable route %llu/%llu.",
            static_cast<unsigned long long>(routeCount),
            static_cast<unsigned long long>(kMaximumBufferVtableRoutes));
        return true;
    }
    return false;
}

HRESULT STDMETHODCALLTYPE HookCreateSoundBuffer(
    IDirectSound8* directSound,
    LPCDSBUFFERDESC description,
    LPDIRECTSOUNDBUFFER* buffer,
    LPUNKNOWN outerUnknown)
{
    const HRESULT result = g_originalCreateSoundBuffer(
        directSound,
        description,
        buffer,
        outerUnknown);
    if (FAILED(result) || description == nullptr ||
        buffer == nullptr || *buffer == nullptr)
    {
        return result;
    }

    if ((description->dwFlags & DSBCAPS_PRIMARYBUFFER) != 0)
    {
        if (g_primaryBuffers.fetch_add(1) == 0)
        {
            Log(
                L"BFVR HRTF observed the first DSOAL primary buffer (flags=0x%08lX).",
                description->dwFlags);
        }
    }
    if ((description->dwFlags & DSBCAPS_CTRL3D) != 0)
    {
        if (g_threeDimensionalBuffers.fetch_add(1) == 0)
        {
            Log(
                L"BFVR HRTF observed the first DSOAL DSBCAPS_CTRL3D buffer (flags=0x%08lX).",
                description->dwFlags);
        }
    }

    if (!InstallBufferVtableHooks(*buffer))
    {
        Log(L"BFVR HRTF could not install the bounded DSOAL buffer observer/correction hooks.");
    }
    else if ((description->dwFlags &
                  (DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRL3D)) ==
             (DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRL3D))
    {
        // Acquire and immediately release the standard primary listener once
        // so the HMD bridge does not depend on BF1942 querying it through the
        // same concrete buffer vtable later.
        IDirectSound3DListener* listener = nullptr;
        if (SUCCEEDED((*buffer)->QueryInterface(
                IID_IDirectSound3DListener,
                reinterpret_cast<void**>(&listener))) &&
            listener != nullptr)
        {
            listener->Release();
        }
    }
    return result;
}

bool InstallDirectSoundHooks(IDirectSound8* directSound) noexcept
{
    return directSound != nullptr &&
        PatchVtableSlot(
            *reinterpret_cast<void***>(directSound),
            3,
            &HookCreateSoundBuffer,
            g_originalCreateSoundBuffer);
}

HRESULT WINAPI HookDirectSoundCreate8(
    const GUID* deviceGuid,
    IDirectSound8** directSound,
    IUnknown* outerUnknown)
{
    ++g_backendCreateCalls;
    const HRESULT result = g_dsoalCreate(
        deviceGuid,
        directSound,
        outerUnknown);
    if (SUCCEEDED(result) && directSound != nullptr && *directSound != nullptr)
    {
        if (!InstallDirectSoundHooks(*directSound))
        {
            Log(L"BFVR HRTF created DSOAL but could not install its listener bridge.");
        }
        return result;
    }

    ++g_backendFallbackCalls;
    Log(
        L"BFVR HRTF DirectSound creation failed (HRESULT 0x%08lX); falling back to BF1942's original DirectSound route.",
        static_cast<unsigned long>(result));
    return g_originalCreate == nullptr
        ? result
        : g_originalCreate(deviceGuid, directSound, outerUnknown);
}

bool ProbeBackend(OpenALHrtfRouterDiagnostics& diagnostics) noexcept
{
    IDirectSound8* directSound = nullptr;
    HRESULT result = g_dsoalCreate(nullptr, &directSound, nullptr);
    if (FAILED(result) || directSound == nullptr)
    {
        Log(
            L"BFVR HRTF backend probe could not create DirectSound8 (HRESULT 0x%08lX).",
            static_cast<unsigned long>(result));
        return false;
    }

    result = directSound->SetCooperativeLevel(
        GetDesktopWindow(),
        DSSCL_PRIORITY);
    IDirectSoundBuffer* primary = nullptr;
    IDirectSound3DListener* listener = nullptr;
    if (SUCCEEDED(result))
    {
        DSBUFFERDESC description = {};
        description.dwSize = sizeof(description);
        description.dwFlags =
            DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRL3D;
        result = directSound->CreateSoundBuffer(
            &description,
            &primary,
            nullptr);
    }
    if (SUCCEEDED(result) && primary != nullptr)
    {
        result = primary->QueryInterface(
            IID_IDirectSound3DListener,
            reinterpret_cast<void**>(&listener));
    }

    diagnostics = {};
    const bool readDiagnostics =
        g_getRouterDiagnostics != nullptr &&
        g_getRouterDiagnostics(&diagnostics) != 0;

    if (listener != nullptr)
    {
        listener->Release();
    }
    if (primary != nullptr)
    {
        primary->Release();
    }
    directSound->Release();

    if (FAILED(result))
    {
        Log(
            L"BFVR HRTF backend probe could not establish a 3D listener (HRESULT 0x%08lX).",
            static_cast<unsigned long>(result));
        return false;
    }
    return readDiagnostics &&
        diagnostics.forcedHrtfCalls != 0 &&
        diagnostics.successfulContextCalls != 0 &&
        diagnostics.lastHrtfStatus == kAlcHrtfEnabledSoft;
}

bool IsEnvironmentExplicitlyDisabled(const wchar_t* name) noexcept
{
    wchar_t value[2] = {};
    const DWORD length = GetEnvironmentVariableW(
        name,
        value,
        static_cast<DWORD>(std::size(value)));
    return length == 1 && value[0] == L'0';
}
} // namespace

bool IsHrtfAudioRequested() noexcept
{
    return !IsEnvironmentExplicitlyDisabled(L"BFVR_HRTF");
}

bool InitializeHrtfAudio(
    HMODULE bfvrClient,
    HrtfAudioLogCallback logCallback) noexcept
{
    g_logCallback = logCallback;
    if (!IsHrtfAudioRequested())
    {
        return true;
    }
    g_centerMonoInterfaceSounds =
        !IsEnvironmentExplicitlyDisabled(L"BFVR_HRTF_CENTER_MONO");
    g_centerStockMenuSounds =
        !IsEnvironmentExplicitlyDisabled(L"BFVR_HRTF_CENTER_MENU");
    if (bfvrClient == nullptr)
    {
        Log(L"BFVR HRTF was requested but the BFVR client module is unavailable.");
        return false;
    }

    g_actualOpenAL = LoadRuntimeModule(
        bfvrClient,
        L"BFVROpenALSoft.dll");
    g_openALRouter = g_actualOpenAL == nullptr
        ? nullptr
        : LoadRuntimeModule(bfvrClient, L"dsoal-aldrv.dll");
    g_dsoal = g_openALRouter == nullptr
        ? nullptr
        : LoadRuntimeModule(bfvrClient, L"BFVRDSoal.dll");
    if (g_dsoal == nullptr)
    {
        Log(L"BFVR HRTF failed closed before DirectSound import routing.");
        return false;
    }

    g_dsoalCreate = reinterpret_cast<DirectSoundCreate8Function>(
        GetProcAddress(g_dsoal, "DirectSoundCreate8"));
    g_getRouterDiagnostics =
        reinterpret_cast<GetOpenALHrtfRouterDiagnosticsFunction>(
            GetProcAddress(
                g_openALRouter,
                "BFVRGetOpenALHrtfRouterDiagnostics"));
    if (g_dsoalCreate == nullptr || g_getRouterDiagnostics == nullptr)
    {
        Log(L"BFVR HRTF runtime exports are incomplete; the original audio route was preserved.");
        return false;
    }

    OpenALHrtfRouterDiagnostics diagnostics = {};
    if (!ProbeBackend(diagnostics))
    {
        Log(
            L"BFVR HRTF probe rejected the backend: contexts=%lu forced=%lu successful=%lu status=%ld malformed=%lu. The original audio route was preserved.",
            diagnostics.createContextCalls,
            diagnostics.forcedHrtfCalls,
            diagnostics.successfulContextCalls,
            diagnostics.lastHrtfStatus,
            diagnostics.malformedAttributeLists);
        return false;
    }

    const DirectSoundImportRouteResult route =
        RouteExecutableDirectSoundCreate8(&HookDirectSoundCreate8);
    if (!route.routed || route.previous == nullptr)
    {
        Log(
            L"BFVR HRTF could not route BF1942's DirectSoundCreate8 import (error %lu); the original route was preserved.",
            route.error);
        return false;
    }
    g_originalCreate = route.previous;
    Log(
        L"BFVR HRTF enabled by default: private DSOAL/OpenAL Soft backend passed its 3D-listener probe with HRTF status %ld; BF1942 DirectSound creation is now routed through BFVR. HMD listener motion expires to native audio after %llu ms without tracked poses. Mono non-3D pan correction is %s; visible-menu sound centring is %s.",
        diagnostics.lastHrtfStatus,
        kPoseExpiryMilliseconds,
        g_centerMonoInterfaceSounds ? L"enabled" : L"disabled",
        g_centerStockMenuSounds ? L"enabled" : L"disabled");
    return true;
}

void PublishHrtfHeadPose(
    const stereo::Pose& headPose,
    bool tracked) noexcept
{
    if (!IsHrtfAudioRequested())
    {
        return;
    }
    AcquireSRWLockExclusive(&g_poseLock);
    g_headPose = headPose;
    g_headPoseTracked = tracked;
    g_headPoseTick = GetTickCount64();
    ReleaseSRWLockExclusive(&g_poseLock);
}
} // namespace bfvr::audio
