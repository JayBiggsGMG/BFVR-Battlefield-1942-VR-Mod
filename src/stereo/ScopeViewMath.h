#pragma once

#include "stereo/StereoMath.h"

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

enum class ScopeAimSource
{
    None,
    Fresh,
    Tracked,
    Latched
};

// Keeps native scope-mode lifetime independent from transient pose-cache
// availability. A confirmed receiver/lifetime contradiction fails closed;
// an ordinary cache miss may reuse only an already-validated gun basis.
[[nodiscard]] ScopeAimSource SelectScopeAimSource(
    bool scopeRequested,
    bool freshPoseMatchesRequestedWeapon,
    bool freshPoseContradictsRequestedWeapon,
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

// Preserves the established primary-support binding only while the current
// focused, tracked left squeeze remains within its ordinary release gate.
// It is a view-policy check and does not acquire or mutate native grip state.
[[nodiscard]] bool IsD3D8ScopeOffHandSupportHeld(
    bool bindingEstablished,
    bool sessionFocused,
    bool leftGripTracked,
    bool leftSqueezeActive,
    float leftSqueezeValue,
    const Matrix4& predictedSupportWorld,
    const Matrix4& trackedLeftGripWorld,
    float worldUnitsPerMetre = 1.0F) noexcept;

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

} // namespace bfvr::stereo
