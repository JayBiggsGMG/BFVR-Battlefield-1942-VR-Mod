#pragma once

#include "stereo/StereoMath.h"

#include <windows.h>

#include <array>
#include <optional>

namespace bfvr
{

struct BFSoldierOffHandWeaponFingerprint
{
    float zoomFov = 0.0F;
    std::array<float, 3> soldierCameraPosition = {};
};

// Owner-accepted corrections for exact installed weapon-template properties.
// The native relation remains a secondary fingerprint so a mod that reuses a
// stock template but authors a different support pose falls through unchanged.
[[nodiscard]] std::optional<stereo::Matrix4>
ResolveBFSoldierOffHandOverride(
    const BFSoldierOffHandWeaponFingerprint& weaponFingerprint,
    LONG activeItemIndex,
    const stereo::Matrix4& nativeLeftHandFromRightHand) noexcept;

} // namespace bfvr
