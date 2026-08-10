#pragma once

#include "stereo/StereoMath.h"

#include <cstdint>
#include <optional>

namespace bfvr::stereo
{

struct ScopeOverlayFov
{
    float angleLeft = 0.0F;
    float angleRight = 0.0F;
    float angleUp = 0.0F;
    float angleDown = 0.0F;
};

struct ScopeOverlayQuadSize
{
    float widthMeters = 0.0F;
    float heightMeters = 0.0F;
};

struct ScopeOverlayQuad
{
    Pose pose = {};
    float widthMeters = 0.0F;
    float heightMeters = 0.0F;
};

inline constexpr float kEyeFillingScopeOverlayDistanceMeters = 1.0F;

enum class ScopeAimSource
{
    None,
    Fresh,
    Tracked,
    Latched
};

// Unity-style bounded angular stabilization. History can influence the result
// only while its total orientation error is below 0.25 degrees. At or beyond
// that boundary, deliberate motion catches up to raw immediately.
inline constexpr float kScopeAimSmoothingMaximumErrorRadians =
    0.004363323F;
inline constexpr std::int64_t
    kScopeAimSmoothingMaximumSampleIntervalNanoseconds = 50'000'000;

struct ScopeAimSmoothingState
{
    Matrix4 filteredGunWorld = {};
    const void* weapon = nullptr;
    const void* soldier = nullptr;
    std::int64_t predictedDisplayTime = 0;
    std::int32_t controllerGeneration = 0;
    bool valid = false;
};

// Filters rotation only and always keeps the newest sample's translation.
// Repeated reads of one controller generation return the identical result.
// Disabling, changing weapon/soldier lifetime, or receiving an invalid sample
// clears history and starts from the current sample without an entry hitch.
[[nodiscard]] std::optional<Matrix4> UpdateD3D8ScopeAimSmoothing(
    ScopeAimSmoothingState& state,
    const Matrix4& currentGunWorld,
    const void* weapon,
    const void* soldier,
    std::int32_t controllerGeneration,
    std::int64_t predictedDisplayTime,
    bool enabled) noexcept;

void ResetD3D8ScopeAimSmoothing(
    ScopeAimSmoothingState& state) noexcept;

// Accepted fire releases only the exact currently owned scope lifetime.
// BF1942's native post-fire zoom decision then passes through unchanged,
// matching vanilla and mod-specific behavior without a weapon timer.
[[nodiscard]] bool ShouldReleaseD3D8OwnedScopeOnAcceptedShot(
    const void* requestedWeapon,
    const void* ownedWeapon,
    const void* ownedSoldier,
    bool ownedScopeEnabled,
    const void* shotWeapon,
    const void* shotSoldier) noexcept;

// Death is a hard scope lifetime boundary even when BF1942 temporarily keeps
// the old soldier and weapon pointers alive for the deploy-menu transition.
// An unreadable player sample does not authorize a native method call.
[[nodiscard]] bool ShouldReleaseD3D8ScopeForPlayerLifecycle(
    bool localPlayerStateReadable,
    bool localPlayerAlive,
    bool scopeLifetimeActive) noexcept;

// Keeps native scope-mode lifetime independent from transient pose-cache
// availability. Once available, the corrected continuous tracked basis stays
// authoritative even if BF1942 intermittently republishes its hidden 1P arm;
// native and latched poses remain fallbacks. Confirmed lifetime contradictions
// still fail closed.
[[nodiscard]] ScopeAimSource SelectScopeAimSource(
    bool scopeRequested,
    bool freshPoseMatchesRequestedWeapon,
    bool freshPoseContradictsRequestedWeapon,
    bool retainOwnedLifetimeThroughPoseContradiction,
    bool trackedPoseAvailable,
    bool latchedPoseAvailable) noexcept;

// Allows WeaponFire_Core to consume the scoped gun basis only for the exact
// active weapon and soldier lifetime that produced the visible scope frame.
// This keeps the generic useScope path fail-closed across item and respawn
// transitions without relying on weapon names or slots.
[[nodiscard]] bool IsExactScopeFirePoseEligible(
    bool scopeFrameAvailable,
    const void* fireWeapon,
    const void* scopeWeapon,
    const void* currentSoldier,
    const void* scopeSoldier) noexcept;

// Captures the item-specific local correction that maps a raw tracked
// controller-aim world basis to the last authoritative native-arm gun basis.
// The correction can be reused while BF1942 suppresses 1P arm updates in its
// native scope view.
[[nodiscard]] std::optional<Matrix4> MakeD3D8ScopeAimCorrection(
    const Matrix4& authoritativeGunWorld,
    const Matrix4& trackedGunWorld) noexcept;

// Applies a previously validated local correction to a current raw tracked
// controller-aim world basis. The result can be shared by the scope camera and
// exact-weapon scoped-fire fallback without mutating hand or weapon state.
[[nodiscard]] std::optional<Matrix4> ApplyD3D8ScopeAimCorrection(
    const Matrix4& correction,
    const Matrix4& trackedGunWorld) noexcept;

// Preserves the established primary-support binding while the current focused,
// tracked left squeeze remains held. Distance is deliberately acquisition-only;
// this view-policy check does not acquire or mutate native grip state.
[[nodiscard]] bool IsD3D8ScopeOffHandSupportHeld(
    bool bindingEstablished,
    bool sessionFocused,
    bool leftGripTracked,
    bool leftSqueezeActive,
    float leftSqueezeValue) noexcept;

// Converts BF1942's saved normal vertical FOV and configured scope FOV into
// the projection-axis scale needed to preserve the weapon's exact relative
// zoom after BFVR replaces the native projection with the OpenXR eye FOV.
[[nodiscard]] std::optional<float> ComputeD3D8ScopeProjectionScale(
    float normalFovRadians,
    float scopeFovRadians) noexcept;

// Keeps the already head-adjusted camera position at the viewer while using
// the authoritative controller-directed gun basis for the scoped view. The
// weapon translation is deliberately ignored.
[[nodiscard]] std::optional<Matrix4> MakeD3D8WeaponDirectedScopeCamera(
    const Matrix4& headAdjustedCameraWorld,
    const Matrix4& controllerGunWorld) noexcept;

// Applies relative scope zoom to an asymmetric OpenXR projection without
// changing its optical centre or BF1942's native near/far interval.
[[nodiscard]] bool ApplyD3D8ScopeProjectionScale(
    Matrix4& projection,
    float projectionScale) noexcept;

// Sizes one eye-exclusive VIEW-space scope quad around the eye's forward
// axis. Using the larger tangent on each axis preserves the centred native
// reticle while overscanning asymmetric OpenXR view frusta.
[[nodiscard]] std::optional<ScopeOverlayQuadSize>
ComputeEyeFillingScopeOverlayQuadSize(
    const ScopeOverlayFov& fov,
    float distanceMeters,
    float overscanScale = 1.02F) noexcept;

// Reconstructs the eye-exclusive scope quad in the eye's LOCAL coordinate
// space. The desktop mirror uses this same placement instead of sampling the
// Ref2 texture at projection-image coordinates, whose centre is not generally
// the optical axis of an asymmetric OpenXR eye FOV.
[[nodiscard]] std::optional<ScopeOverlayQuad>
MakeEyeFillingScopeOverlayQuad(
    const Pose& eyePose,
    const ScopeOverlayFov& fov,
    float distanceMeters = kEyeFillingScopeOverlayDistanceMeters,
    float overscanScale = 1.02F) noexcept;

} // namespace bfvr::stereo
