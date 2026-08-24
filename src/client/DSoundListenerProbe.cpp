#include "client/DSoundListenerProbe.h"

#include "client/VrListenerBasis.h"
#include "settings/UserSettings.h"

#include <MinHook.h>

#include <windows.h>

#include <mmreg.h> // WIN32_LEAN_AND_MEAN hides WAVEFORMATEX definition

#define DIRECTSOUND_VERSION 0x0800
#include <dsound.h>

#include <array>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cwchar>
#include <iterator>

namespace
{
constexpr wchar_t kProbeEnvironment[] = L"BFVR_DSOUND_LISTENER_PROBE";
constexpr DWORD kReportIntervalMs = 15000;
// Full detail for the opening calls, then one sampled call per second. The
// opening burst is spent entirely in the main menu, so without the sampling
// no in-game listener basis is ever recorded.
constexpr LONG kDetailedCallLimit = 24;
constexpr DWORD kDetailSampleIntervalMs = 1000;
// The correction is not applied until the setter slots have been proven at
// runtime. VerifyListenerLayout only establishes the getter; the setters are
// inferred from dsound.h declaration order, and BFVR writes through them. So
// the first few commits pass BF1942's own values through untouched, read them
// back through the getter, and only arm the correction once they agree. If
// they never agree the slots are wrong and the correction stays off for the
// session rather than silently writing a bogus basis.
constexpr LONG kReadbackVerificationLimit = 3;

// Older than this and the renderer is no longer publishing - menus, loading,
// or a non-VR path - so BF1942's own listener values are left alone.
constexpr unsigned long kBasisMaxAgeMs = 250;

bool ReadFlag(const wchar_t* name, bool defaultValue) noexcept
{
    wchar_t value[8] = {};

    DWORD length = GetEnvironmentVariableW(
        name,
        value,
        static_cast<DWORD>(std::size(value)));

    if (length != 1)
        return defaultValue;

    if (value[0] == L'0')
        return false;

    return value[0] == L'1' ? true : defaultValue;
}

// BF1942 itself sometimes drives a degenerate basis, and a bad substitution is
// worse than no substitution, so the renderer's basis is checked before use
// rather than trusted.
bool IsUsableBasis(const bfvr::VrListenerBasis& basis) noexcept
{
    float forwardLengthSquared =
        basis.forwardX * basis.forwardX +
        basis.forwardY * basis.forwardY +
        basis.forwardZ * basis.forwardZ;

    float upLengthSquared =
        basis.upX * basis.upX +
        basis.upY * basis.upY +
        basis.upZ * basis.upZ;

    if (!std::isfinite(forwardLengthSquared) ||
        !std::isfinite(upLengthSquared) ||
        forwardLengthSquared < 0.25F || forwardLengthSquared > 4.0F ||
        upLengthSquared < 0.25F || upLengthSquared > 4.0F ||
        !std::isfinite(basis.positionX) || !std::isfinite(basis.positionY) ||
        !std::isfinite(basis.positionZ))
    {
        return false;
    }

    float dot =
        basis.forwardX * basis.upX +
        basis.forwardY * basis.upY +
        basis.forwardZ * basis.upZ;

    float normalized = dot / std::sqrt(forwardLengthSquared * upLengthSquared);

    return std::isfinite(normalized) && normalized > -0.99F &&
        normalized < 0.99F;
}

// Declared locally so the client keeps no dsound.lib import of its own; the
// value is IID_IDirectSound3DListener from dsound.h.
constexpr GUID kListenerInterfaceId = {
    0x279AFA84,
    0x4981,
    0x11CE,
    {0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60}};

// Vtable slots in dsound.h declaration order. IUnknown occupies 0-2.
// IDirectSound/IDirectSound8 share their first eleven entries.
constexpr std::size_t kDirectSoundCreateSoundBufferSlot = 3;
// IDirectSound3DListener: GetAllParameters, GetDistanceFactor,
// GetDopplerFactor, GetOrientation, GetPosition, GetRolloffFactor,
// GetVelocity, then the setters.
constexpr std::size_t kListenerGetOrientationSlot = 6;
constexpr std::size_t kListenerSetAllParametersSlot = 10;
constexpr std::size_t kListenerSetOrientationSlot = 13;
constexpr std::size_t kListenerSetPositionSlot = 14;
constexpr std::size_t kListenerCommitDeferredSettingsSlot = 17;

using LoadLibraryAFn = HMODULE(WINAPI*)(LPCSTR);
using LoadLibraryWFn = HMODULE(WINAPI*)(LPCWSTR);
using LoadLibraryExAFn = HMODULE(WINAPI*)(LPCSTR, HANDLE, DWORD);
using LoadLibraryExWFn = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);
using DirectSoundCreate8Fn =
    HRESULT(WINAPI*)(const GUID*, IDirectSound8**, IUnknown*);
using DirectSoundCreateFn =
    HRESULT(WINAPI*)(const GUID*, IDirectSound**, IUnknown*);
using CreateSoundBufferFn = HRESULT(STDMETHODCALLTYPE*)(
    void*, const DSBUFFERDESC*, IDirectSoundBuffer**, IUnknown*);
using GetOrientationFn =
    HRESULT(STDMETHODCALLTYPE*)(void*, D3DVECTOR*, D3DVECTOR*);
using SetOrientationFn = HRESULT(STDMETHODCALLTYPE*)(
    void*, float, float, float, float, float, float, DWORD);
using SetPositionFn =
    HRESULT(STDMETHODCALLTYPE*)(void*, float, float, float, DWORD);
using SetAllParametersFn =
    HRESULT(STDMETHODCALLTYPE*)(void*, const DS3DLISTENER*, DWORD);
using CommitDeferredSettingsFn = HRESULT(STDMETHODCALLTYPE*)(void*);

struct ListenerBasis
{
    volatile LONG sequence = 0;
    float frontX = 0.0F;
    float frontY = 0.0F;
    float frontZ = 0.0F;
    float topX = 0.0F;
    float topY = 0.0F;
    float topZ = 0.0F;
    float positionX = 0.0F;
    float positionY = 0.0F;
    float positionZ = 0.0F;
    DWORD apply = 0;
    DWORD threadId = 0;
};

// Names the module that actually implements a function, resolved from its
// address. With BF42++ acting as the dsound.dll proxy the loaded module and
// the implementation are different files - BF42++ forwards to dsound_next.dll
// - so reporting the module we attached to would credit the wrong DLL.
void DescribeImplementingModule(
    void* address,
    wchar_t* path,
    std::size_t pathSize) noexcept
{
    if (path == nullptr || pathSize == 0)
        return;

    path[0] = L'\0';
    HMODULE module = nullptr;

    if (address == nullptr ||
        GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(address),
            &module) == FALSE ||
        module == nullptr ||
        GetModuleFileNameW(module, path, static_cast<DWORD>(pathSize)) == 0)
    {
        path[0] = L'\0';
    }
}

void* ReadVTableSlot(void* instance, std::size_t slot) noexcept
{
    if (instance == nullptr)
        return nullptr;

    __try
    {
        void** const table = *reinterpret_cast<void***>(instance);
        return table == nullptr ? nullptr : table[slot];
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

class DSoundListenerProbe
{
public:
    void Start(void (*log)(const wchar_t* message))
    {
        if (InterlockedCompareExchange(&started, 1, 0) != 0)
            return;

        appendLog = log;
        diagnosticsEnabled = ReadFlag(kProbeEnvironment, false);

        // Read once here, on the injected initialization thread. The settings
        // runtime is already loaded by this point and BF1942 has not resumed,
        // so the choice is settled before any DirectSound object exists.
        bfvr::settings::UserSettingsValues userSettings =
            bfvr::settings::ProcessUserSettingsRuntime().IsReady()
                ? bfvr::settings::DecodeUserSettings(
                    bfvr::settings::ProcessUserSettingsRuntime().Current())
                : bfvr::settings::UserSettingsValues{};

        orientationOverride = userSettings.headTrackedAudioEnabled;
        positionOverride = orientationOverride && userSettings.headTrackedAudioPositionEnabled;

        if (!diagnosticsEnabled && !orientationOverride && !positionOverride)
            return;

        MH_STATUS initializeStatus = MH_Initialize();

        if (initializeStatus != MH_OK &&
            initializeStatus != MH_ERROR_ALREADY_INITIALIZED)
        {
            WriteLog(
                L"DirectSound listener probe could not initialize MinHook (status=%d).",
                static_cast<int>(initializeStatus));

            return;
        }

        active = this;

        // Static-import or already-loaded case. BF1942 normally loads
        // dsound.dll later, in which case this does nothing and the
        // LoadLibrary detours below carry the installation instead.
        if (!TryInstallDSoundHooks())
            InstallLoadLibraryHooks();

        // The worker only emits periodic summaries. With diagnostics off the
        // correction runs entirely from the hooks and there is nothing for a
        // thread to do, so none is started.
        if (!diagnosticsEnabled)
            return;

        HANDLE worker = CreateThread(
            nullptr, 0,
            &DSoundListenerProbe::Run,
            this, 0, nullptr);

        if (worker == nullptr)
        {
            WriteLog(
                L"DirectSound listener probe could not start its summary worker (%lu).",
                GetLastError());
            return;
        }

        CloseHandle(worker);
    }

private:
    static DWORD WINAPI Run(LPVOID parameter)
    {
        auto* const probe = static_cast<DSoundListenerProbe*>(parameter);

        if (probe != nullptr)
            probe->ReportUntilProcessExit();

        return 0;
    }

    // Returns true once dsound.dll has been seen, whether or not the hooks
    // themselves succeeded, so callers stop trying.
    bool TryInstallDSoundHooks()
    {
        if (InterlockedCompareExchange(&dsoundSeen, 0, 0) != 0)
            return true;

        HMODULE module = GetModuleHandleW(L"dsound.dll");

        if (module == nullptr)
            return false;

        if (InterlockedCompareExchange(&dsoundSeen, 1, 0) != 0)
            return true;

        wchar_t modulePath[MAX_PATH] = {};

        if (GetModuleFileNameW(
                module,
                modulePath,
                static_cast<DWORD>(std::size(modulePath))) == 0)
        {
            modulePath[0] = L'\0';
        }

        WriteLog(
            L"DirectSound listener probe attached to '%s'. It forwards every call unchanged and records listener creation and setter traffic only.",
            modulePath);

        InstallCreationHooks(module);

        return true;
    }

    // Installed before BF1942 resumes. Because the detour runs on the game's
    // own thread and the loader lock has already been released by the time
    // the original returns, the DirectSound hooks are in place before the
    // caller can reach GetProcAddress.
    // Anything short of all four leaves a load path unwatched. There is no
    // fallback for that: the creation hook has to exist before the game calls
    // DirectSoundCreate8, and any later discovery is already too late.
    void InstallLoadLibraryHooks()
    {
        HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
        if (kernel == nullptr)
        {
            WriteLog(
                L"DirectSound listener probe could not resolve kernel32.dll and cannot watch for a dynamic dsound.dll load.");
            return;
        }

        struct Binding
        {
            const char* name;
            void* detour;
            void** original;
        };

        const Binding bindings[] = {
            {"LoadLibraryW",
             reinterpret_cast<void*>(&DSoundListenerProbe::LoadLibraryWHook),
             reinterpret_cast<void**>(&originalLoadLibraryW)},

            {"LoadLibraryA",
             reinterpret_cast<void*>(&DSoundListenerProbe::LoadLibraryAHook),
             reinterpret_cast<void**>(&originalLoadLibraryA)},

            {"LoadLibraryExW",
             reinterpret_cast<void*>(&DSoundListenerProbe::LoadLibraryExWHook),
             reinterpret_cast<void**>(&originalLoadLibraryExW)},

            {"LoadLibraryExA",
             reinterpret_cast<void*>(&DSoundListenerProbe::LoadLibraryExAHook),
             reinterpret_cast<void**>(&originalLoadLibraryExA)}
        };

        int hooked = 0;
        for (const Binding& binding : bindings)
        {
            auto* const target =
                reinterpret_cast<void*>(GetProcAddress(kernel, binding.name));

            wchar_t wideName[32] = {};

            _snwprintf_s(
                wideName,
                std::size(wideName),
                _TRUNCATE,
                L"%hs",
                binding.name);

            if (target != nullptr &&
                CreateAndEnable(
                    target,
                    binding.detour,
                    binding.original,
                    wideName))
            {
                ++hooked;
            }
        }
        WriteLog(
            hooked == 4
                ? L"DirectSound listener probe hooked %d of 4 module-load entry points; BF1942 loads dsound.dll dynamically, so installation happens the moment that load returns."
                : L"DirectSound listener probe hooked only %d of 4 module-load entry points. A dsound.dll loaded through an unhooked path will be missed and the VR audio correction will not install for this run.",
            hooked);
    }

    static HMODULE WINAPI LoadLibraryWHook(LPCWSTR name)
    {
        DSoundListenerProbe* const probe = active;

        if (probe == nullptr || probe->originalLoadLibraryW == nullptr)
            return nullptr;

        HMODULE result = probe->originalLoadLibraryW(name);
        probe->TryInstallDSoundHooks();

        return result;
    }

    static HMODULE WINAPI LoadLibraryAHook(LPCSTR name)
    {
        DSoundListenerProbe* const probe = active;

        if (probe == nullptr || probe->originalLoadLibraryA == nullptr)
            return nullptr;

        HMODULE result = probe->originalLoadLibraryA(name);
        probe->TryInstallDSoundHooks();

        return result;
    }

    static HMODULE WINAPI LoadLibraryExWHook(
        LPCWSTR name,
        HANDLE file,
        DWORD flags)
    {
        DSoundListenerProbe* const probe = active;

        if (probe == nullptr || probe->originalLoadLibraryExW == nullptr)
            return nullptr;

        HMODULE result = probe->originalLoadLibraryExW(name, file, flags);
        probe->TryInstallDSoundHooks();

        return result;
    }

    static HMODULE WINAPI LoadLibraryExAHook(
        LPCSTR name,
        HANDLE file,
        DWORD flags)
    {
        DSoundListenerProbe* const probe = active;

        if (probe == nullptr || probe->originalLoadLibraryExA == nullptr)
            return nullptr;

        HMODULE result = probe->originalLoadLibraryExA(name, file, flags);
        probe->TryInstallDSoundHooks();

        return result;
    }

    bool InstallCreationHooks(HMODULE module)
    {
        auto* const create8 = reinterpret_cast<void*>(
            GetProcAddress(module, "DirectSoundCreate8"));

        auto* const create = reinterpret_cast<void*>(
            GetProcAddress(module, "DirectSoundCreate"));

        bool hooked8 = create8 != nullptr &&
            CreateAndEnable(
                create8,
                reinterpret_cast<void*>(&DSoundListenerProbe::Create8Hook),
                reinterpret_cast<void**>(&originalCreate8),
                L"DirectSoundCreate8");

        bool hooked = create != nullptr &&
            CreateAndEnable(
                create,
                reinterpret_cast<void*>(&DSoundListenerProbe::CreateHook),
                reinterpret_cast<void**>(&originalCreate),
                L"DirectSoundCreate");

        if (!hooked8 && !hooked)
        {
            WriteLog(
                L"DirectSound listener probe hooked neither DirectSoundCreate8 nor DirectSoundCreate; BF1942 may reach DirectSound through CoCreateInstance instead.");
            return false;
        }

        return true;
    }

    bool CreateAndEnable(
        void* target,
        void* detour,
        void** original,
        const wchar_t* name)
    {
        MH_STATUS createStatus =
            MH_CreateHook(target, detour, original);

        if (createStatus != MH_OK || *original == nullptr)
        {
            WriteLog(
                L"DirectSound listener probe could not hook %s (status=%d).",
                name,
                static_cast<int>(createStatus));

            return false;
        }

        MH_STATUS enableStatus = MH_EnableHook(target);
        if (enableStatus != MH_OK)
        {
            WriteLog(
                L"DirectSound listener probe could not enable its %s hook (status=%d).",
                name,
                static_cast<int>(enableStatus));
            MH_RemoveHook(target);
            *original = nullptr;

            return false;
        }

        return true;
    }

    static HRESULT WINAPI Create8Hook(
        const GUID* device,
        IDirectSound8** result,
        IUnknown* outer)
    {
        DSoundListenerProbe* const probe = active;

        if (probe == nullptr || probe->originalCreate8 == nullptr)
            return DSERR_UNINITIALIZED;

        HRESULT status =
            probe->originalCreate8(device, result, outer);

        InterlockedIncrement(&probe->deviceCreateCalls);

        if (SUCCEEDED(status) && result != nullptr && *result != nullptr)
            probe->HookDevice(*result, L"DirectSoundCreate8");

        return status;
    }

    static HRESULT WINAPI CreateHook(
        const GUID* device,
        IDirectSound** result,
        IUnknown* outer)
    {
        DSoundListenerProbe* const probe = active;

        if (probe == nullptr || probe->originalCreate == nullptr)
            return DSERR_UNINITIALIZED;

        HRESULT status = probe->originalCreate(device, result, outer);
        InterlockedIncrement(&probe->deviceCreateCalls);

        if (SUCCEEDED(status) && result != nullptr && *result != nullptr)
            probe->HookDevice(*result, L"DirectSoundCreate");

        return status;
    }

    // The vtable is shared by every instance of the class, so hooking it once
    // from the first created device covers whatever BF1942 goes on to use.
    void HookDevice(void* device, const wchar_t* entryPoint)
    {
        if (InterlockedCompareExchange(&deviceHooked, 1, 0) != 0)
            return;

        void* const target = ReadVTableSlot(device, kDirectSoundCreateSoundBufferSlot);

        if (target == nullptr)
        {
            WriteLog(
                L"DirectSound listener probe could not read CreateSoundBuffer from the device returned by %s.",
                entryPoint);
            return;
        }

        if (CreateAndEnable(
                target,
                reinterpret_cast<void*>(
                    &DSoundListenerProbe::CreateSoundBufferHook),
                reinterpret_cast<void**>(&originalCreateSoundBuffer),
                L"IDirectSound::CreateSoundBuffer"))
        {
            WriteLog(
                L"DirectSound listener probe observed %s and is now watching buffer creation for a 3D-capable primary buffer.",
                entryPoint);
        }
    }

    static HRESULT STDMETHODCALLTYPE CreateSoundBufferHook(
        void* self,
        const DSBUFFERDESC* description,
        IDirectSoundBuffer** buffer,
        IUnknown* outer)
    {
        DSoundListenerProbe* const probe = active;

        if (probe == nullptr || probe->originalCreateSoundBuffer == nullptr)
            return DSERR_UNINITIALIZED;

        HRESULT status = probe->originalCreateSoundBuffer(
            self,
            description,
            buffer,
            outer);

        probe->RecordBufferCreation(description);

        if (SUCCEEDED(status) && buffer != nullptr && *buffer != nullptr &&
            description != nullptr &&
            (description->dwFlags & DSBCAPS_PRIMARYBUFFER) != 0 &&
            (description->dwFlags & DSBCAPS_CTRL3D) != 0)
        {
            probe->CaptureListener(*buffer);
        }

        return status;
    }

    void RecordBufferCreation(const DSBUFFERDESC* description) noexcept
    {
        InterlockedIncrement(&bufferCreateCalls);

        if (description == nullptr)
            return;

        if ((description->dwFlags & DSBCAPS_PRIMARYBUFFER) != 0)
            InterlockedIncrement(&primaryBufferCreateCalls);

        if ((description->dwFlags & DSBCAPS_CTRL3D) != 0)
            InterlockedIncrement(&control3dBufferCreateCalls);
    }

    // Querying the listener ourselves avoids hooking QueryInterface on a hot
    // path. The interface can only be reached through a 3D-capable primary
    // buffer, so this sees the same object BF1942 will get, and its vtable is
    // the one the game's own calls travel through.
    void CaptureListener(IDirectSoundBuffer* primaryBuffer)
    {
        if (InterlockedCompareExchange(&listenerHooked, 1, 0) != 0)
            return;

        void* listener = nullptr;

        HRESULT status = primaryBuffer->QueryInterface(
            kListenerInterfaceId,
            &listener);

        if (FAILED(status) || listener == nullptr)
        {
            WriteLog(
                L"DirectSound listener probe saw a 3D-capable primary buffer, but IID_IDirectSound3DListener was unavailable (hr=0x%08lX). BFVR cannot correct the listener at this boundary.",
                static_cast<unsigned long>(status));
            InterlockedExchange(&listenerHooked, 0);

            return;
        }

        InterlockedExchange(&listenerAvailable, 1);
        HookListener(listener);

        static_cast<IUnknown*>(listener)->Release();
    }

    // The setter slots below are hardcoded from dsound.h declaration order.
    // Hooking the wrong slot would mean forwarding through a mismatched
    // __stdcall signature and corrupting the stack, so prove the numbering
    // first with the read-only getter that sits in the same block. A failure
    // here means the layout assumption is wrong and nothing gets hooked.
    bool VerifyListenerLayout(void* listener)
    {
        auto* const getOrientation = reinterpret_cast<GetOrientationFn>(
            ReadVTableSlot(listener, kListenerGetOrientationSlot));

        if (getOrientation == nullptr)
            return false;

        D3DVECTOR front = {};
        D3DVECTOR top = {};
        HRESULT status = E_FAIL;

        __try
        {
            status = getOrientation(listener, &front, &top);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            status = E_FAIL;
        }

        bool finite =
            std::isfinite(front.x) && std::isfinite(front.y) &&
            std::isfinite(front.z) && std::isfinite(top.x) &&
            std::isfinite(top.y) && std::isfinite(top.z);

        if (FAILED(status) || !finite)
        {
            WriteLog(
                L"DirectSound listener probe could not confirm the IDirectSound3DListener vtable layout (GetOrientation hr=0x%08lX finite=%d); refusing to hook any setter.",
                static_cast<unsigned long>(status),
                finite ? 1 : 0);
            return false;
        }

        WriteLog(
            L"DirectSound listener probe confirmed the vtable layout; the engine's current listener basis is front=(%.4f %.4f %.4f) top=(%.4f %.4f %.4f).",
            front.x, front.y, front.z,
            top.x, top.y, top.z);
        listenerGetOrientation = getOrientation;

        return true;
    }

    // False until the post-commit readback has proven the setter slots, and
    // permanently false if that proof fails. Nothing is substituted until then.
    [[nodiscard]] bool SetterSlotsVerified() noexcept
    {
        return InterlockedCompareExchange(&overrideArmed, 0, 0) > 0;
    }

    // Must run after CommitDeferredSettings, never straight after the setter.
    // BF1942 sets with DS3D_DEFERRED, so until the commit the getter still
    // returns the previously applied basis and a comparison would report a
    // mismatch on every call that changed the orientation.
    void VerifyOrientationReadback(void* self)
    {
        if (listenerGetOrientation == nullptr ||
            InterlockedCompareExchange(&overrideArmed, 0, 0) != 0 ||
            InterlockedCompareExchange(&setOrientationCalls, 0, 0) == 0)
        {
            return;
        }

        LONG attempt = InterlockedIncrement(&readbackChecks);

        float frontX = basis.frontX;
        float frontY = basis.frontY;
        float frontZ = basis.frontZ;

        float topX = basis.topX;
        float topY = basis.topY;
        float topZ = basis.topZ;

        D3DVECTOR front = {};
        D3DVECTOR top = {};

        HRESULT status = E_FAIL;

        __try
        {
            status = listenerGetOrientation(self, &front, &top);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            status = E_FAIL;
        }

        bool matches = SUCCEEDED(status) &&
            front.x == frontX &&
            front.y == frontY && front.z == frontZ && top.x == topX &&
            top.y == topY && top.z == topZ;

        if (matches)
        {
            InterlockedExchange(&overrideArmed, 1);
        }
        else if (attempt >= kReadbackVerificationLimit)
        {
            InterlockedExchange(&overrideArmed, -1);
        }
        else
        {
            // Inconclusive so far; the correction stays off and the next
            // commit tries again.
            return;
        }

        WriteLog(
            L"DirectSound listener setter verification %s after %ld attempt(s): %s. Last set front=(%.4f %.4f %.4f) top=(%.4f %.4f %.4f), read back hr=0x%08lX front=(%.4f %.4f %.4f) top=(%.4f %.4f %.4f).",
            matches ? L"confirmed" : L"FAILED",
            attempt,
            matches
                ? L"the VR head correction is now active"
                : L"the assumed setter vtable slot or signature is wrong, so the correction stays disabled for this session and BF1942 keeps its own listener",
            frontX, frontY, frontZ,
            topX, topY, topZ,
            static_cast<unsigned long>(status),
            front.x, front.y, front.z,
            top.x, top.y, top.z);
    }

    void HookListener(void* listener)
    {
        if (!VerifyListenerLayout(listener))
            return;

        struct Binding
        {
            std::size_t slot;
            void* detour;
            void** original;
            const wchar_t* name;
        };

        const Binding bindings[] = {
            {kListenerSetOrientationSlot,
             reinterpret_cast<void*>(&DSoundListenerProbe::SetOrientationHook),
             reinterpret_cast<void**>(&originalSetOrientation),
             L"IDirectSound3DListener::SetOrientation"},

            {kListenerSetPositionSlot,
             reinterpret_cast<void*>(&DSoundListenerProbe::SetPositionHook),
             reinterpret_cast<void**>(&originalSetPosition),
             L"IDirectSound3DListener::SetPosition"},

            {kListenerSetAllParametersSlot,
             reinterpret_cast<void*>(
                 &DSoundListenerProbe::SetAllParametersHook),
             reinterpret_cast<void**>(&originalSetAllParameters),
             L"IDirectSound3DListener::SetAllParameters"},

            {kListenerCommitDeferredSettingsSlot,
             reinterpret_cast<void*>(
                 &DSoundListenerProbe::CommitDeferredSettingsHook),
             reinterpret_cast<void**>(&originalCommitDeferredSettings),
             L"IDirectSound3DListener::CommitDeferredSettings"}
        };

        int hooked = 0;
        for (const Binding& binding : bindings)
        {
            void* const target = ReadVTableSlot(listener, binding.slot);

            if (target != nullptr &&
                CreateAndEnable(
                    target,
                    binding.detour,
                    binding.original,
                    binding.name))
            {
                ++hooked;
            }
        }

        wchar_t implementingPath[MAX_PATH] = {};

        DescribeImplementingModule(
            ReadVTableSlot(listener, kListenerSetOrientationSlot),
            implementingPath,
            std::size(implementingPath));

        WriteLog(
            L"DirectSound listener acquired IID_IDirectSound3DListener and hooked %d of 4 setters. The listener is implemented by '%s'. VR head orientation is %s and head position is %s. Nothing is substituted until the first commits verify the setter slots; after that the renderer's composed camera replaces BF1942's own listener basis while it stays fresh, and BF1942's values pass through untouched in menus, during loading, and on any non-VR path.",
            hooked,
            implementingPath[0] == L'\0' ? L"unresolved" : implementingPath,
            orientationOverride ? L"applied" : L"disabled",
            positionOverride ? L"applied" : L"disabled (default)");
    }

    static HRESULT STDMETHODCALLTYPE SetOrientationHook(
        void* self,
        float frontX,
        float frontY,
        float frontZ,
        float topX,
        float topY,
        float topZ,
        DWORD apply)
    {
        DSoundListenerProbe* const probe = active;

        if (probe == nullptr || probe->originalSetOrientation == nullptr)
            return DSERR_UNINITIALIZED;

        bfvr::VrListenerBasis head = {};

        bool overridden =
            probe->orientationOverride &&
            probe->SetterSlotsVerified() &&
            bfvr::ReadVrListenerBasis(head, kBasisMaxAgeMs) &&
            IsUsableBasis(head);

        // BF1942 derives its listener from the untouched game camera, which is
        // why head rotation moved the image but not the sound. The renderer's
        // composed camera already carries the game basis with the head folded
        // in, so substituting it outright keeps the two in agreement instead
        // of layering a second, competing rotation on top.
        float appliedFrontX = overridden ? head.forwardX : frontX;
        float appliedFrontY = overridden ? head.forwardY : frontY;
        float appliedFrontZ = overridden ? head.forwardZ : frontZ;

        float appliedTopX = overridden ? head.upX : topX;
        float appliedTopY = overridden ? head.upY : topY;
        float appliedTopZ = overridden ? head.upZ : topZ;

        if (overridden)
            InterlockedIncrement(&probe->overriddenOrientationCalls);

        probe->RecordOrientation(
            frontX, frontY, frontZ,
            topX, topY, topZ,
            apply,
            overridden,
            appliedFrontX, appliedFrontY, appliedFrontZ,
            appliedTopX, appliedTopY, appliedTopZ);

        return probe->originalSetOrientation(
            self,
            appliedFrontX, appliedFrontY, appliedFrontZ,
            appliedTopX, appliedTopY, appliedTopZ,
            apply);
    }

    static HRESULT STDMETHODCALLTYPE SetPositionHook(
        void* self,
        float x,
        float y,
        float z,
        DWORD apply)
    {
        DSoundListenerProbe* const probe = active;

        if (probe == nullptr || probe->originalSetPosition == nullptr)
            return DSERR_UNINITIALIZED;

        bfvr::VrListenerBasis head = {};

        bool overridden =
            probe->positionOverride &&
            probe->SetterSlotsVerified() &&
            bfvr::ReadVrListenerBasis(head, kBasisMaxAgeMs) &&
            IsUsableBasis(head);

        float appliedX = overridden ? head.positionX : x;
        float appliedY = overridden ? head.positionY : y;
        float appliedZ = overridden ? head.positionZ : z;

        if (overridden)
            InterlockedIncrement(&probe->overriddenPositionCalls);

        probe->RecordPosition(appliedX, appliedY, appliedZ);

        return probe->originalSetPosition(
            self,
            appliedX, appliedY, appliedZ,
            apply);
    }

    static HRESULT STDMETHODCALLTYPE SetAllParametersHook(
        void* self,
        const DS3DLISTENER* parameters,
        DWORD apply)
    {
        DSoundListenerProbe* const probe = active;

        if (probe == nullptr || probe->originalSetAllParameters == nullptr)
            return DSERR_UNINITIALIZED;

        InterlockedIncrement(&probe->setAllParametersCalls);

        if (parameters == nullptr)
            return probe->originalSetAllParameters(self, parameters, apply);

        // BF1942 was never observed using this entry point, but it is the one
        // call that could set orientation behind the setter above, so it is
        // corrected too rather than left as a silent bypass.
        bfvr::VrListenerBasis head = {};

        bool basisReady =
            probe->SetterSlotsVerified() &&
            bfvr::ReadVrListenerBasis(head, kBasisMaxAgeMs) &&
            IsUsableBasis(head);

        bool overrideOrientation =
            probe->orientationOverride && basisReady;

        bool overridePosition = probe->positionOverride && basisReady;

        DS3DLISTENER applied = *parameters;
        if (overrideOrientation)
        {
            applied.vOrientFront.x = head.forwardX;
            applied.vOrientFront.y = head.forwardY;
            applied.vOrientFront.z = head.forwardZ;
            applied.vOrientTop.x = head.upX;
            applied.vOrientTop.y = head.upY;
            applied.vOrientTop.z = head.upZ;
            InterlockedIncrement(&probe->overriddenOrientationCalls);
        }

        if (overridePosition)
        {
            applied.vPosition.x = head.positionX;
            applied.vPosition.y = head.positionY;
            applied.vPosition.z = head.positionZ;
            InterlockedIncrement(&probe->overriddenPositionCalls);
        }

        probe->RecordOrientation(
            parameters->vOrientFront.x, parameters->vOrientFront.y, parameters->vOrientFront.z,
            parameters->vOrientTop.x, parameters->vOrientTop.y, parameters->vOrientTop.z,
            apply,
            overrideOrientation,
            applied.vOrientFront.x, applied.vOrientFront.y, applied.vOrientFront.z,
            applied.vOrientTop.x, applied.vOrientTop.y, applied.vOrientTop.z);

        probe->RecordPosition(
            applied.vPosition.x, applied.vPosition.y, applied.vPosition.z);

        return probe->originalSetAllParameters(self, &applied, apply);
    }

    static HRESULT STDMETHODCALLTYPE CommitDeferredSettingsHook(void* self)
    {
        DSoundListenerProbe* const probe = active;

        if (probe == nullptr || probe->originalCommitDeferredSettings == nullptr)
            return DSERR_UNINITIALIZED;

        InterlockedIncrement(&probe->commitCalls);

        HRESULT status = probe->originalCommitDeferredSettings(self);

        probe->VerifyOrientationReadback(self);

        return status;
    }

    // The recorded basis is the one actually submitted, so the post-commit
    // readback verifies what BFVR applied rather than what BF1942 proposed.
    void RecordOrientation(
        float gameFrontX,
        float gameFrontY,
        float gameFrontZ,
        float gameTopX,
        float gameTopY,
        float gameTopZ,
        DWORD apply,
        bool overridden,
        float frontX,
        float frontY,
        float frontZ,
        float topX,
        float topY,
        float topZ) noexcept
    {
        LONG sequence = InterlockedIncrement(&setOrientationCalls);

        InterlockedExchange(&basis.sequence, 0);

        basis.frontX = frontX;
        basis.frontY = frontY;
        basis.frontZ = frontZ;

        basis.topX = topX;
        basis.topY = topY;
        basis.topZ = topZ;

        basis.apply = apply;
        basis.threadId = GetCurrentThreadId();

        MemoryBarrier();
        InterlockedExchange(&basis.sequence, sequence);

        if (!diagnosticsEnabled)
            return;

        DWORD now = GetTickCount();

        bool sample = sequence <= kDetailedCallLimit ||
            now - static_cast<DWORD>(
                InterlockedCompareExchange(&lastDetailTick, 0, 0)) >=
                kDetailSampleIntervalMs;

        if (sample)
        {
            InterlockedExchange(&lastDetailTick, static_cast<LONG>(now));
            WriteLog(
                L"DirectSound listener orientation seq=%ld thread=%lu apply=%lu source=%s game front=(%.4f %.4f %.4f) top=(%.4f %.4f %.4f), submitted front=(%.4f %.4f %.4f) top=(%.4f %.4f %.4f).",
                sequence,
                basis.threadId,
                apply,
                overridden ? L"BFVR head" : L"BF1942 (unchanged)",
                gameFrontX, gameFrontY, gameFrontZ,
                gameTopX, gameTopY, gameTopZ,
                frontX, frontY, frontZ,
                topX, topY, topZ);
        }
    }

    void RecordPosition(float x, float y, float z) noexcept
    {
        InterlockedIncrement(&setPositionCalls);

        basis.positionX = x;
        basis.positionY = y;
        basis.positionZ = z;
    }

    void ReportUntilProcessExit()
    {
        for (;;)
        {
            Sleep(kReportIntervalMs);
            LogSummary();
        }
    }

    void LogSummary()
    {
        LONG orientationCalls =
            InterlockedCompareExchange(&setOrientationCalls, 0, 0);

        WriteLog(
            L"DirectSound listener summary: deviceCreates=%ld bufferCreates=%ld primaryBuffers=%ld ctrl3dBuffers=%ld listener=%s setOrientation=%ld (+%ld) setPosition=%ld setAllParameters=%ld commitDeferred=%ld headOrientationApplied=%ld headPositionApplied=%ld thread=%lu front=(%.4f %.4f %.4f) top=(%.4f %.4f %.4f) position=(%.2f %.2f %.2f).",
            InterlockedCompareExchange(&deviceCreateCalls, 0, 0),
            InterlockedCompareExchange(&bufferCreateCalls, 0, 0),
            InterlockedCompareExchange(&primaryBufferCreateCalls, 0, 0),
            InterlockedCompareExchange(&control3dBufferCreateCalls, 0, 0),
            InterlockedCompareExchange(&listenerAvailable, 0, 0) != 0
                ? L"present"
                : L"absent",
            orientationCalls,
            orientationCalls - reportedOrientationCalls,
            InterlockedCompareExchange(&setPositionCalls, 0, 0),
            InterlockedCompareExchange(&setAllParametersCalls, 0, 0),
            InterlockedCompareExchange(&commitCalls, 0, 0),
            InterlockedCompareExchange(&overriddenOrientationCalls, 0, 0),
            InterlockedCompareExchange(&overriddenPositionCalls, 0, 0),
            basis.threadId,
            basis.frontX, basis.frontY, basis.frontZ,
            basis.topX, basis.topY, basis.topZ,
            basis.positionX, basis.positionY, basis.positionZ);

        reportedOrientationCalls = orientationCalls;
    }

    void WriteLog(const wchar_t* format, ...) const
    {
        if (appendLog == nullptr)
            return;

        std::array<wchar_t, 1200> message = {};

        va_list arguments;
        va_start(arguments, format);

        _vsnwprintf_s(
            message.data(),
            message.size(),
            _TRUNCATE,
            format,
            arguments);

        va_end(arguments);

        appendLog(message.data());
    }

    static DSoundListenerProbe* active;

    void (*appendLog)(const wchar_t* message) = nullptr;
    LoadLibraryAFn originalLoadLibraryA = nullptr;
    LoadLibraryWFn originalLoadLibraryW = nullptr;
    LoadLibraryExAFn originalLoadLibraryExA = nullptr;
    LoadLibraryExWFn originalLoadLibraryExW = nullptr;
    DirectSoundCreate8Fn originalCreate8 = nullptr;
    DirectSoundCreateFn originalCreate = nullptr;
    CreateSoundBufferFn originalCreateSoundBuffer = nullptr;
    GetOrientationFn listenerGetOrientation = nullptr;
    SetOrientationFn originalSetOrientation = nullptr;
    SetPositionFn originalSetPosition = nullptr;
    SetAllParametersFn originalSetAllParameters = nullptr;
    CommitDeferredSettingsFn originalCommitDeferredSettings = nullptr;

    ListenerBasis basis = {};

    volatile LONG started = 0;
    volatile LONG dsoundSeen = 0;
    volatile LONG deviceHooked = 0;
    volatile LONG listenerHooked = 0;
    volatile LONG listenerAvailable = 0;
    volatile LONG deviceCreateCalls = 0;
    volatile LONG bufferCreateCalls = 0;
    volatile LONG primaryBufferCreateCalls = 0;
    volatile LONG control3dBufferCreateCalls = 0;

    bool diagnosticsEnabled = false;
    bool orientationOverride = false;
    bool positionOverride = false;

    volatile LONG overriddenOrientationCalls = 0;
    volatile LONG overriddenPositionCalls = 0;
    // 0 = setter slots not yet proven, 1 = proven, -1 = proof failed.
    volatile LONG overrideArmed = 0;
    volatile LONG readbackChecks = 0;
    volatile LONG lastDetailTick = 0;
    volatile LONG setOrientationCalls = 0;
    volatile LONG setPositionCalls = 0;
    volatile LONG setAllParametersCalls = 0;
    volatile LONG commitCalls = 0;
    // Touched only by the reporting worker, so the per-interval delta stays
    // correct without an interlocked read.
    LONG reportedOrientationCalls = 0;
};

DSoundListenerProbe* DSoundListenerProbe::active = nullptr;
DSoundListenerProbe g_probe = {};

} // namespace

namespace bfvr
{

void StartDSoundListenerProbe(void (*appendLog)(const wchar_t* message))
{
    g_probe.Start(appendLog);
}

} // namespace bfvr
