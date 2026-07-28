#include "client/WeaponAimOverlay.h"

#include "client/ControllerInputCache.h"
#include "client/WeaponPoseRuntimeCache.h"
#include "presenter/SharedPresentationProtocol.h"
#include "stereo/WeaponFireAimMath.h"

#include <MinHook.h>

#include <windows.h>

#include <array>
#include <algorithm>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <intrin.h>

namespace
{

constexpr wchar_t kEnableWeaponMotionEnvironment[] =
    L"BFVR_ENABLE_WEAPON_MOTION";
constexpr std::ptrdiff_t kWeaponFireCoreRva = 0x0013CDB0;
// WeaponFire_Ordinary dispatches the same fire core through five mutually
// exclusive native branches: the default barrel, barrel zero, an indexed
// multi-barrel loop, every configured barrel, and the rotating-barrel case.
// These are all direct calls in the profiled 0x0053D7B0 function.
constexpr std::array<std::ptrdiff_t, 5> kExpectedCallerReturnRvas = {
    0x0013DBAE,
    0x0013DBCE,
    0x0013DC0B,
    0x0013DC6E,
    0x0013DC9E};
constexpr std::ptrdiff_t kPlayerManagerGlobalRva = 0x0057D76C;
constexpr std::size_t kPlayerManagerLocalPlayerOffset = 0x54;
constexpr std::size_t kBFPlayerIsAliveOffset = 0xA9;
constexpr DWORD kControllerHandRight = 1;
constexpr DWORD kControllerSampleMaximumAgeMs = 125;
constexpr std::size_t kRecordCapacity = 16;
constexpr BYTE kWeaponFireCorePrefix[] = {
    0x81, 0xEC, 0xB8, 0x01, 0x00, 0x00, 0x53, 0x55,
    0x8B, 0xE9, 0x8B, 0x85, 0xB4, 0x01, 0x00, 0x00};

struct AimRecord
{
    volatile LONG sequence = 0;
    LONG trackingGeneration = 0;
    LONG visualGeneration = 0;
    DWORD barrelIndex = 0;
    float nativeForward[3] = {};
    float adjustedForward[3] = {};
    float nativePosition[3] = {};
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

class WeaponAimOverlay
{
public:
    using FireFn = void(__thiscall*)(
        void* weapon,
        void* actor,
        const bfvr::stereo::Matrix4* matrix,
        DWORD barrelIndex);

    void Start(
        void* image,
        void (*log)(const wchar_t* message))
    {
        if (InterlockedCompareExchange(&started, 1, 0) != 0)
        {
            return;
        }
        appendLog = log;
        wchar_t enabled[2] = {};
        if (GetEnvironmentVariableW(
                kEnableWeaponMotionEnvironment,
                enabled,
                static_cast<DWORD>(std::size(enabled))) != 1 ||
            enabled[0] != L'1')
        {
            InterlockedExchange(&started, 0);
            return;
        }

        gameImage = static_cast<std::byte*>(image);
        fireTarget = gameImage == nullptr
            ? nullptr
            : gameImage + kWeaponFireCoreRva;
        if (!HasExpectedPrefix(
                fireTarget,
                kWeaponFireCorePrefix,
                sizeof(kWeaponFireCorePrefix)))
        {
            WriteLog(
                L"Controller-aim fire overlay rejected profiled target %p: the WinPC WeaponFire_Core prefix differs.",
                fireTarget);
            InterlockedExchange(&started, 0);
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
                L"Controller-aim fire overlay could not initialize MinHook (status=%d).",
                static_cast<int>(initializeStatus));
            InterlockedExchange(&started, 0);
            return;
        }

        const MH_STATUS createStatus = MH_CreateHook(
            fireTarget,
            reinterpret_cast<LPVOID>(&WeaponAimOverlay::FireHook),
            reinterpret_cast<LPVOID*>(&originalFire));
        if (createStatus != MH_OK || originalFire == nullptr)
        {
            WriteLog(
                L"Controller-aim fire overlay could not create its WeaponFire_Core hook (status=%d).",
                static_cast<int>(createStatus));
            RemoveHook();
            InterlockedExchange(&started, 0);
            return;
        }
        hookCreated = true;
        InterlockedExchangePointer(&active, this);
        const MH_STATUS enableStatus = MH_EnableHook(fireTarget);
        if (enableStatus != MH_OK)
        {
            WriteLog(
                L"Controller-aim fire overlay could not enable its WeaponFire_Core hook (status=%d).",
                static_cast<int>(enableStatus));
            RemoveHook();
            InterlockedExchange(&started, 0);
            return;
        }
        hookEnabled = true;
        WriteLog(
            L"Controller-directed fire overlay armed at 0x0053CDB0. Only the five verified branches in WeaponFire_Ordinary (default, barrel zero, indexed loop, all barrels, or rotating barrel) can receive the exact fresh world attachment already used by the rendered weapon for the alive local infantry player. BF1942 retains the native fire-matrix position, weapon/barrel muzzle offsets, spread, cadence, projectile creation, and networking path; every failed gate forwards the original matrix.");
    }

    void Stop()
    {
        if (InterlockedCompareExchange(&started, 0, 0) == 0)
        {
            return;
        }
        if (hookEnabled)
        {
            MH_DisableHook(fireTarget);
            hookEnabled = false;
        }
        InterlockedCompareExchangePointer(&active, nullptr, this);
        while (InterlockedCompareExchange(&callbackEntrants, 0, 0) != 0)
        {
            Sleep(0);
        }
        Report();
        RemoveHook();
        InterlockedExchange(&started, 0);
    }

private:
    static void __fastcall FireHook(
        void* weapon,
        void*,
        void* actor,
        const bfvr::stereo::Matrix4* matrix,
        DWORD barrelIndex)
    {
        // Enter before reading active. Stop first makes the native detour
        // unreachable, clears active, and then waits on this counter. A thread
        // already inside this function can therefore never race teardown of
        // originalFire or its MinHook trampoline.
        InterlockedIncrement(&callbackEntrants);
        WeaponAimOverlay* const overlay =
            static_cast<WeaponAimOverlay*>(
                InterlockedCompareExchangePointer(
                    &active,
                    nullptr,
                    nullptr));
        if (overlay == nullptr || overlay->originalFire == nullptr)
        {
            InterlockedDecrement(&callbackEntrants);
            return;
        }
        overlay->Dispatch(
            weapon,
            actor,
            matrix,
            barrelIndex,
            _ReturnAddress());
        InterlockedDecrement(&callbackEntrants);
    }

    void Dispatch(
        void* weapon,
        void* actor,
        const bfvr::stereo::Matrix4* matrix,
        DWORD barrelIndex,
        const void* callerReturn) noexcept
    {
        InterlockedIncrement(&observedCalls);
        bfvr::stereo::Matrix4 nativeMatrix = {};
        bool readable = false;
        if (matrix != nullptr)
        {
            __try
            {
                std::memcpy(&nativeMatrix, matrix, sizeof(nativeMatrix));
                readable = true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                readable = false;
            }
        }

        if (!IsExpectedCaller(callerReturn))
        {
            InterlockedIncrement(&wrongCallerCalls);
            originalFire(weapon, actor, matrix, barrelIndex);
            return;
        }
        if (!IsLocalAliveActor(actor))
        {
            InterlockedIncrement(&nonLocalOrDeadCalls);
            originalFire(weapon, actor, matrix, barrelIndex);
            return;
        }
        if (!readable)
        {
            InterlockedIncrement(&unreadableMatrices);
            originalFire(weapon, actor, matrix, barrelIndex);
            return;
        }

        bfvr::D3D8RuntimeControllerSample sample = {};
        bfvr::D3D8RuntimeView matchingHead = {};
        LONG controllerGeneration = 0;
        if (!bfvr::ReadFreshAcceptedWeaponTracking(
                sample,
                matchingHead,
                controllerGeneration,
                kControllerSampleMaximumAgeMs))
        {
            InterlockedIncrement(&staleTracking);
            originalFire(weapon, actor, matrix, barrelIndex);
            return;
        }
        const auto& right = sample.hands[kControllerHandRight];
        constexpr DWORD kRequiredGripFlags =
            bfvr::shared::kControllerHandFlagGripActive |
            bfvr::shared::kControllerHandFlagGripPositionValid |
            bfvr::shared::kControllerHandFlagGripOrientationValid |
            bfvr::shared::kControllerHandFlagGripPositionTracked |
            bfvr::shared::kControllerHandFlagGripOrientationTracked;
        if ((right.flags & kRequiredGripFlags) != kRequiredGripFlags)
        {
            InterlockedIncrement(&invalidGripTracking);
            originalFire(weapon, actor, matrix, barrelIndex);
            return;
        }

        bfvr::stereo::Matrix4 visualWeaponWorldAttachment = {};
        LONG visualControllerGeneration = 0;
        if (!bfvr::ReadFreshWeaponWorldAttachment(
                visualWeaponWorldAttachment,
                visualControllerGeneration,
                kControllerSampleMaximumAgeMs))
        {
            InterlockedIncrement(&missingVisualWeaponPose);
            originalFire(weapon, actor, matrix, barrelIndex);
            return;
        }
        if (visualControllerGeneration != controllerGeneration)
        {
            InterlockedIncrement(&visualGenerationMismatches);
            originalFire(weapon, actor, matrix, barrelIndex);
            return;
        }

        const auto adjusted =
            bfvr::stereo::MakeD3D8WorldAttachedWeaponFireMatrix(
                nativeMatrix,
                visualWeaponWorldAttachment);
        if (!adjusted.has_value())
        {
            InterlockedIncrement(&mathRejections);
            originalFire(weapon, actor, matrix, barrelIndex);
            return;
        }

        Record(
            nativeMatrix,
            *adjusted,
            barrelIndex,
            controllerGeneration,
            visualControllerGeneration);
        InterlockedIncrement(&adjustedCalls);
        originalFire(weapon, actor, &*adjusted, barrelIndex);
    }

    bool IsExpectedCaller(const void* callerReturn) const noexcept
    {
        if (gameImage == nullptr || callerReturn == nullptr)
        {
            return false;
        }
        for (const std::ptrdiff_t rva : kExpectedCallerReturnRvas)
        {
            if (callerReturn == gameImage + rva)
            {
                return true;
            }
        }
        return false;
    }

    bool IsLocalAliveActor(void* actor) const noexcept
    {
        if (gameImage == nullptr || actor == nullptr)
        {
            return false;
        }
        __try
        {
            void* const manager = *reinterpret_cast<void* const*>(
                gameImage + kPlayerManagerGlobalRva);
            void* const localPlayer = manager == nullptr
                ? nullptr
                : *reinterpret_cast<void* const*>(
                    static_cast<const std::byte*>(manager) +
                    kPlayerManagerLocalPlayerOffset);
            return actor == localPlayer &&
                std::to_integer<BYTE>(
                    static_cast<const std::byte*>(localPlayer)
                        [kBFPlayerIsAliveOffset]) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void Record(
        const bfvr::stereo::Matrix4& nativeMatrix,
        const bfvr::stereo::Matrix4& adjustedMatrix,
        DWORD barrelIndex,
        LONG trackingGeneration,
        LONG visualGeneration) noexcept
    {
        const LONG sequence = InterlockedIncrement(&nextRecordSequence);
        if (sequence > static_cast<LONG>(records.size()))
        {
            return;
        }
        AimRecord& record = records[static_cast<std::size_t>(sequence - 1)];
        record.trackingGeneration = trackingGeneration;
        record.visualGeneration = visualGeneration;
        record.barrelIndex = barrelIndex;
        for (std::size_t component = 0; component < 3; ++component)
        {
            record.nativeForward[component] =
                nativeMatrix.values[2][component];
            record.adjustedForward[component] =
                adjustedMatrix.values[2][component];
            record.nativePosition[component] =
                nativeMatrix.values[3][component];
        }
        MemoryBarrier();
        InterlockedExchange(&record.sequence, sequence);
    }

    void Report() const
    {
        WriteLog(
            L"Controller-directed fire overlay stopped: observed=%ld adjusted=%ld wrongCaller=%ld nonLocalOrDead=%ld unreadable=%ld missingVisualWeaponPose=%ld visualGenerationMismatch=%ld staleTracking=%ld invalidGripTracking=%ld mathRejected=%ld.",
            observedCalls,
            adjustedCalls,
            wrongCallerCalls,
            nonLocalOrDeadCalls,
            unreadableMatrices,
            missingVisualWeaponPose,
            visualGenerationMismatches,
            staleTracking,
            invalidGripTracking,
            mathRejections);
        const LONG maximum = std::min(
            InterlockedCompareExchange(
                const_cast<volatile LONG*>(&nextRecordSequence),
                0,
                0),
            static_cast<LONG>(records.size()));
        for (LONG index = 0; index < maximum; ++index)
        {
            const AimRecord& record =
                records[static_cast<std::size_t>(index)];
            if (InterlockedCompareExchange(
                    const_cast<volatile LONG*>(&record.sequence),
                    0,
                    0) != index + 1)
            {
                continue;
            }
            WriteLog(
                L"Controller-directed fire sample seq=%ld trackingGeneration=%ld visualGeneration=%ld barrel=%lu position=(%.4f,%.4f,%.4f) nativeForward=(%.5f,%.5f,%.5f) adjustedForward=(%.5f,%.5f,%.5f).",
                record.sequence,
                record.trackingGeneration,
                record.visualGeneration,
                record.barrelIndex,
                record.nativePosition[0],
                record.nativePosition[1],
                record.nativePosition[2],
                record.nativeForward[0],
                record.nativeForward[1],
                record.nativeForward[2],
                record.adjustedForward[0],
                record.adjustedForward[1],
                record.adjustedForward[2]);
        }
    }

    void RemoveHook()
    {
        InterlockedCompareExchangePointer(&active, nullptr, this);
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
        originalFire = nullptr;
        if (ownsMinHook)
        {
            MH_Uninitialize();
            ownsMinHook = false;
        }
        fireTarget = nullptr;
        gameImage = nullptr;
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

    static PVOID volatile active;
    static volatile LONG callbackEntrants;

    std::byte* gameImage = nullptr;
    void* fireTarget = nullptr;
    FireFn originalFire = nullptr;
    void (*appendLog)(const wchar_t* message) = nullptr;
    std::array<AimRecord, kRecordCapacity> records = {};
    volatile LONG started = 0;
    volatile LONG observedCalls = 0;
    volatile LONG adjustedCalls = 0;
    volatile LONG wrongCallerCalls = 0;
    volatile LONG nonLocalOrDeadCalls = 0;
    volatile LONG unreadableMatrices = 0;
    volatile LONG missingVisualWeaponPose = 0;
    volatile LONG visualGenerationMismatches = 0;
    volatile LONG staleTracking = 0;
    volatile LONG invalidGripTracking = 0;
    volatile LONG mathRejections = 0;
    volatile LONG nextRecordSequence = 0;
    bool ownsMinHook = false;
    bool hookCreated = false;
    bool hookEnabled = false;
};

PVOID volatile WeaponAimOverlay::active = nullptr;
volatile LONG WeaponAimOverlay::callbackEntrants = 0;
WeaponAimOverlay g_overlay = {};

} // namespace

namespace bfvr
{

void StartWeaponAimOverlay(
    void* gameImage,
    void (*appendLog)(const wchar_t* message))
{
    g_overlay.Start(gameImage, appendLog);
}

void StopWeaponAimOverlay()
{
    g_overlay.Stop();
}

} // namespace bfvr
