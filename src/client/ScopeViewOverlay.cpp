#include "client/ScopeViewOverlay.h"

#include "client/BFSoldierVrMotionFilter.h"
#include "client/BFSoldierOffHandWeaponSteering.h"
#include "client/ScopedOffHandSupportPoseCache.h"
#include "client/TrackedScopeAim.h"
#include "client/WeaponPoseRuntimeCache.h"
#include "stereo/OffHandWeaponSteeringMath.h"
#include "stereo/ScopeViewMath.h"

#include <MinHook.h>

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <optional>

namespace
{
constexpr std::ptrdiff_t kFireArmsSetZoomRva = 0x001391B0;
constexpr std::size_t kFireArmsTemplateOffset = 0x4C;
constexpr std::size_t kFireArmsNormalFovOffset = 0x1F4;
constexpr std::size_t kFireArmsTemplateUseScopeOffset = 0x3D8;
constexpr std::size_t kFireArmsTemplateZoomFovOffset = 0x3DC;
constexpr DWORD kNativeArmPoseMaximumAgeMs = 125;
constexpr float kBf1942WorldUnitsPerMetre = 1.0F;
constexpr float kMaximumPrimarySupportSwingRadians =
    35.0F * 3.14159265358979323846F / 180.0F;
constexpr std::array<BYTE, 30> kExpectedPrefix = {
    0x55, 0x56, 0x8B, 0xF1, 0x8B, 0x6E, 0x4C, 0xD9,
    0x85, 0xDC, 0x03, 0x00, 0x00, 0xD8, 0x1D, 0xAC,
    0x41, 0x8C, 0x00, 0xDF, 0xE0, 0xF6, 0xC4, 0x05,
    0x0F, 0x8B, 0x3D, 0x02, 0x00, 0x00};

struct ScopeProperties
{
    bool useScope = false;
    float normalFov = -1.0F;
    float zoomFov = -1.0F;
};

struct CachedScopeAim
{
    bfvr::stereo::Matrix4 controllerGunWorld = {};
    bfvr::stereo::Matrix4 trackedAimCorrection = {};
    bfvr::stereo::Matrix4 offHandSupportFromGun = {};
    const void* soldier = nullptr;
    LONG controllerGeneration = 0;
    bool trackedAimCorrectionValid = false;
    bool offHandSupportValid = false;
};

bool ReadScopeProperties(
    const void* weapon,
    ScopeProperties& properties) noexcept
{
    properties = {};
    if (weapon == nullptr)
    {
        return false;
    }
    __try
    {
        const auto* const weaponBytes =
            static_cast<const std::byte*>(weapon);
        const auto* const weaponTemplate =
            *reinterpret_cast<const std::byte* const*>(
                weaponBytes + kFireArmsTemplateOffset);
        if (weaponTemplate == nullptr)
        {
            return false;
        }
        properties.useScope =
            std::to_integer<BYTE>(
                weaponTemplate[kFireArmsTemplateUseScopeOffset]) != 0;
        properties.normalFov = *reinterpret_cast<const float*>(
            weaponBytes + kFireArmsNormalFovOffset);
        properties.zoomFov = *reinterpret_cast<const float*>(
            weaponTemplate + kFireArmsTemplateZoomFovOffset);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        properties = {};
        return false;
    }
}

class ScopeViewOverlay
{
public:
    using SetZoomFn = void(__thiscall*)(void* weapon, BOOL enabled);

    void Start(
        void* gameImage,
        void (*log)(const wchar_t* message))
    {
        if (InterlockedCompareExchange(&started_, 1, 0) != 0)
        {
            return;
        }
        appendLog_ = log;
        target_ = gameImage == nullptr
            ? nullptr
            : static_cast<std::byte*>(gameImage) + kFireArmsSetZoomRva;
        if (!HasExpectedPrefix())
        {
            WriteLog(
                L"Scoped-view activation rejected target %p: the profiled WinPC FireArms::setZoom bytes differ.",
                target_);
            InterlockedExchange(&started_, 0);
            return;
        }

        const MH_STATUS initializeStatus = MH_Initialize();
        if (initializeStatus == MH_OK)
        {
            ownsMinHook_ = true;
        }
        else if (initializeStatus != MH_ERROR_ALREADY_INITIALIZED)
        {
            WriteLog(
                L"Scoped-view activation could not initialize MinHook (status=%d).",
                static_cast<int>(initializeStatus));
            InterlockedExchange(&started_, 0);
            return;
        }

        const MH_STATUS createStatus = MH_CreateHook(
            target_,
            reinterpret_cast<LPVOID>(&ScopeViewOverlay::Hook),
            reinterpret_cast<LPVOID*>(&original_));
        if (createStatus != MH_OK || original_ == nullptr)
        {
            WriteLog(
                L"Scoped-view activation could not create its FireArms hook (status=%d).",
                static_cast<int>(createStatus));
            RemoveHook();
            InterlockedExchange(&started_, 0);
            return;
        }
        hookCreated_ = true;
        InterlockedExchangePointer(&active_, this);
        const MH_STATUS enableStatus = MH_EnableHook(target_);
        if (enableStatus != MH_OK)
        {
            WriteLog(
                L"Scoped-view activation could not enable its FireArms hook (status=%d).",
                static_cast<int>(enableStatus));
            RemoveHook();
            InterlockedExchange(&started_, 0);
            return;
        }
        hookEnabled_ = true;
        WriteLog(
            L"Weapon-directed stereo scope policy armed at 0x005391B0. Only useScope-enabled exact local active items are eligible; ordinary zoom-only secondary fire remains unchanged.");
    }

    void Stop()
    {
        if (InterlockedCompareExchange(&started_, 0, 0) == 0)
        {
            return;
        }
        if (hookEnabled_)
        {
            MH_DisableHook(target_);
            hookEnabled_ = false;
        }
        InterlockedCompareExchangePointer(&active_, nullptr, this);
        while (InterlockedCompareExchange(&callbackEntrants_, 0, 0) != 0)
        {
            Sleep(0);
        }
        ClearAll();
        WriteLog(
            L"Scoped-view activation stopped: zoomCalls=%ld activations=%ld deactivations=%ld nonScopeZoomsIgnored=%ld localReceiverRejects=%ld freshAimUpdates=%ld trackedAimUpdates=%ld trackedOffHandSteeringUpdates=%ld latchedAimFallbacks=%ld trackedAimCorrectionFailures=%ld.",
            InterlockedCompareExchange(&zoomCalls_, 0, 0),
            InterlockedCompareExchange(&activations_, 0, 0),
            InterlockedCompareExchange(&deactivations_, 0, 0),
            InterlockedCompareExchange(&ignoredNonScopeZooms_, 0, 0),
            InterlockedCompareExchange(&localReceiverRejects_, 0, 0),
            InterlockedCompareExchange(&freshAimUpdates_, 0, 0),
            InterlockedCompareExchange(&trackedAimUpdates_, 0, 0),
            InterlockedCompareExchange(
                &trackedOffHandSteeringUpdates_,
                0,
                0),
            InterlockedCompareExchange(&latchedAimFallbacks_, 0, 0),
            InterlockedCompareExchange(
                &trackedAimCorrectionFailures_,
                0,
                0));
        RemoveHook();
        InterlockedExchange(&started_, 0);
    }

    bool ReadFrameState(bfvr::ScopeViewFrameState& state) noexcept
    {
        state = {};
        void* const requested = requestedWeapon_.load(
            std::memory_order_acquire);
        if (requested == nullptr ||
            InterlockedCompareExchange(&started_, 0, 0) == 0)
        {
            return false;
        }

        CachedScopeAim cachedAim = {};
        const bool hasCachedAim = ReadCachedAim(
            requested,
            cachedAim);
        bfvr::stereo::Matrix4 controllerGunWorld =
            cachedAim.controllerGunWorld;
        const void* cachedSoldier = cachedAim.soldier;
        LONG controllerGeneration = cachedAim.controllerGeneration;
        bfvr::NativeArmWeaponVisualPose nativePose = {};
        const bool freshPose = bfvr::ReadFreshNativeArmWeaponVisualPose(
            nativePose,
            kNativeArmPoseMaximumAgeMs);
        const void* const currentSoldier =
            bfvr::ReadCurrentBFSoldierVrCameraSoldier();
        if (currentSoldier != nullptr && cachedSoldier != nullptr &&
            currentSoldier != cachedSoldier)
        {
            ClearIfRequested(requested);
            return false;
        }

        bfvr::TrackedScopeAimSample trackedPose = {};
        const bool rawTrackedPoseAvailable = hasCachedAim &&
            currentSoldier != nullptr && cachedSoldier == currentSoldier &&
            bfvr::ReadFreshTrackedScopeAim(
                cachedSoldier,
                trackedPose,
                kNativeArmPoseMaximumAgeMs);
        const auto correctedTrackedPose =
            rawTrackedPoseAvailable &&
                cachedAim.trackedAimCorrectionValid
            ? bfvr::stereo::ApplyD3D8ScopeAimCorrection(
                  cachedAim.trackedAimCorrection,
                  trackedPose.controllerGunWorld)
            : std::optional<bfvr::stereo::Matrix4>{};
        auto continuousTrackedPose = correctedTrackedPose;
        bool trackedOffHandApplied = false;
        if (continuousTrackedPose.has_value() &&
            cachedAim.offHandSupportValid)
        {
            bfvr::stereo::Matrix4 oneHandGunWorld =
                *continuousTrackedPose;
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                oneHandGunWorld.values[3][axis] =
                    trackedPose.controllerGunWorld.values[3][axis];
            }
            const auto predictedSupportWorld =
                bfvr::stereo::ApplyD3D8ScopeAimCorrection(
                    cachedAim.offHandSupportFromGun,
                    oneHandGunWorld);
            if (predictedSupportWorld.has_value() &&
                bfvr::stereo::IsD3D8ScopeOffHandSupportHeld(
                    true,
                    trackedPose.sessionFocused,
                    trackedPose.leftGripTracked,
                    trackedPose.leftSqueezeActive,
                    trackedPose.leftSqueezeValue,
                    *predictedSupportWorld,
                    trackedPose.leftGripWorld,
                    kBf1942WorldUnitsPerMetre))
            {
                const auto steering =
                    bfvr::stereo::ComputeBoundedOffHandWeaponSteering(
                        oneHandGunWorld,
                        *predictedSupportWorld,
                        trackedPose.leftGripWorld,
                        kMaximumPrimarySupportSwingRadians,
                        kBf1942WorldUnitsPerMetre);
                if (steering.has_value())
                {
                    continuousTrackedPose = steering->gunWorld;
                    trackedOffHandApplied = true;
                }
            }
        }
        const bool requestedChanged =
            requestedWeapon_.load(std::memory_order_acquire) != requested;
        const bool freshPoseContradicts = freshPose &&
            (nativePose.soldier == nullptr ||
             (currentSoldier != nullptr &&
              nativePose.soldier != currentSoldier) ||
             (hasCachedAim && cachedSoldier != nullptr &&
              nativePose.soldier != cachedSoldier) ||
             (nativePose.activeItem != nullptr &&
              nativePose.activeItem != requested));
        const bool freshPoseMatches = freshPose &&
            !freshPoseContradicts && nativePose.activeItem == requested;
        const auto aimSource = bfvr::stereo::SelectScopeAimSource(
            !requestedChanged,
            freshPoseMatches,
            freshPoseContradicts,
            continuousTrackedPose.has_value(),
            hasCachedAim);
        if (aimSource == bfvr::stereo::ScopeAimSource::None)
        {
            if (freshPoseContradicts)
            {
                ClearIfRequested(requested);
            }
            return false;
        }

        if (aimSource == bfvr::stereo::ScopeAimSource::Fresh)
        {
            controllerGunWorld = nativePose.controllerGunWorld;
            cachedSoldier = nativePose.soldier;
            controllerGeneration = nativePose.controllerGeneration;
            const auto trackedAimCorrection =
                !cachedAim.trackedAimCorrectionValid &&
                    rawTrackedPoseAvailable &&
                    trackedPose.soldier == nativePose.soldier
                ? bfvr::stereo::MakeD3D8ScopeAimCorrection(
                      nativePose.controllerGunWorld,
                      trackedPose.controllerGunWorld)
                : std::optional<bfvr::stereo::Matrix4>{};
            if (!cachedAim.trackedAimCorrectionValid &&
                rawTrackedPoseAvailable &&
                !trackedAimCorrection.has_value())
            {
                InterlockedIncrement(&trackedAimCorrectionFailures_);
            }
            StoreCachedAim(
                requested,
                cachedSoldier,
                controllerGunWorld,
                controllerGeneration,
                trackedAimCorrection.has_value()
                    ? &*trackedAimCorrection
                    : nullptr,
                nullptr,
                false);
            InterlockedIncrement(&freshAimUpdates_);
        }
        else if (aimSource == bfvr::stereo::ScopeAimSource::Tracked)
        {
            controllerGunWorld = *continuousTrackedPose;
            controllerGeneration = trackedPose.controllerGeneration;
            StoreCachedAim(
                requested,
                cachedSoldier,
                controllerGunWorld,
                controllerGeneration,
                nullptr,
                nullptr,
                false);
            InterlockedIncrement(&trackedAimUpdates_);
            if (trackedOffHandApplied)
            {
                InterlockedIncrement(
                    &trackedOffHandSteeringUpdates_);
                if (InterlockedCompareExchange(
                        &firstTrackedOffHandSteeringLogged_,
                        1,
                        0) == 0)
                {
                    WriteLog(
                        L"Scoped view is preserving the established primary two-hand support binding with fresh left-grip fixed-pivot steering; native grip ownership is unchanged and this exact scoped gun basis is available to WeaponFire_Core.");
                }
            }
            if (InterlockedCompareExchange(
                    &firstTrackedAimLogged_,
                    1,
                    0) == 0)
            {
                WriteLog(
                    L"Scoped view is consuming fresh accepted right-controller aim independently of BF1942's hidden native-arm publisher; the exact scoped camera and WeaponFire_Core fallback share this gun basis.");
            }
        }
        else if (aimSource == bfvr::stereo::ScopeAimSource::Latched)
        {
            InterlockedIncrement(&latchedAimFallbacks_);
            if (InterlockedCompareExchange(
                    &firstLatchedAimFallbackLogged_,
                    1,
                    0) == 0)
            {
                WriteLog(
                    L"Scoped view retained its last valid gun direction through a transient native-arm pose-cache miss; native FireArms::setZoom remains authoritative for mode lifetime.");
            }
        }

        const float projectionScale = projectionScale_.load(
            std::memory_order_acquire);
        const float normalFov = normalFov_.load(
            std::memory_order_acquire);
        state.controllerGunWorld = controllerGunWorld;
        state.weapon = requested;
        state.soldier = cachedSoldier;
        state.controllerGeneration = controllerGeneration;
        state.normalFov = normalFov;
        state.projectionScale = projectionScale;
        return true;
    }

    bool IsActive() noexcept
    {
        return InterlockedCompareExchange(&started_, 0, 0) != 0 &&
            requestedWeapon_.load(std::memory_order_acquire) != nullptr;
    }

    float ProjectionScale() noexcept
    {
        return IsActive()
            ? projectionScale_.load(std::memory_order_acquire)
            : 1.0F;
    }

    void InvalidateFrame(const void* weapon) noexcept
    {
        ClearCachedAim(weapon);
    }

private:
    static void __fastcall Hook(
        void* weapon,
        void*,
        BOOL enabled)
    {
        InterlockedIncrement(&callbackEntrants_);
        ScopeViewOverlay* const overlay =
            static_cast<ScopeViewOverlay*>(
                InterlockedCompareExchangePointer(
                    &active_,
                    nullptr,
                    nullptr));
        if (overlay != nullptr && overlay->original_ != nullptr)
        {
            InterlockedIncrement(&overlay->zoomCalls_);
            overlay->original_(weapon, enabled);
            overlay->ObserveZoomResult(weapon, enabled != FALSE);
        }
        InterlockedDecrement(&callbackEntrants_);
    }

    void ObserveZoomResult(void* weapon, bool enabled) noexcept
    {
        if (!enabled)
        {
            if (ClearIfRequested(weapon))
            {
                InterlockedIncrement(&deactivations_);
                WriteLog(
                    L"Scoped view released for local weapon=%p; normal head-directed VR camera behavior restored.",
                    weapon);
            }
            return;
        }

        ScopeProperties properties = {};
        if (!ReadScopeProperties(weapon, properties))
        {
            ClearIfRequested(weapon);
            return;
        }
        if (!properties.useScope)
        {
            InterlockedIncrement(&ignoredNonScopeZooms_);
            ClearIfRequested(weapon);
            return;
        }

        const auto projectionScale =
            bfvr::stereo::ComputeD3D8ScopeProjectionScale(
                properties.normalFov,
                properties.zoomFov);
        bfvr::NativeArmWeaponVisualPose nativePose = {};
        if (!projectionScale.has_value() ||
            !bfvr::ReadFreshNativeArmWeaponVisualPose(
                nativePose,
                kNativeArmPoseMaximumAgeMs) ||
            nativePose.activeItem != weapon ||
            nativePose.soldier == nullptr ||
            nativePose.soldier !=
                bfvr::ReadCurrentBFSoldierVrCameraSoldier())
        {
            InterlockedIncrement(&localReceiverRejects_);
            ClearIfRequested(weapon);
            return;
        }

        const bool newlyActive = requestedWeapon_.load(
            std::memory_order_acquire) != weapon;
        bfvr::TrackedScopeAimSample trackedPose = {};
        const bool rawTrackedPoseAvailable =
            bfvr::ReadFreshTrackedScopeAim(
                nativePose.soldier,
                trackedPose,
                kNativeArmPoseMaximumAgeMs);
        bfvr::ScopedOffHandSupportPose supportPose = {};
        const bool supportPoseAvailable = rawTrackedPoseAvailable &&
            bfvr::ReadFreshScopedOffHandSupportPose(
                bfvr::MakeBFSoldierOffHandBindingId(
                    nativePose.soldier,
                    weapon),
                supportPose,
                kNativeArmPoseMaximumAgeMs) &&
            supportPose.supported;
        const auto offHandSupportFromGun = supportPoseAvailable
            ? bfvr::stereo::MakeD3D8ScopeAimCorrection(
                  supportPose.predictedSupportWorld,
                  supportPose.oneHandGunWorld)
            : std::optional<bfvr::stereo::Matrix4>{};
        const bool offHandSupportReady =
            offHandSupportFromGun.has_value();
        const auto trackedAimCorrection = rawTrackedPoseAvailable
            ? bfvr::stereo::MakeD3D8ScopeAimCorrection(
                  offHandSupportReady
                      ? supportPose.oneHandGunWorld
                      : nativePose.controllerGunWorld,
                  trackedPose.controllerGunWorld)
            : std::optional<bfvr::stereo::Matrix4>{};
        if (rawTrackedPoseAvailable && !trackedAimCorrection.has_value())
        {
            InterlockedIncrement(&trackedAimCorrectionFailures_);
        }
        normalFov_.store(properties.normalFov, std::memory_order_release);
        projectionScale_.store(*projectionScale, std::memory_order_release);
        StoreCachedAim(
            weapon,
            nativePose.soldier,
            nativePose.controllerGunWorld,
            nativePose.controllerGeneration,
            trackedAimCorrection.has_value()
                ? &*trackedAimCorrection
                : nullptr,
            offHandSupportReady
                ? &*offHandSupportFromGun
                : nullptr,
            true);
        requestedWeapon_.store(weapon, std::memory_order_release);
        if (newlyActive)
        {
            InterlockedIncrement(&activations_);
            WriteLog(
                L"Scoped view activated for exact local weapon=%p: normalFov=%.6f scopeFov=%.6f projectionScale=%.6f trackedAimCorrectionReady=%d trackedOffHandReady=%d; camera position remains head-based and direction follows the authoritative gun basis.",
                weapon,
                properties.normalFov,
                properties.zoomFov,
                *projectionScale,
                trackedAimCorrection.has_value() ? 1 : 0,
                offHandSupportReady ? 1 : 0);
        }
    }

    bool ClearIfRequested(const void* weapon) noexcept
    {
        void* expected = const_cast<void*>(weapon);
        if (!requestedWeapon_.compare_exchange_strong(
                expected,
                nullptr,
                std::memory_order_acq_rel))
        {
            return false;
        }
        ClearCachedAim(weapon);
        normalFov_.store(-1.0F, std::memory_order_release);
        projectionScale_.store(1.0F, std::memory_order_release);
        return true;
    }

    void StoreCachedAim(
        const void* weapon,
        const void* soldier,
        const bfvr::stereo::Matrix4& controllerGunWorld,
        LONG controllerGeneration,
        const bfvr::stereo::Matrix4* trackedAimCorrection,
        const bfvr::stereo::Matrix4* offHandSupportFromGun,
        const bool resetTrackedCalibration) noexcept
    {
        AcquireSRWLockExclusive(&aimLock_);
        const bool sameLifetime = cachedAimValid_ &&
            cachedAimWeapon_ == weapon && cachedAimSoldier_ == soldier;
        cachedAimWeapon_ = weapon;
        cachedAimSoldier_ = soldier;
        cachedControllerGunWorld_ = controllerGunWorld;
        cachedControllerGeneration_ = controllerGeneration;
        if (trackedAimCorrection != nullptr)
        {
            cachedTrackedAimCorrection_ = *trackedAimCorrection;
            cachedTrackedAimCorrectionValid_ = true;
        }
        else if (resetTrackedCalibration || !sameLifetime)
        {
            cachedTrackedAimCorrection_ = {};
            cachedTrackedAimCorrectionValid_ = false;
        }
        if (offHandSupportFromGun != nullptr)
        {
            cachedOffHandSupportFromGun_ = *offHandSupportFromGun;
            cachedOffHandSupportValid_ = true;
        }
        else if (resetTrackedCalibration || !sameLifetime)
        {
            cachedOffHandSupportFromGun_ = {};
            cachedOffHandSupportValid_ = false;
        }
        cachedAimValid_ = true;
        ReleaseSRWLockExclusive(&aimLock_);
    }

    bool ReadCachedAim(
        const void* weapon,
        CachedScopeAim& aim) noexcept
    {
        aim = {};
        AcquireSRWLockShared(&aimLock_);
        const bool valid = cachedAimValid_ && cachedAimWeapon_ == weapon;
        if (valid)
        {
            aim.controllerGunWorld = cachedControllerGunWorld_;
            aim.trackedAimCorrection = cachedTrackedAimCorrection_;
            aim.offHandSupportFromGun =
                cachedOffHandSupportFromGun_;
            aim.soldier = cachedAimSoldier_;
            aim.controllerGeneration = cachedControllerGeneration_;
            aim.trackedAimCorrectionValid =
                cachedTrackedAimCorrectionValid_;
            aim.offHandSupportValid = cachedOffHandSupportValid_;
        }
        ReleaseSRWLockShared(&aimLock_);
        return valid;
    }

    void ClearCachedAim(const void* weapon) noexcept
    {
        AcquireSRWLockExclusive(&aimLock_);
        if (weapon == nullptr || cachedAimWeapon_ == weapon)
        {
            cachedAimWeapon_ = nullptr;
            cachedAimSoldier_ = nullptr;
            cachedControllerGunWorld_ = {};
            cachedControllerGeneration_ = 0;
            cachedTrackedAimCorrection_ = {};
            cachedOffHandSupportFromGun_ = {};
            cachedTrackedAimCorrectionValid_ = false;
            cachedOffHandSupportValid_ = false;
            cachedAimValid_ = false;
        }
        ReleaseSRWLockExclusive(&aimLock_);
    }

    void ClearAll() noexcept
    {
        requestedWeapon_.store(nullptr, std::memory_order_release);
        ClearCachedAim(nullptr);
        normalFov_.store(-1.0F, std::memory_order_release);
        projectionScale_.store(1.0F, std::memory_order_release);
    }

    bool HasExpectedPrefix() const noexcept
    {
        if (target_ == nullptr)
        {
            return false;
        }
        __try
        {
            return std::memcmp(
                       target_,
                       kExpectedPrefix.data(),
                       kExpectedPrefix.size()) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void RemoveHook()
    {
        InterlockedCompareExchangePointer(&active_, nullptr, this);
        if (hookCreated_)
        {
            MH_RemoveHook(target_);
            hookCreated_ = false;
        }
        original_ = nullptr;
        if (ownsMinHook_)
        {
            MH_Uninitialize();
            ownsMinHook_ = false;
        }
    }

    void WriteLog(const wchar_t* format, ...) const
    {
        if (appendLog_ == nullptr)
        {
            return;
        }
        std::array<wchar_t, 700> message = {};
        va_list arguments;
        va_start(arguments, format);
        _vsnwprintf_s(
            message.data(),
            message.size(),
            _TRUNCATE,
            format,
            arguments);
        va_end(arguments);
        appendLog_(message.data());
    }

    volatile LONG started_ = 0;
    volatile LONG zoomCalls_ = 0;
    volatile LONG activations_ = 0;
    volatile LONG deactivations_ = 0;
    volatile LONG ignoredNonScopeZooms_ = 0;
    volatile LONG localReceiverRejects_ = 0;
    volatile LONG freshAimUpdates_ = 0;
    volatile LONG trackedAimUpdates_ = 0;
    volatile LONG trackedOffHandSteeringUpdates_ = 0;
    volatile LONG latchedAimFallbacks_ = 0;
    volatile LONG trackedAimCorrectionFailures_ = 0;
    volatile LONG firstTrackedAimLogged_ = 0;
    volatile LONG firstTrackedOffHandSteeringLogged_ = 0;
    volatile LONG firstLatchedAimFallbackLogged_ = 0;
    std::atomic<void*> requestedWeapon_ = nullptr;
    std::atomic<float> normalFov_ = -1.0F;
    std::atomic<float> projectionScale_ = 1.0F;
    SRWLOCK aimLock_ = SRWLOCK_INIT;
    bfvr::stereo::Matrix4 cachedControllerGunWorld_ = {};
    bfvr::stereo::Matrix4 cachedTrackedAimCorrection_ = {};
    bfvr::stereo::Matrix4 cachedOffHandSupportFromGun_ = {};
    const void* cachedAimWeapon_ = nullptr;
    const void* cachedAimSoldier_ = nullptr;
    LONG cachedControllerGeneration_ = 0;
    bool cachedAimValid_ = false;
    bool cachedTrackedAimCorrectionValid_ = false;
    bool cachedOffHandSupportValid_ = false;
    void* target_ = nullptr;
    SetZoomFn original_ = nullptr;
    bool hookCreated_ = false;
    bool hookEnabled_ = false;
    bool ownsMinHook_ = false;
    void (*appendLog_)(const wchar_t* message) = nullptr;

    static void* volatile active_;
    static volatile LONG callbackEntrants_;
};

void* volatile ScopeViewOverlay::active_ = nullptr;
volatile LONG ScopeViewOverlay::callbackEntrants_ = 0;
ScopeViewOverlay g_scopeViewOverlay;
} // namespace

namespace bfvr
{

void StartScopeViewOverlay(
    void* gameImage,
    void (*appendLog)(const wchar_t* message))
{
    g_scopeViewOverlay.Start(gameImage, appendLog);
}

void StopScopeViewOverlay()
{
    g_scopeViewOverlay.Stop();
}

bool ReadScopeViewFrameState(ScopeViewFrameState& state) noexcept
{
    return g_scopeViewOverlay.ReadFrameState(state);
}

void InvalidateScopeViewFrameState(const void* weapon) noexcept
{
    g_scopeViewOverlay.InvalidateFrame(weapon);
}

bool IsScopeViewActive() noexcept
{
    return g_scopeViewOverlay.IsActive();
}

float ReadScopeViewProjectionScale() noexcept
{
    return g_scopeViewOverlay.ProjectionScale();
}

} // namespace bfvr
