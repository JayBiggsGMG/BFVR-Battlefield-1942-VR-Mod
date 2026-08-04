#include "client/ScopeViewOverlay.h"

#include "client/BFSoldierVrMotionFilter.h"
#include "client/BFSoldierOffHandWeaponSteering.h"
#include "client/ScopedOffHandSupportPoseCache.h"
#include "client/TrackedScopeAim.h"
#include "client/WeaponPoseRuntimeCache.h"
#include "stereo/OffHandWeaponSteeringMath.h"
#include "stereo/ScopeViewMath.h"

#include <MinHook.h>

#include <intrin.h>
#include <windows.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <optional>

namespace
{
constexpr std::ptrdiff_t kFireArmsSetZoomRva = 0x001391B0;
constexpr std::ptrdiff_t kBFSoldierSetStateBitsRva = 0x000F76D0;
constexpr unsigned int kBFSoldierZoomStateBit = 0x20U;
constexpr std::size_t kFireArmsTemplateOffset = 0x4C;
constexpr std::size_t kFireArmsNormalFovOffset = 0x1F4;
constexpr std::size_t kFireArmsTargetFovOffset = 0x1F8;
constexpr std::size_t kFireArmsZoomedStateOffset = 0x278;
constexpr std::size_t kFireArmsTemplateUseScopeOffset = 0x3D8;
constexpr std::size_t kFireArmsTemplateZoomFovOffset = 0x3DC;
constexpr DWORD kNativeArmPoseMaximumAgeMs = 125;
constexpr DWORD kScopeIntentMaximumAgeMs = 250;
constexpr float kBf1942WorldUnitsPerMetre = 1.0F;
constexpr std::array<BYTE, 30> kFireArmsSetZoomExpectedPrefix = {
    0x55, 0x56, 0x8B, 0xF1, 0x8B, 0x6E, 0x4C, 0xD9,
    0x85, 0xDC, 0x03, 0x00, 0x00, 0xD8, 0x1D, 0xAC,
    0x41, 0x8C, 0x00, 0xDF, 0xE0, 0xF6, 0xC4, 0x05,
    0x0F, 0x8B, 0x3D, 0x02, 0x00, 0x00};
constexpr std::array<BYTE, 26> kBFSoldierSetStateBitsExpectedPrefix = {
    0x53, 0x8B, 0x5C, 0x24, 0x08, 0x33, 0xC0, 0x8A,
    0xC3, 0xC0, 0xE8, 0x04, 0x56, 0x8B, 0xF1, 0x25,
    0x01, 0xFF, 0xFF, 0xFF, 0x50, 0xE8, 0xC6, 0xFC,
    0xFF, 0xFF};

enum class ScopeIntentSource : LONG
{
    None = 0,
    Controller = 1,
    Native = 2,
};

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

struct RuntimeZoomFields
{
    float savedNormalFov = -1.0F;
    float targetFov = -1.0F;
    bool zoomed = false;
};

bool ReadRuntimeZoomFields(
    const void* weapon,
    RuntimeZoomFields& fields) noexcept
{
    fields = {};
    if (weapon == nullptr)
    {
        return false;
    }
    __try
    {
        const auto* const weaponBytes =
            static_cast<const std::byte*>(weapon);
        fields.savedNormalFov = *reinterpret_cast<const float*>(
            weaponBytes + kFireArmsNormalFovOffset);
        fields.targetFov = *reinterpret_cast<const float*>(
            weaponBytes + kFireArmsTargetFovOffset);
        fields.zoomed = std::to_integer<BYTE>(
            weaponBytes[kFireArmsZoomedStateOffset]) != 0;
        return std::isfinite(fields.savedNormalFov) &&
            std::isfinite(fields.targetFov);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        fields = {};
        return false;
    }
}

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
    using SetZoomFn = void(__thiscall*)(void* weapon, BYTE enabled);
    using SetStateBitsFn = void(__thiscall*)(
        void* soldier,
        unsigned int stateBits);

    void Start(
        void* gameImage,
        void (*log)(const wchar_t* message))
    {
        if (InterlockedCompareExchange(&started_, 1, 0) != 0)
        {
            return;
        }
        appendLog_ = log;
        setZoomTarget_ = gameImage == nullptr
            ? nullptr
            : static_cast<std::byte*>(gameImage) + kFireArmsSetZoomRva;
        setStateBitsTarget_ = gameImage == nullptr
            ? nullptr
            : static_cast<std::byte*>(gameImage) +
                kBFSoldierSetStateBitsRva;
        if (!HasExpectedPrefix(
                setZoomTarget_,
                kFireArmsSetZoomExpectedPrefix.data(),
                kFireArmsSetZoomExpectedPrefix.size()))
        {
            WriteLog(
                L"Scoped-view activation rejected target %p: the profiled WinPC FireArms::setZoom bytes differ.",
                setZoomTarget_);
            InterlockedExchange(&started_, 0);
            return;
        }
        if (!HasExpectedPrefix(
                setStateBitsTarget_,
                kBFSoldierSetStateBitsExpectedPrefix.data(),
                kBFSoldierSetStateBitsExpectedPrefix.size()))
        {
            WriteLog(
                L"Scoped-view activation rejected target %p: the profiled WinPC BFSoldier::setStateBits bytes differ.",
                setStateBitsTarget_);
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

        const MH_STATUS createZoomStatus = MH_CreateHook(
            setZoomTarget_,
            reinterpret_cast<LPVOID>(&ScopeViewOverlay::Hook),
            reinterpret_cast<LPVOID*>(&originalSetZoom_));
        if (createZoomStatus != MH_OK || originalSetZoom_ == nullptr)
        {
            WriteLog(
                L"Scoped-view activation could not create its FireArms hook (status=%d).",
                static_cast<int>(createZoomStatus));
            RemoveHook();
            InterlockedExchange(&started_, 0);
            return;
        }
        setZoomHookCreated_ = true;

        const MH_STATUS createStateBitsStatus = MH_CreateHook(
            setStateBitsTarget_,
            reinterpret_cast<LPVOID>(&ScopeViewOverlay::HookSetStateBits),
            reinterpret_cast<LPVOID*>(&originalSetStateBits_));
        if (createStateBitsStatus != MH_OK ||
            originalSetStateBits_ == nullptr)
        {
            WriteLog(
                L"Scoped-view activation could not create its BFSoldier state-bit hook (status=%d).",
                static_cast<int>(createStateBitsStatus));
            RemoveHook();
            InterlockedExchange(&started_, 0);
            return;
        }
        setStateBitsHookCreated_ = true;
        InterlockedExchangePointer(&active_, this);
        const MH_STATUS enableZoomStatus = MH_EnableHook(setZoomTarget_);
        if (enableZoomStatus != MH_OK)
        {
            WriteLog(
                L"Scoped-view activation could not enable its FireArms hook (status=%d).",
                static_cast<int>(enableZoomStatus));
            RemoveHook();
            InterlockedExchange(&started_, 0);
            return;
        }
        setZoomHookEnabled_ = true;
        const MH_STATUS enableStateBitsStatus = MH_EnableHook(
            setStateBitsTarget_);
        if (enableStateBitsStatus != MH_OK)
        {
            WriteLog(
                L"Scoped-view activation could not enable its BFSoldier state-bit hook (status=%d).",
                static_cast<int>(enableStateBitsStatus));
            RemoveHook();
            InterlockedExchange(&started_, 0);
            return;
        }
        setStateBitsHookEnabled_ = true;
        WriteLog(
            L"Weapon-directed stereo scope policy armed at FireArms::setZoom 0x005391B0 and BFSoldier::setStateBits 0x004F76D0. Only useScope-enabled exact local active items are eligible; ordinary zoom-only secondary fire remains unchanged. A fresh multiplayer-infantry mouse or controller alt-fire edge owns only that exact weapon/soldier zoom lifetime until the next edge or lifecycle loss; the exact local soldier zoom bit and weapon request are kept consistent while owned.");
    }

    void Stop()
    {
        if (InterlockedCompareExchange(&started_, 0, 0) == 0)
        {
            return;
        }
        if (setStateBitsHookEnabled_)
        {
            MH_DisableHook(setStateBitsTarget_);
            setStateBitsHookEnabled_ = false;
        }
        if (setZoomHookEnabled_)
        {
            MH_DisableHook(setZoomTarget_);
            setZoomHookEnabled_ = false;
        }
        InterlockedCompareExchangePointer(&active_, nullptr, this);
        while (InterlockedCompareExchange(&callbackEntrants_, 0, 0) != 0)
        {
            Sleep(0);
        }
        ClearAll();
        WriteLog(
            L"Scoped-view activation stopped: zoomCalls=%ld stateBitsCalls=%ld activations=%ld deactivations=%ld nonScopeZoomsIgnored=%ld localReceiverRejects=%ld freshAimUpdates=%ld trackedAimUpdates=%ld trackedOffHandSteeringUpdates=%ld latchedAimFallbacks=%ld trackedAimCorrectionFailures=%ld scopeIntentToggles=%ld zoomOverrides=%ld stateBitOverrides=%ld ownedZoomLifetimePreservations=%ld ownedPoseContradictionFallbacks=%ld.",
            InterlockedCompareExchange(&zoomCalls_, 0, 0),
            InterlockedCompareExchange(&stateBitsCalls_, 0, 0),
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
                0),
            InterlockedCompareExchange(&scopeIntentToggles_, 0, 0),
            InterlockedCompareExchange(&zoomOverrides_, 0, 0),
            InterlockedCompareExchange(
                &stateBitOverrides_,
                0,
                0),
            InterlockedCompareExchange(
                &ownedZoomLifetimePreservations_,
                0,
                0),
            InterlockedCompareExchange(
                &ownedPoseContradictionFallbacks_,
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
                    trackedPose.leftSqueezeValue))
            {
                const auto steering =
                    bfvr::stereo::ComputeBoundedOffHandWeaponSteering(
                        oneHandGunWorld,
                        *predictedSupportWorld,
                        trackedPose.leftGripWorld,
                        bfvr::stereo::
                            kUnrestrictedOffHandWeaponSwingRadians,
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
        const void* const ownedSoldier = ownedScopeSoldier_.load(
            std::memory_order_acquire);
        const bool retainExactOwnedLifetime =
            IsOwnedScopeEnabled(requested) && ownedSoldier != nullptr &&
            currentSoldier == ownedSoldier && hasCachedAim &&
            cachedSoldier == ownedSoldier;
        const bool freshPoseMatches = freshPose &&
            !freshPoseContradicts && nativePose.activeItem == requested;
        const auto aimSource = bfvr::stereo::SelectScopeAimSource(
            !requestedChanged,
            freshPoseMatches,
            freshPoseContradicts,
            retainExactOwnedLifetime,
            continuousTrackedPose.has_value(),
            hasCachedAim);
        if (freshPoseContradicts && retainExactOwnedLifetime)
        {
            InterlockedIncrement(&ownedPoseContradictionFallbacks_);
            if (InterlockedCompareExchange(
                    &firstOwnedPoseContradictionFallbackLogged_,
                    1,
                    0) == 0)
            {
                WriteLog(
                    L"Scoped view ignored its first contradictory native-arm cache sample for the still-exact multiplayer-owned weapon=%p soldier=%p. BF1942 hides or republishes that 1P cache while scoped; the validated tracked/latched gun basis and owned zoom lifetime remain active.",
                    requested,
                    ownedSoldier);
            }
        }
        if (aimSource == bfvr::stereo::ScopeAimSource::None)
        {
            if (freshPoseContradicts && !retainExactOwnedLifetime)
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

    void NotifyControllerAltFirePulse() noexcept
    {
        PublishScopeIntent(ScopeIntentSource::Controller);
    }

    void NotifyNativeAltFireInput() noexcept
    {
        PublishScopeIntent(ScopeIntentSource::Native);
    }

    bool IsActivationPending() const noexcept
    {
        return pendingOwnedScopeActivation_.load(
                   std::memory_order_acquire) != nullptr;
    }

    void RecordProjectionReplay(
        std::int32_t frameSequence,
        const bfvr::stereo::Matrix4& sourceProjection,
        const bfvr::stereo::Matrix4& leftProjection,
        const bfvr::stereo::Matrix4& rightProjection) noexcept
    {
        if (frameSequence <= 0 || !IsActive())
        {
            return;
        }
        // Projection telemetry is only needed to confirm that the first
        // scoped frame reached the published world-eye boundary. Continuing
        // to collect and report it while the scope remains open puts logging
        // work on the render path and can cause a visible periodic hitch.
        if (projectionPublicationConfirmed_.load(std::memory_order_acquire))
        {
            return;
        }
        const float scale = projectionScale_.load(
            std::memory_order_acquire);
        if (!std::isfinite(scale) || scale <= 1.0F)
        {
            return;
        }
        projectionSourceX_.store(
            sourceProjection.values[0][0],
            std::memory_order_relaxed);
        projectionSourceY_.store(
            sourceProjection.values[1][1],
            std::memory_order_relaxed);
        projectionLeftX_.store(
            leftProjection.values[0][0],
            std::memory_order_relaxed);
        projectionLeftY_.store(
            leftProjection.values[1][1],
            std::memory_order_relaxed);
        projectionRightX_.store(
            rightProjection.values[0][0],
            std::memory_order_relaxed);
        projectionRightY_.store(
            rightProjection.values[1][1],
            std::memory_order_relaxed);
        projectionReplaySequence_.store(
            frameSequence,
            std::memory_order_release);
        projectionReplayBuilds_.fetch_add(1, std::memory_order_relaxed);
    }

    void NotifyFramePublished(std::int32_t frameSequence) noexcept
    {
        void* const weapon = requestedWeapon_.load(
            std::memory_order_acquire);
        if (weapon == nullptr || frameSequence <= 0 || !IsActive())
        {
            if (projectionPublicationConfirmed_.exchange(
                    false,
                    std::memory_order_acq_rel))
            {
                WriteLog(
                    L"Scoped projection publication interval ended; subsequent world-eye frames are unscaled until another exact scope activates.");
            }
            projectionReplayBuilds_.store(0, std::memory_order_release);
            return;
        }

        if (projectionPublicationConfirmed_.load(std::memory_order_acquire))
        {
            return;
        }
        const bool sequenceMatched =
            projectionReplaySequence_.load(std::memory_order_acquire) ==
            frameSequence;
        if (!sequenceMatched)
        {
            return;
        }
        if (projectionPublicationConfirmed_.exchange(
                true,
                std::memory_order_acq_rel))
        {
            return;
        }

        RuntimeZoomFields runtime = {};
        const bool runtimeReadable = ReadRuntimeZoomFields(weapon, runtime);
        const LONG replayBuilds = projectionReplayBuilds_.exchange(
            0,
            std::memory_order_acq_rel);
        WriteLog(
            L"Scoped projection reached the published world-eye boundary: weapon=%p frame=%ld replaySequenceMatch=1 replayTransforms=%ld requestedScale=%.6f nativeFieldsReadable=%d savedNormalFov=%.6f targetFov=%.6f zoomedByte=%d sourceM00/M11=(%.6f,%.6f) leftM00/M11=(%.6f,%.6f) rightM00/M11=(%.6f,%.6f).",
            weapon,
            static_cast<long>(frameSequence),
            replayBuilds,
            projectionScale_.load(std::memory_order_acquire),
            runtimeReadable ? 1 : 0,
            runtime.savedNormalFov,
            runtime.targetFov,
            runtime.zoomed ? 1 : 0,
            projectionSourceX_.load(std::memory_order_relaxed),
            projectionSourceY_.load(std::memory_order_relaxed),
            projectionLeftX_.load(std::memory_order_relaxed),
            projectionLeftY_.load(std::memory_order_relaxed),
            projectionRightX_.load(std::memory_order_relaxed),
            projectionRightY_.load(std::memory_order_relaxed));
    }

private:
    void PublishScopeIntent(ScopeIntentSource source) noexcept
    {
        scopeIntentTick_.store(GetTickCount(), std::memory_order_relaxed);
        scopeIntentSource_.store(
            static_cast<LONG>(source),
            std::memory_order_relaxed);
        scopeIntentSequence_.fetch_add(1, std::memory_order_release);
    }

    static void __fastcall Hook(
        void* weapon,
        void*,
        BYTE enabled)
    {
        InterlockedIncrement(&callbackEntrants_);
        ScopeViewOverlay* const overlay =
            static_cast<ScopeViewOverlay*>(
                InterlockedCompareExchangePointer(
                    &active_,
                    nullptr,
                    nullptr));
        if (overlay != nullptr && overlay->originalSetZoom_ != nullptr)
        {
            InterlockedIncrement(&overlay->zoomCalls_);
            const void* const returnAddress = _ReturnAddress();
            const bool resolvedEnabled = overlay->ResolveOwnedZoomRequest(
                weapon,
                enabled != 0,
                returnAddress);
            const bool pendingOwnedActivation =
                resolvedEnabled && overlay->IsOwnedScopeEnabled(weapon);
            if (pendingOwnedActivation)
            {
                overlay->pendingOwnedScopeActivation_.store(
                    weapon,
                    std::memory_order_release);
            }
            overlay->originalSetZoom_(
                weapon,
                static_cast<BYTE>(resolvedEnabled ? 1 : 0));
            if (pendingOwnedActivation)
            {
                void* expected = weapon;
                overlay->pendingOwnedScopeActivation_.compare_exchange_strong(
                    expected,
                    nullptr,
                    std::memory_order_acq_rel);
            }
            overlay->ObserveZoomResult(
                weapon,
                resolvedEnabled,
                returnAddress);
        }
        InterlockedDecrement(&callbackEntrants_);
    }

    static void __fastcall HookSetStateBits(
        void* soldier,
        void*,
        unsigned int stateBits)
    {
        InterlockedIncrement(&callbackEntrants_);
        ScopeViewOverlay* const overlay =
            static_cast<ScopeViewOverlay*>(
                InterlockedCompareExchangePointer(
                    &active_,
                    nullptr,
                    nullptr));
        if (overlay != nullptr && overlay->originalSetStateBits_ != nullptr)
        {
            InterlockedIncrement(&overlay->stateBitsCalls_);
            const void* const returnAddress = _ReturnAddress();
            const unsigned int resolvedStateBits =
                overlay->ResolveOwnedSoldierStateBits(
                    soldier,
                    stateBits,
                    returnAddress);
            overlay->originalSetStateBits_(soldier, resolvedStateBits);
        }
        InterlockedDecrement(&callbackEntrants_);
    }

    unsigned int ResolveOwnedSoldierStateBits(
        void* soldier,
        unsigned int nativeStateBits,
        const void* returnAddress) noexcept
    {
        void* const policyWeapon = ownedScopeWeapon_.load(
            std::memory_order_acquire);
        const void* const policySoldier = ownedScopeSoldier_.load(
            std::memory_order_acquire);
        if (policyWeapon == nullptr || policySoldier == nullptr ||
            soldier != policySoldier)
        {
            return nativeStateBits;
        }

        const void* const currentSoldier =
            bfvr::ReadCurrentBFSoldierVrCameraSoldier();
        if (currentSoldier != policySoldier)
        {
            ReleaseOwnedScopePolicy(policyWeapon);
            return nativeStateBits;
        }

        bfvr::NativeArmWeaponVisualPose nativePose = {};
        if (bfvr::ReadFreshNativeArmWeaponVisualPose(
                nativePose,
                kNativeArmPoseMaximumAgeMs) &&
            (nativePose.soldier != policySoldier ||
             nativePose.activeItem != policyWeapon))
        {
            ReleaseOwnedScopePolicy(policyWeapon);
            return nativeStateBits;
        }

        const bool desiredEnabled = ownedScopeDesiredEnabled_.load(
            std::memory_order_acquire);
        const unsigned int resolvedStateBits = desiredEnabled
            ? nativeStateBits | kBFSoldierZoomStateBit
            : nativeStateBits & ~kBFSoldierZoomStateBit;
        if (resolvedStateBits != nativeStateBits)
        {
            InterlockedIncrement(&stateBitOverrides_);
            if (InterlockedCompareExchange(
                    &firstStateBitOverrideLogged_,
                    1,
                    0) == 0)
            {
                WriteLog(
                    L"Multiplayer local scope policy corrected its first exact BFSoldier zoom bit before native state application: soldier=%p weapon=%p incoming=0x%04X resolved=0x%04X desiredZoom=%d callerReturn=%p. FireArms, the soldier state word, and native HUD now receive one consistent value for either controller or native mouse scope input.",
                    soldier,
                    policyWeapon,
                    nativeStateBits & 0xFFFFU,
                    resolvedStateBits & 0xFFFFU,
                    desiredEnabled ? 1 : 0,
                    returnAddress);
            }
        }
        return resolvedStateBits;
    }

    bool ResolveOwnedZoomRequest(
        void* weapon,
        bool nativeEnabled,
        const void* returnAddress) noexcept
    {
        const ULONG intentSequence = scopeIntentSequence_.load(
            std::memory_order_acquire);
        if (intentSequence != observedScopeIntentSequence_)
        {
            observedScopeIntentSequence_ = intentSequence;
            const DWORD intentAge = GetTickCount() -
                scopeIntentTick_.load(std::memory_order_relaxed);
            if (intentAge <= kScopeIntentMaximumAgeMs)
            {
                void* const policyWeapon = ownedScopeWeapon_.load(
                    std::memory_order_acquire);
                const void* const policySoldier = ownedScopeSoldier_.load(
                    std::memory_order_acquire);
                const void* const currentSoldier =
                    bfvr::ReadCurrentBFSoldierVrCameraSoldier();
                const bool continuingExactScope =
                    policyWeapon == weapon && policySoldier != nullptr &&
                    policySoldier == currentSoldier;

                bool eligibleNewScope = false;
                const void* newScopeSoldier = nullptr;
                if (!continuingExactScope)
                {
                    ScopeProperties properties = {};
                    bfvr::NativeArmWeaponVisualPose nativePose = {};
                    eligibleNewScope =
                        ReadScopeProperties(weapon, properties) &&
                        properties.useScope &&
                        bfvr::ReadFreshNativeArmWeaponVisualPose(
                            nativePose,
                            kNativeArmPoseMaximumAgeMs) &&
                        nativePose.activeItem == weapon &&
                        nativePose.soldier != nullptr &&
                        nativePose.soldier == currentSoldier;
                    if (eligibleNewScope)
                    {
                        newScopeSoldier = nativePose.soldier;
                    }
                }

                if (continuingExactScope || eligibleNewScope)
                {
                    const bool desiredEnabled = continuingExactScope
                        ? !ownedScopeDesiredEnabled_.load(
                              std::memory_order_acquire)
                        : true;
                    ownedScopeSoldier_.store(
                        continuingExactScope
                            ? policySoldier
                            : newScopeSoldier,
                        std::memory_order_relaxed);
                    ownedScopeDesiredEnabled_.store(
                        desiredEnabled,
                        std::memory_order_relaxed);
                    ownedScopeWeapon_.store(
                        weapon,
                        std::memory_order_release);
                    InterlockedIncrement(&scopeIntentToggles_);
                    const ScopeIntentSource intentSource =
                        static_cast<ScopeIntentSource>(
                            scopeIntentSource_.load(
                                std::memory_order_relaxed));
                    WriteLog(
                        L"Multiplayer local scope policy latched %s for exact weapon=%p at intentSequence=%lu source=%s; incoming FireArms::setZoom request=%d callerReturn=%p. The next mouse or controller alt-fire edge toggles this exact local lifetime.",
                        desiredEnabled ? L"ON" : L"OFF",
                        weapon,
                        intentSequence,
                        intentSource == ScopeIntentSource::Native
                            ? L"native"
                            : L"controller",
                        nativeEnabled ? 1 : 0,
                        returnAddress);
                }
            }
        }

        void* const policyWeapon = ownedScopeWeapon_.load(
            std::memory_order_acquire);
        if (policyWeapon != weapon)
        {
            return nativeEnabled;
        }

        const void* const policySoldier = ownedScopeSoldier_.load(
            std::memory_order_acquire);
        const void* const currentSoldier =
            bfvr::ReadCurrentBFSoldierVrCameraSoldier();
        if (policySoldier == nullptr || currentSoldier != policySoldier)
        {
            ReleaseOwnedScopePolicy(weapon);
            return nativeEnabled;
        }

        bfvr::NativeArmWeaponVisualPose nativePose = {};
        if (bfvr::ReadFreshNativeArmWeaponVisualPose(
                nativePose,
                kNativeArmPoseMaximumAgeMs) &&
            (nativePose.soldier != policySoldier ||
             nativePose.activeItem != weapon))
        {
            ReleaseOwnedScopePolicy(weapon);
            return nativeEnabled;
        }

        const bool desiredEnabled = ownedScopeDesiredEnabled_.load(
            std::memory_order_acquire);
        if (nativeEnabled != desiredEnabled)
        {
            InterlockedIncrement(&zoomOverrides_);
            if (InterlockedCompareExchange(
                    &firstZoomOverrideLogged_,
                    1,
                    0) == 0)
            {
                WriteLog(
                    L"Multiplayer local scope policy corrected its first conflicting native zoom synchronization for weapon=%p: incoming=%d resolved=%d callerReturn=%p. The exact mouse-or-controller-owned local useScope lifetime remains authoritative until another input edge or lifecycle change.",
                    weapon,
                    nativeEnabled ? 1 : 0,
                    desiredEnabled ? 1 : 0,
                    returnAddress);
            }
        }
        return desiredEnabled;
    }

    bool IsOwnedScopeEnabled(const void* weapon) const noexcept
    {
        return weapon != nullptr &&
            ownedScopeWeapon_.load(std::memory_order_acquire) == weapon &&
            ownedScopeDesiredEnabled_.load(std::memory_order_acquire);
    }

    void ObserveZoomResult(
        void* weapon,
        bool enabled,
        const void* returnAddress) noexcept
    {
        if (!enabled)
        {
            if (ClearIfRequested(weapon))
            {
                InterlockedIncrement(&deactivations_);
                WriteLog(
                    L"Scoped view released for local weapon=%p by callerReturn=%p; normal head-directed VR camera behavior restored.",
                    weapon,
                    returnAddress);
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
        const void* const currentSoldier =
            bfvr::ReadCurrentBFSoldierVrCameraSoldier();
        const void* const policySoldier = ownedScopeSoldier_.load(
            std::memory_order_acquire);
        const bool continuingExactOwnedScope =
            projectionScale.has_value() &&
            requestedWeapon_.load(std::memory_order_acquire) == weapon &&
            IsOwnedScopeEnabled(weapon) && policySoldier != nullptr &&
            currentSoldier == policySoldier;
        if (continuingExactOwnedScope)
        {
            normalFov_.store(
                properties.normalFov,
                std::memory_order_release);
            projectionScale_.store(
                *projectionScale,
                std::memory_order_release);
            InterlockedIncrement(&ownedZoomLifetimePreservations_);
            if (InterlockedCompareExchange(
                    &firstOwnedZoomLifetimePreservationLogged_,
                    1,
                    0) == 0)
            {
                WriteLog(
                    L"Scoped view preserved its first repeated multiplayer-owned setZoom(true) result for exact weapon=%p soldier=%p without revalidating BF1942's now-hidden native 1P arm cache; projectionScale=%.6f remains active until the next input edge or real soldier lifetime change.",
                    weapon,
                    policySoldier,
                    *projectionScale);
            }
            return;
        }
        bfvr::NativeArmWeaponVisualPose nativePose = {};
        if (!projectionScale.has_value() ||
            !bfvr::ReadFreshNativeArmWeaponVisualPose(
                nativePose,
                kNativeArmPoseMaximumAgeMs) ||
            nativePose.activeItem != weapon ||
            nativePose.soldier == nullptr ||
            nativePose.soldier != currentSoldier)
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
                L"Scoped view activated for exact local weapon=%p by callerReturn=%p: normalFov=%.6f scopeFov=%.6f projectionScale=%.6f trackedAimCorrectionReady=%d trackedOffHandReady=%d; camera position remains head-based and direction follows the authoritative gun basis.",
                weapon,
                returnAddress,
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
        pendingOwnedScopeActivation_.store(
            nullptr,
            std::memory_order_release);
        requestedWeapon_.store(nullptr, std::memory_order_release);
        ReleaseOwnedScopePolicy(nullptr);
        ClearCachedAim(nullptr);
        normalFov_.store(-1.0F, std::memory_order_release);
        projectionScale_.store(1.0F, std::memory_order_release);
        projectionReplaySequence_.store(0, std::memory_order_release);
        projectionReplayBuilds_.store(0, std::memory_order_release);
        projectionPublicationConfirmed_.store(false, std::memory_order_release);
    }

    void ReleaseOwnedScopePolicy(const void* weapon) noexcept
    {
        void* const policyWeapon = ownedScopeWeapon_.load(
            std::memory_order_acquire);
        if (weapon != nullptr && policyWeapon != weapon)
        {
            return;
        }
        ownedScopeWeapon_.store(nullptr, std::memory_order_release);
        ownedScopeSoldier_.store(nullptr, std::memory_order_relaxed);
        ownedScopeDesiredEnabled_.store(false, std::memory_order_relaxed);
    }

    bool HasExpectedPrefix(
        const void* target,
        const BYTE* expected,
        std::size_t expectedSize) const noexcept
    {
        if (target == nullptr || expected == nullptr || expectedSize == 0)
        {
            return false;
        }
        __try
        {
            return std::memcmp(target, expected, expectedSize) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void RemoveHook()
    {
        InterlockedCompareExchangePointer(&active_, nullptr, this);
        if (setStateBitsHookEnabled_)
        {
            MH_DisableHook(setStateBitsTarget_);
            setStateBitsHookEnabled_ = false;
        }
        if (setZoomHookEnabled_)
        {
            MH_DisableHook(setZoomTarget_);
            setZoomHookEnabled_ = false;
        }
        if (setStateBitsHookCreated_)
        {
            MH_RemoveHook(setStateBitsTarget_);
            setStateBitsHookCreated_ = false;
        }
        if (setZoomHookCreated_)
        {
            MH_RemoveHook(setZoomTarget_);
            setZoomHookCreated_ = false;
        }
        originalSetStateBits_ = nullptr;
        originalSetZoom_ = nullptr;
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
    volatile LONG stateBitsCalls_ = 0;
    volatile LONG activations_ = 0;
    volatile LONG deactivations_ = 0;
    volatile LONG ignoredNonScopeZooms_ = 0;
    volatile LONG localReceiverRejects_ = 0;
    volatile LONG freshAimUpdates_ = 0;
    volatile LONG trackedAimUpdates_ = 0;
    volatile LONG trackedOffHandSteeringUpdates_ = 0;
    volatile LONG latchedAimFallbacks_ = 0;
    volatile LONG trackedAimCorrectionFailures_ = 0;
    volatile LONG scopeIntentToggles_ = 0;
    volatile LONG zoomOverrides_ = 0;
    volatile LONG stateBitOverrides_ = 0;
    volatile LONG ownedZoomLifetimePreservations_ = 0;
    volatile LONG ownedPoseContradictionFallbacks_ = 0;
    volatile LONG firstTrackedAimLogged_ = 0;
    volatile LONG firstTrackedOffHandSteeringLogged_ = 0;
    volatile LONG firstLatchedAimFallbackLogged_ = 0;
    volatile LONG firstZoomOverrideLogged_ = 0;
    volatile LONG firstStateBitOverrideLogged_ = 0;
    volatile LONG firstOwnedZoomLifetimePreservationLogged_ = 0;
    volatile LONG firstOwnedPoseContradictionFallbackLogged_ = 0;
    std::atomic<ULONG> scopeIntentSequence_ = 0;
    std::atomic<DWORD> scopeIntentTick_ = 0;
    std::atomic<LONG> scopeIntentSource_ =
        static_cast<LONG>(ScopeIntentSource::None);
    ULONG observedScopeIntentSequence_ = 0;
    std::atomic<void*> ownedScopeWeapon_ = nullptr;
    std::atomic<const void*> ownedScopeSoldier_ = nullptr;
    std::atomic<bool> ownedScopeDesiredEnabled_ = false;
    std::atomic<void*> pendingOwnedScopeActivation_ = nullptr;
    std::atomic<void*> requestedWeapon_ = nullptr;
    std::atomic<float> normalFov_ = -1.0F;
    std::atomic<float> projectionScale_ = 1.0F;
    std::atomic<LONG> projectionReplaySequence_ = 0;
    std::atomic<LONG> projectionReplayBuilds_ = 0;
    std::atomic<bool> projectionPublicationConfirmed_ = false;
    std::atomic<float> projectionSourceX_ = 0.0F;
    std::atomic<float> projectionSourceY_ = 0.0F;
    std::atomic<float> projectionLeftX_ = 0.0F;
    std::atomic<float> projectionLeftY_ = 0.0F;
    std::atomic<float> projectionRightX_ = 0.0F;
    std::atomic<float> projectionRightY_ = 0.0F;
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
    void* setZoomTarget_ = nullptr;
    void* setStateBitsTarget_ = nullptr;
    SetZoomFn originalSetZoom_ = nullptr;
    SetStateBitsFn originalSetStateBits_ = nullptr;
    bool setZoomHookCreated_ = false;
    bool setStateBitsHookCreated_ = false;
    bool setZoomHookEnabled_ = false;
    bool setStateBitsHookEnabled_ = false;
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

void NotifyMultiplayerInfantryAltFirePulse() noexcept
{
    g_scopeViewOverlay.NotifyControllerAltFirePulse();
}

void NotifyMultiplayerNativeAltFireInput() noexcept
{
    g_scopeViewOverlay.NotifyNativeAltFireInput();
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

bool IsScopeViewActivationPending() noexcept
{
    return g_scopeViewOverlay.IsActivationPending();
}

float ReadScopeViewProjectionScale() noexcept
{
    return g_scopeViewOverlay.ProjectionScale();
}

void RecordScopeViewProjectionReplay(
    std::int32_t frameSequence,
    const stereo::Matrix4& sourceProjection,
    const stereo::Matrix4& leftProjection,
    const stereo::Matrix4& rightProjection) noexcept
{
    g_scopeViewOverlay.RecordProjectionReplay(
        frameSequence,
        sourceProjection,
        leftProjection,
        rightProjection);
}

void NotifyScopeViewFramePublished(std::int32_t frameSequence) noexcept
{
    g_scopeViewOverlay.NotifyFramePublished(frameSequence);
}

} // namespace bfvr
