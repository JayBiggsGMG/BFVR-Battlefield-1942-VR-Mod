#pragma once

#include "stereo/StereoMath.h"

#include <optional>

namespace bfvr::stereo
{

enum class WorldCrosshairAimSource
{
    None,
    HandWeapon,
    GadgetController,
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

// The installed stock content assigns all requested throwables/tools/support
// gadgets to slots 4, 5, and 6. Slot 11 is the TNT detonator and is
// deliberately excluded: the owner requested the placement gadget, not an
// unrestricted reticle for every hand item.
[[nodiscard]] bool IsWorldCrosshairGadgetItemIndex(int itemIndex) noexcept;
[[nodiscard]] bool IsWorldCrosshairShootingWeaponItemIndex(
    int itemIndex) noexcept;

// Infantry must have an exact fresh native-arm item pose in one of the three
// allowed shooting-weapon or gadget slots. Modded items which reuse those
// slots remain eligible by design; unknown slots do not. A non-default PlayerControlObject
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
