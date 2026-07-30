#pragma once

#include "stereo/StereoMath.h"

#include <optional>

namespace bfvr::stereo
{

struct OffHandWeaponSteeringResult
{
    Matrix4 gunWorld = {};
    float requestedSwingRadians = 0.0F;
    float appliedSwingRadians = 0.0F;
};

// Rotates a right-authoritative gun basis minimally about its fixed
// translation pivot so the predicted authored support direction approaches
// the tracked left hand. Radial mismatch is ignored and no scale is applied.
// Degenerate, non-finite, or nearly opposite directions fail closed.
[[nodiscard]] std::optional<OffHandWeaponSteeringResult>
ComputeBoundedOffHandWeaponSteering(
    const Matrix4& controllerGunWorld,
    const Matrix4& predictedSupportWorld,
    const Matrix4& trackedLeftHandWorld,
    float maximumSwingRadians,
    float worldUnitsPerMetre) noexcept;

} // namespace bfvr::stereo
