#include "client/WeaponFireProbe.h"

#include <MinHook.h>

#include <windows.h>

#include <array>
#include <cstdarg>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <intrin.h>

namespace
{
constexpr std::ptrdiff_t kWeaponFireCoreRva = 0x0013CDB0;
constexpr std::ptrdiff_t kPlayerManagerGlobalRva = 0x0057D76C;
constexpr std::size_t kPlayerManagerLocalPlayerOffset = 0x54;
constexpr DWORD kObservationWindowMs = 180000;
constexpr DWORD kReportIntervalMs = 250;
constexpr std::size_t kCallCapacity = 128;
constexpr BYTE kWeaponFireCorePrefix[] = {
    0x81, 0xEC, 0xB8, 0x01, 0x00, 0x00, 0x53, 0x55,
    0x8B, 0xE9, 0x8B, 0x85, 0xB4, 0x01, 0x00, 0x00};

struct Matrix
{
    float values[4][4] = {};
};
static_assert(sizeof(Matrix) == sizeof(float) * 16);

struct FireCall
{
    volatile LONG sequence = 0;
    void* weapon = nullptr;
    void* actor = nullptr;
    void* localPlayer = nullptr;
    DWORD barrelIndex = 0;
    DWORD threadId = 0;
    DWORD callerRva = 0;
    bool actorMatchesLocalPlayer = false;
    bool matrixReadable = false;
    bool matrixFinite = false;
    Matrix matrix = {};
};

bool HasExpectedPrefix(
    const void* target,
    const BYTE* expected,
    std::size_t expectedLength) noexcept
{
    if (target == nullptr || expected == nullptr || expectedLength == 0)
    {
        return false;
    }
    __try
    {
        return std::memcmp(target, expected, expectedLength) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool IsFiniteMatrix(const Matrix& matrix) noexcept
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

class WeaponFireProbe
{
public:
    using FireFn = void(__thiscall*)(void*, void*, const Matrix*, DWORD);

    void Start(void* image, void (*log)(const wchar_t* message))
    {
        if (InterlockedCompareExchange(&started, 1, 0) != 0)
        {
            WriteLog(L"Weapon-fire probe ignored a duplicate start request.");
            return;
        }
        gameImage = static_cast<std::byte*>(image);
        appendLog = log;
        fireTarget = gameImage == nullptr ? nullptr : gameImage + kWeaponFireCoreRva;
        if (!HasExpectedPrefix(
                fireTarget,
                kWeaponFireCorePrefix,
                sizeof(kWeaponFireCorePrefix)))
        {
            WriteLog(
                L"Weapon-fire probe rejected profiled target %p: the WinPC fire-core prefix differs.",
                fireTarget);
            return;
        }

        const MH_STATUS initializeStatus = MH_Initialize();
        if (initializeStatus == MH_OK)
        {
            ownsMinHook = true;
        }
        else if (initializeStatus != MH_ERROR_ALREADY_INITIALIZED)
        {
            WriteLog(
                L"Weapon-fire probe could not initialize MinHook (status=%d).",
                static_cast<int>(initializeStatus));
            return;
        }

        const MH_STATUS createStatus = MH_CreateHook(
            fireTarget,
            reinterpret_cast<LPVOID>(&WeaponFireProbe::FireHook),
            reinterpret_cast<LPVOID*>(&originalFire));
        if (createStatus != MH_OK || originalFire == nullptr)
        {
            WriteLog(
                L"Weapon-fire probe could not create its forwarding fire hook (status=%d).",
                static_cast<int>(createStatus));
            RemoveHook();
            return;
        }
        hookCreated = true;
        active = this;
        const MH_STATUS enableStatus = MH_EnableHook(fireTarget);
        if (enableStatus != MH_OK)
        {
            WriteLog(
                L"Weapon-fire probe could not enable its forwarding fire hook (status=%d).",
                static_cast<int>(enableStatus));
            RemoveHook();
            return;
        }
        hookEnabled = true;

        HANDLE worker = CreateThread(
            nullptr,
            0,
            &WeaponFireProbe::Run,
            this,
            0,
            nullptr);
        if (worker == nullptr)
        {
            WriteLog(
                L"Weapon-fire probe could not start its reporting worker (%lu).",
                GetLastError());
            RemoveHook();
            return;
        }
        CloseHandle(worker);
        WriteLog(
            L"Weapon-fire probe enabled a 180-second forwarding hook at 0x0053CDB0. It records only the native fire matrix, barrel index, caller, and a raw local-player pointer comparison; it writes no BF1942 input, camera, weapon, projectile, ray, or network state.");
    }

private:
    static void __fastcall FireHook(
        void* weapon,
        void*,
        void* actor,
        const Matrix* matrix,
        DWORD barrelIndex)
    {
        WeaponFireProbe* const probe = active;
        if (probe == nullptr || probe->originalFire == nullptr)
        {
            return;
        }
        probe->RecordCall(weapon, actor, matrix, barrelIndex, _ReturnAddress());
        probe->originalFire(weapon, actor, matrix, barrelIndex);
    }

    static DWORD WINAPI Run(LPVOID parameter)
    {
        auto* const probe = static_cast<WeaponFireProbe*>(parameter);
        if (probe != nullptr)
        {
            probe->ReportUntilComplete();
        }
        return 0;
    }

    void RecordCall(
        void* weapon,
        void* actor,
        const Matrix* matrix,
        DWORD barrelIndex,
        const void* callerReturn) noexcept
    {
        const LONG sequence = InterlockedIncrement(&nextCallSequence);
        FireCall& call = calls[static_cast<std::size_t>(sequence) % calls.size()];
        InterlockedExchange(&call.sequence, 0);
        call.weapon = weapon;
        call.actor = actor;
        call.localPlayer = ReadLocalPlayer();
        call.barrelIndex = barrelIndex;
        call.threadId = GetCurrentThreadId();
        call.callerRva = ToRva(callerReturn);
        call.actorMatchesLocalPlayer =
            call.actor != nullptr && call.actor == call.localPlayer;
        call.matrix = {};
        call.matrixReadable = false;
        call.matrixFinite = false;
        if (matrix != nullptr)
        {
            __try
            {
                std::memcpy(&call.matrix, matrix, sizeof(call.matrix));
                call.matrixReadable = true;
                call.matrixFinite = IsFiniteMatrix(call.matrix);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                call.matrixReadable = false;
                call.matrixFinite = false;
            }
        }
        MemoryBarrier();
        InterlockedExchange(&call.sequence, sequence);
    }

    void* ReadLocalPlayer() const noexcept
    {
        if (gameImage == nullptr)
        {
            return nullptr;
        }
        __try
        {
            void* manager = *reinterpret_cast<void* const*>(
                gameImage + kPlayerManagerGlobalRva);
            return manager == nullptr
                ? nullptr
                : *reinterpret_cast<void* const*>(
                    static_cast<const std::byte*>(manager) +
                    kPlayerManagerLocalPlayerOffset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void ReportUntilComplete()
    {
        LONG nextSequence = 1;
        const DWORD startedAt = GetTickCount();
        while (GetTickCount() - startedAt < kObservationWindowMs)
        {
            Sleep(kReportIntervalMs);
            ReportCalls(nextSequence);
        }
        ReportCalls(nextSequence);
        WriteLog(
            L"Weapon-fire probe completed: observedCalls=%ld. Removing the forwarding hook without changing BF1942 state.",
            InterlockedCompareExchange(&nextCallSequence, 0, 0));
        RemoveHook();
    }

    void ReportCalls(LONG& nextSequence)
    {
        const LONG latest = InterlockedCompareExchange(&nextCallSequence, 0, 0);
        if (latest < nextSequence)
        {
            return;
        }
        if (latest - nextSequence >= static_cast<LONG>(calls.size()))
        {
            WriteLog(
                L"Weapon-fire probe call ring overran %ld observation(s); retaining the newest %zu.",
                latest - nextSequence - static_cast<LONG>(calls.size()) + 1,
                calls.size());
            nextSequence = latest - static_cast<LONG>(calls.size()) + 1;
        }
        for (; nextSequence <= latest; ++nextSequence)
        {
            const FireCall& call = calls[
                static_cast<std::size_t>(nextSequence) % calls.size()];
            if (InterlockedCompareExchange(
                    const_cast<volatile LONG*>(&call.sequence),
                    0,
                    0) != nextSequence)
            {
                continue;
            }
            WriteLog(
                L"Weapon-fire call seq=%ld weapon=%p actor=%p localPlayer=%p actorIsLocal=%d barrel=%ld thread=%lu callerRva=0x%05lX matrixReadable=%d finite=%d rows=[%.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f].",
                nextSequence,
                call.weapon,
                call.actor,
                call.localPlayer,
                call.actorMatchesLocalPlayer ? 1 : 0,
                static_cast<LONG>(call.barrelIndex),
                call.threadId,
                call.callerRva,
                call.matrixReadable ? 1 : 0,
                call.matrixFinite ? 1 : 0,
                call.matrix.values[0][0], call.matrix.values[0][1],
                call.matrix.values[0][2], call.matrix.values[0][3],
                call.matrix.values[1][0], call.matrix.values[1][1],
                call.matrix.values[1][2], call.matrix.values[1][3],
                call.matrix.values[2][0], call.matrix.values[2][1],
                call.matrix.values[2][2], call.matrix.values[2][3],
                call.matrix.values[3][0], call.matrix.values[3][1],
                call.matrix.values[3][2], call.matrix.values[3][3]);
        }
    }

    DWORD ToRva(const void* address) const noexcept
    {
        const auto image = reinterpret_cast<std::uintptr_t>(gameImage);
        const auto value = reinterpret_cast<std::uintptr_t>(address);
        return image != 0 && value >= image
            ? static_cast<DWORD>(value - image)
            : 0;
    }

    void RemoveHook()
    {
        if (hookEnabled)
        {
            MH_DisableHook(fireTarget);
            hookEnabled = false;
        }
        if (hookCreated)
        {
            MH_RemoveHook(fireTarget);
            hookCreated = false;
        }
        if (active == this)
        {
            active = nullptr;
        }
        originalFire = nullptr;
        if (ownsMinHook)
        {
            MH_Uninitialize();
            ownsMinHook = false;
        }
    }

    void WriteLog(const wchar_t* format, ...) const
    {
        if (appendLog == nullptr)
        {
            return;
        }
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

    static WeaponFireProbe* active;

    std::byte* gameImage = nullptr;
    void (*appendLog)(const wchar_t* message) = nullptr;
    void* fireTarget = nullptr;
    FireFn originalFire = nullptr;
    std::array<FireCall, kCallCapacity> calls = {};
    volatile LONG started = 0;
    volatile LONG nextCallSequence = 0;
    bool ownsMinHook = false;
    bool hookCreated = false;
    bool hookEnabled = false;
};

WeaponFireProbe* WeaponFireProbe::active = nullptr;
WeaponFireProbe g_probe = {};

} // namespace

namespace bfvr
{

void StartWeaponFireProbe(
    void* gameImage,
    void (*appendLog)(const wchar_t* message))
{
    g_probe.Start(gameImage, appendLog);
}

} // namespace bfvr
