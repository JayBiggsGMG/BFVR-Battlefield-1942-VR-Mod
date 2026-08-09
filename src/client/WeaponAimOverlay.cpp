#include "client/WeaponAimOverlay.h"

#include "client/BFSoldierVrMotionFilter.h"
#include "client/ControllerHaptics.h"
#include "client/HandWeaponRecoilRuntime.h"
#include "client/MountedWeaponAimResolver.h"
#include "client/ScopedOffHandSupportPoseCache.h"
#include "client/ScopeViewOverlay.h"
#include "client/WeaponPoseRuntimeCache.h"
#include "stereo/ScopeViewMath.h"
#include "stereo/WeaponFireAimMath.h"
#include "stereo/WorldCrosshairMath.h"

#include <MinHook.h>

#include <windows.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <intrin.h>

namespace
{

constexpr wchar_t kEnableWeaponMotionEnvironment[] =
    L"BFVR_ENABLE_WEAPON_MOTION";
constexpr wchar_t kEnableNativeArmIkEnvironment[] =
    L"BFVR_ENABLE_NATIVE_1P_ARMS_IK";
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
constexpr std::size_t kSoldierActiveItemIndexOffset = 0x3E8;
constexpr DWORD kVisualWeaponPoseMaximumAgeMs = 125;
constexpr DWORD kOffHandSupportMaximumAgeMs = 150;
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
    float adjustedPosition[3] = {};
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

bool ReadActiveItemIndex(
    const void* soldier,
    int& activeItemIndex) noexcept
{
    activeItemIndex = -1;
    if (soldier == nullptr)
    {
        return false;
    }
    __try
    {
        activeItemIndex = *reinterpret_cast<const int*>(
            static_cast<const std::byte*>(soldier) +
            kSoldierActiveItemIndexOffset);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        activeItemIndex = -1;
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
        weaponMotionEnabled =
            GetEnvironmentVariableW(
                kEnableWeaponMotionEnvironment,
                enabled,
                static_cast<DWORD>(std::size(enabled))) == 1 &&
            enabled[0] == L'1';
        wchar_t nativeArmIkEnabled[2] = {};
        moveNativeFireOrigin = weaponMotionEnabled &&
            GetEnvironmentVariableW(
                kEnableNativeArmIkEnvironment,
                nativeArmIkEnabled,
                static_cast<DWORD>(std::size(nativeArmIkEnabled))) == 1 &&
            nativeArmIkEnabled[0] == L'1';

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
            !weaponMotionEnabled
                ? L"Local WeaponFire_Core haptic observer armed at 0x0053CDB0. Weapon motion is disabled, so every call forwards unchanged after publishing only accepted local firing feedback."
                : moveNativeFireOrigin
                ? L"Controller-directed fire overlay armed at 0x0053CDB0 for native 1P arms. Exact current gadget slots 4/5/6 use the raw OpenXR aim-pointer origin and direction without changing the held item or hand; knives and ordinary shooting items retain the established held-gun basis. During an exact validated useScope view whose hidden native arm is stale, WeaponFire_Core instead shares the visible scoped basis while retaining BF1942's native projectile origin. BF1942 retains weapon/barrel offsets, spread, cadence, projectile creation, and networking; unsupported calls forward unchanged."
                : L"Controller-directed fire overlay armed at 0x0053CDB0. Only the five verified branches in WeaponFire_Ordinary can receive the fresh displayed-weapon rotation for the alive local infantry player; native fire position and unsupported calls remain unchanged.");
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
        const bool localAliveActor = IsLocalAliveActor(actor);
        if (localAliveActor)
        {
            // Haptics cover every accepted local weapon. Infantry camera and
            // handweapon recoil lifetimes are narrower: a mounted/vehicle
            // WeaponFire_Core call must never survive its independently timed
            // tracking-context handoff into the returned soldier view.
            bfvr::NotifyControllerWeaponFired();
            const void* const cameraSoldier =
                bfvr::ReadCurrentBFSoldierVrCameraSoldier();
            bfvr::LocalPlayerControlContext controlContext = {};
            const bool exactInfantryControl =
                bfvr::ReadLocalPlayerControlContext(controlContext) &&
                controlContext.currentControlObject ==
                    controlContext.defaultControlObject &&
                controlContext.defaultControlObject == cameraSoldier;
            if (exactInfantryControl)
            {
                // WeaponFire_Core is BF1942's accepted handweapon boundary.
                // Publishing here preserves native cadence for semi-auto,
                // automatic, and multi-barrel infantry weapons.
                bfvr::NotifyBFSoldierVrLocalWeaponFired();
                bfvr::NotifyHandWeaponRecoilShot(
                    cameraSoldier,
                    weapon,
                    bfvr::IsFreshCurrentOffHandSupportHeld(
                        kOffHandSupportMaximumAgeMs));
            }
        }
        if (!weaponMotionEnabled)
        {
            originalFire(weapon, actor, matrix, barrelIndex);
            return;
        }
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
            if (localAliveActor &&
                InterlockedIncrement(&loggedLocalFallbacks) <= 8)
            {
                WriteLog(
                    L"Local WeaponFire_Core call forwarded unchanged because its return address %p is outside the five profiled ordinary-infantry branches.",
                    callerReturn);
            }
            originalFire(weapon, actor, matrix, barrelIndex);
            return;
        }
        if (!localAliveActor)
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

        bfvr::stereo::Matrix4 visualWeaponWorldAttachment = {};
        bfvr::stereo::Matrix4 controllerGunWorld = {};
        LONG visualControllerGeneration = 0;
        float nativeFireToHandDistance = 0.0F;
        float solvedHandDisplacement = 0.0F;
        float nativeFireToHandLimit = 0.0F;
        int selectedItemIndex = -1;
        bool exactCurrentActiveItemReceiver = false;
        bool gadgetPointerAim = false;
        bool scopedDirectionOnly = false;
        if (moveNativeFireOrigin)
        {
            bfvr::NativeArmWeaponVisualPose nativeArmPose = {};
            if (!bfvr::ReadFreshNativeArmWeaponVisualPose(
                    nativeArmPose,
                    kVisualWeaponPoseMaximumAgeMs))
            {
                bfvr::ScopeViewFrameState scopeFrame = {};
                const bool scopeFrameAvailable =
                    bfvr::ReadScopeViewFrameState(scopeFrame);
                const void* const currentSoldier =
                    bfvr::ReadCurrentBFSoldierVrCameraSoldier();
                if (!bfvr::stereo::IsExactScopeFirePoseEligible(
                        scopeFrameAvailable,
                        weapon,
                        scopeFrame.weapon,
                        currentSoldier,
                        scopeFrame.soldier))
                {
                    InterlockedIncrement(&missingNativeArmPose);
                    LogLocalFallback(
                        L"Local WeaponFire_Core call forwarded unchanged because neither a fresh native-arm anchor pair nor an exact current scoped-fire pose was available.");
                    originalFire(weapon, actor, matrix, barrelIndex);
                    return;
                }
                controllerGunWorld = scopeFrame.controllerGunWorld;
                visualControllerGeneration =
                    static_cast<LONG>(scopeFrame.controllerGeneration);
                scopedDirectionOnly = true;
                // Scoped aiming keeps fresh two-controller support after
                // BF1942 hides the native arm callback. Correct the initial
                // ordinary support-cache latch before calcRecoil publishes
                // this shot's first native sample.
                bfvr::NotifyHandWeaponRecoilShot(
                    currentSoldier,
                    weapon,
                    scopeFrame.offHandSupported);
            }
            else if (nativeArmPose.soldier !=
                     bfvr::ReadCurrentBFSoldierVrCameraSoldier())
            {
                InterlockedIncrement(&cameraLifetimeMismatch);
                LogLocalFallback(
                    L"Local WeaponFire_Core call forwarded unchanged because the displayed hand and current camera-soldier lifetimes differ.");
                originalFire(weapon, actor, matrix, barrelIndex);
                return;
            }
            else
            {
                const auto anchorDistances =
                    bfvr::stereo::MeasureD3D8NativeArmFireAnchorDistances(
                        nativeMatrix,
                        nativeArmPose.nativeHandWorld,
                        nativeArmPose.targetHandWorld);
                if (!anchorDistances.has_value())
                {
                    InterlockedIncrement(&anchorDistanceRejected);
                    LogLocalFallback(
                        L"Local WeaponFire_Core call forwarded unchanged because its native fire/hand anchors were not finite rigid transforms.");
                    originalFire(weapon, actor, matrix, barrelIndex);
                    return;
                }
                nativeFireToHandDistance =
                    anchorDistances->nativeFireToHand;
                solvedHandDisplacement =
                    anchorDistances->solvedHandDisplacement;
                exactCurrentActiveItemReceiver =
                    nativeArmPose.activeItem != nullptr &&
                    nativeArmPose.activeItem == weapon;
                const bool selectedItemReadable = ReadActiveItemIndex(
                    nativeArmPose.soldier,
                    selectedItemIndex);
                nativeFireToHandLimit =
                    bfvr::stereo::SelectD3D8NativeArmFireToHandLimit(
                        exactCurrentActiveItemReceiver);
                if (!bfvr::stereo::IsD3D8NativeArmFireAnchorWithinPolicy(
                        *anchorDistances,
                        exactCurrentActiveItemReceiver))
                {
                    InterlockedIncrement(&anchorDistanceRejected);
                    if (InterlockedIncrement(&loggedLocalFallbacks) <= 8)
                    {
                        WriteLog(
                            L"Local WeaponFire_Core call forwarded unchanged because its native origin is not associated with the solved hand (fireToHand=%.3f m, handDisplacement=%.3f m, limits=%.3f/1.500 m, exactActiveItem=%d weapon=%p activeItem=%p). A cinematic/death/match-start camera matrix is not eligible for the native-arm attachment.",
                            nativeFireToHandDistance,
                            solvedHandDisplacement,
                            nativeFireToHandLimit,
                            exactCurrentActiveItemReceiver ? 1 : 0,
                            weapon,
                            nativeArmPose.activeItem);
                    }
                    originalFire(weapon, actor, matrix, barrelIndex);
                    return;
                }
                const bool alignmentWarmup =
                    nativeArmPose.activeItem == nullptr;
                gadgetPointerAim = selectedItemReadable &&
                    bfvr::stereo::IsWorldCrosshairGadgetItemIndex(
                        selectedItemIndex) &&
                    (exactCurrentActiveItemReceiver || alignmentWarmup);
                controllerGunWorld = gadgetPointerAim
                    ? nativeArmPose.controllerAimPointerWorld
                    : nativeArmPose.controllerGunWorld;
                visualControllerGeneration =
                    nativeArmPose.controllerGeneration;
            }
        }
        else if (!bfvr::ReadFreshWeaponWorldAttachment(
                     visualWeaponWorldAttachment,
                     visualControllerGeneration,
                     kVisualWeaponPoseMaximumAgeMs))
        {
            InterlockedIncrement(&missingVisualWeaponPose);
            LogLocalFallback(
                L"Local WeaponFire_Core call forwarded unchanged because no fresh displayed-weapon attachment was available.");
            originalFire(weapon, actor, matrix, barrelIndex);
            return;
        }

        // Full-screen native scopes keep their existing raw controller camera
        // basis. Only the WeaponFire matrix receives the current weapon recoil
        // offset, so scoped recoil can never rotate either VR eye.
        if (scopedDirectionOnly)
        {
            const auto recoiledScopeFire =
                bfvr::MakeCurrentHandWeaponRecoilPose(
                    controllerGunWorld,
                    bfvr::ReadCurrentBFSoldierVrCameraSoldier(),
                    weapon);
            if (recoiledScopeFire.has_value())
            {
                controllerGunWorld = *recoiledScopeFire;
            }
        }

        const auto adjusted = moveNativeFireOrigin
            ? bfvr::stereo::MakeD3D8ControllerDirectedWeaponFireMatrix(
                nativeMatrix,
                controllerGunWorld,
                !scopedDirectionOnly)
            : bfvr::stereo::MakeD3D8WorldAttachedWeaponFireMatrix(
                nativeMatrix,
                visualWeaponWorldAttachment,
                false);
        if (!adjusted.has_value())
        {
            InterlockedIncrement(&mathRejections);
            if (InterlockedIncrement(&loggedLocalFallbacks) <= 8)
            {
                WriteLog(
                    moveNativeFireOrigin
                        ? L"Local WeaponFire_Core call forwarded unchanged because the direct controller gun pose could not replace the native fire basis."
                        : L"Local WeaponFire_Core call forwarded unchanged because the displayed-weapon attachment could not be composed with the native fire matrix.");
            }
            originalFire(weapon, actor, matrix, barrelIndex);
            return;
        }

        if (scopedDirectionOnly)
        {
            InterlockedIncrement(&scopedDirectionOnlyCalls);
        }
        if (gadgetPointerAim)
        {
            InterlockedIncrement(&gadgetPointerAimCalls);
        }

        Record(
            nativeMatrix,
            *adjusted,
            barrelIndex,
            visualControllerGeneration,
            visualControllerGeneration);
        if (InterlockedIncrement(&loggedAdjustedSamples) <= 8)
        {
            if (scopedDirectionOnly)
            {
                WriteLog(
                    L"Controller-directed scoped fire used the exact visible scope gun basis while preserving BF1942's native projectile origin: generation=%ld weapon=%p nativeOrigin=(%.3f,%.3f,%.3f) scopedGunOrigin=(%.3f,%.3f,%.3f) nativeForward=(%.5f,%.5f,%.5f) scopedForward=(%.5f,%.5f,%.5f).",
                    visualControllerGeneration,
                    weapon,
                    nativeMatrix.values[3][0],
                    nativeMatrix.values[3][1],
                    nativeMatrix.values[3][2],
                    controllerGunWorld.values[3][0],
                    controllerGunWorld.values[3][1],
                    controllerGunWorld.values[3][2],
                    nativeMatrix.values[2][0],
                    nativeMatrix.values[2][1],
                    nativeMatrix.values[2][2],
                    adjusted->values[2][0],
                    adjusted->values[2][1],
                    adjusted->values[2][2]);
            }
            else
            {
                WriteLog(
                    L"Controller-directed fire applied direct OpenXR aim: generation=%ld fireToHand=%.3f m handDisplacement=%.3f m nativeLimit=%.3f m exactActiveItem=%d selectedItemIndex=%d gadgetPointerAim=%d nativeOrigin=(%.3f,%.3f,%.3f) aimOrigin=(%.3f,%.3f,%.3f) nativeForward=(%.5f,%.5f,%.5f) aimForward=(%.5f,%.5f,%.5f).",
                    visualControllerGeneration,
                    nativeFireToHandDistance,
                    solvedHandDisplacement,
                    nativeFireToHandLimit,
                    exactCurrentActiveItemReceiver ? 1 : 0,
                    selectedItemIndex,
                    gadgetPointerAim ? 1 : 0,
                    nativeMatrix.values[3][0],
                    nativeMatrix.values[3][1],
                    nativeMatrix.values[3][2],
                    adjusted->values[3][0],
                    adjusted->values[3][1],
                    adjusted->values[3][2],
                    nativeMatrix.values[2][0],
                    nativeMatrix.values[2][1],
                    nativeMatrix.values[2][2],
                    adjusted->values[2][0],
                    adjusted->values[2][1],
                    adjusted->values[2][2]);
            }
        }
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

    void LogLocalFallback(const wchar_t* message) noexcept
    {
        if (InterlockedIncrement(&loggedLocalFallbacks) <= 8)
        {
            WriteLog(L"%s", message);
        }
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
            record.adjustedPosition[component] =
                adjustedMatrix.values[3][component];
        }
        MemoryBarrier();
        InterlockedExchange(&record.sequence, sequence);
    }

    void Report() const
    {
        WriteLog(
            L"Controller-directed fire overlay stopped: observed=%ld adjusted=%ld scopedDirectionOnly=%ld gadgetPointerAim=%ld wrongCaller=%ld nonLocalOrDead=%ld unreadable=%ld missingVisualWeaponPose=%ld missingNativeArmPose=%ld cameraLifetimeMismatch=%ld anchorDistanceRejected=%ld mathRejected=%ld.",
            observedCalls,
            adjustedCalls,
            scopedDirectionOnlyCalls,
            gadgetPointerAimCalls,
            wrongCallerCalls,
            nonLocalOrDeadCalls,
            unreadableMatrices,
            missingVisualWeaponPose,
            missingNativeArmPose,
            cameraLifetimeMismatch,
            anchorDistanceRejected,
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
                L"Controller-directed fire sample seq=%ld trackingGeneration=%ld visualGeneration=%ld barrel=%lu nativePosition=(%.4f,%.4f,%.4f) adjustedPosition=(%.4f,%.4f,%.4f) nativeForward=(%.5f,%.5f,%.5f) adjustedForward=(%.5f,%.5f,%.5f).",
                record.sequence,
                record.trackingGeneration,
                record.visualGeneration,
                record.barrelIndex,
                record.nativePosition[0],
                record.nativePosition[1],
                record.nativePosition[2],
                record.adjustedPosition[0],
                record.adjustedPosition[1],
                record.adjustedPosition[2],
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
        moveNativeFireOrigin = false;
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
    volatile LONG scopedDirectionOnlyCalls = 0;
    volatile LONG gadgetPointerAimCalls = 0;
    volatile LONG wrongCallerCalls = 0;
    volatile LONG nonLocalOrDeadCalls = 0;
    volatile LONG unreadableMatrices = 0;
    volatile LONG missingVisualWeaponPose = 0;
    volatile LONG missingNativeArmPose = 0;
    volatile LONG cameraLifetimeMismatch = 0;
    volatile LONG anchorDistanceRejected = 0;
    volatile LONG loggedLocalFallbacks = 0;
    volatile LONG loggedAdjustedSamples = 0;
    volatile LONG mathRejections = 0;
    volatile LONG nextRecordSequence = 0;
    bool ownsMinHook = false;
    bool hookCreated = false;
    bool hookEnabled = false;
    bool moveNativeFireOrigin = false;
    bool weaponMotionEnabled = false;
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
