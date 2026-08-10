#pragma once

#include "stereo/StereoMath.h"

#include <optional>

namespace bfvr::stereo
{

enum class WorldCrosshairAimSource
{
    None,
    HandWeapon,
    ControllerPointer,
    MountedWeapon
};

struct WorldCrosshairEligibility
{
    bool localPlayerAlive = false;
    bool controlObjectsReadable = false;
    bool currentIsDefaultControlObject = false;
    bool nativeCrosshairRequested = false;
    bool nativeArmPoseFresh = false;
    bool mountedFirePoseReadable = false;
    int activeItemIndex = -1;
};

struct WorldCrosshairProjection
{
    float centerX = 0.0F;
    float centerY = 0.0F;
    float depth = 0.0F;
    float halfExtentPixels = 0.0F;
};

[[nodiscard]] bool IsWorldCrosshairShootingWeaponItemIndex(
    int itemIndex) noexcept;

// Infantry must have a fresh controller-gun publication for the current
// soldier, while the soldier's authoritative selected-item index must be one
// of the allowed shooting-weapon, knife, or gadget slots. Reading the selected
// slot independently keeps the reticle alive through the native-arm alignment
// warm-up, when its item pointer is deliberately unpublished. Modded items
// which reuse those slots remain eligible by design; unknown slots do not. A
// non-default PlayerControlObject
// is eligible only while BF1942's own HudManager requests its crosshair and an
// exact current weapon-fire pose is readable. Dead, stale, unarmed, unknown
// hand-item, and unresolved mounted states fail closed.
[[nodiscard]] WorldCrosshairAimSource SelectWorldCrosshairAimSource(
    const WorldCrosshairEligibility& eligibility) noexcept;

// Derives a finite no-hit endpoint from the same row-2 forward/origin basis
// consumed by WeaponFire_Core. This does not claim a surface collision.
[[nodiscard]] std::optional<Vec3> MakeWorldCrosshairEndpointFromFirePose(
    const Matrix4& firePose,
    float maximumDistance) noexcept;

// Projects one game-world endpoint through an eye's exact replay transforms.
// The sprite size is angular, so its perceived size remains stable as the
// configured endpoint distance changes.
[[nodiscard]] std::optional<WorldCrosshairProjection>
ProjectWorldCrosshairEndpoint(
    const Vec3& endpoint,
    const Matrix4& eyeView,
    const Matrix4& eyeProjection,
    float viewportWidth,
    float viewportHeight,
    float angularDiameterDegrees) noexcept;

} // namespace bfvr::stereo
