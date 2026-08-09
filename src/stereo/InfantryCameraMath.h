#pragma once

#include "stereo/StereoMath.h"

#include <optional>

namespace bfvr::stereo
{

// Builds the VR infantry camera base from game-owned position and the local
// soldier body's horizontal facing direction. Native camera pitch/roll and
// view-only yaw offsets are deliberately excluded: the HMD owns head
// orientation, while the soldier transform still carries ordinary body turns.
[[nodiscard]] std::optional<Matrix4> MakeD3D8InfantryComfortCamera(
    const Matrix4& sourceCameraWorld,
    const Matrix4& soldierBodyWorld) noexcept;

} // namespace bfvr::stereo
