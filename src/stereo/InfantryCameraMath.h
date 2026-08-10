#pragma once

#include "stereo/StereoMath.h"

#include <optional>

namespace bfvr::stereo
{

// Builds the local VR infantry camera from BF1942's game-owned position and a
// request-matched presentation yaw. Authoritative soldier aim, native camera
// pitch/roll, and view-only recoil cannot enter the presentation orientation.
[[nodiscard]] std::optional<Matrix4> MakeD3D8InfantryPresentationCamera(
    const Matrix4& sourceCameraWorld,
    float presentationYawRadians) noexcept;

} // namespace bfvr::stereo
