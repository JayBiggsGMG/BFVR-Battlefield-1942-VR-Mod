#pragma once

#include "stereo/StereoMath.h"

#include <optional>

namespace bfvr::stereo
{

// Tracks the camera-facing yaw separately from BF1942's observed soldier yaw.
// Updating observedYaw even while its delta is suppressed prevents a delayed
// snap when a transient game-owned correction ends.
struct InfantryCameraHeadingState
{
    float presentedYaw = 0.0F;
    float observedYaw = 0.0F;
    bool initialized = false;
};

// Builds the VR infantry camera base from game-owned position and the local
// soldier body's horizontal facing direction. Native camera pitch/roll and
// view-only yaw offsets are deliberately excluded: the HMD owns head
// orientation, while the soldier transform still carries ordinary body turns.
[[nodiscard]] std::optional<Matrix4> MakeD3D8InfantryComfortCamera(
    const Matrix4& sourceCameraWorld,
    const Matrix4& soldierBodyWorld) noexcept;

// Stateful variant used by the live camera path. When suppressObservedYawDelta
// is true, the current body-yaw delta is consumed without rotating the camera.
// Later ordinary/intentional body turns remain relative and do not replay the
// suppressed correction.
[[nodiscard]] std::optional<Matrix4> MakeD3D8FilteredInfantryComfortCamera(
    const Matrix4& sourceCameraWorld,
    const Matrix4& soldierBodyWorld,
    bool suppressObservedYawDelta,
    InfantryCameraHeadingState& headingState) noexcept;

} // namespace bfvr::stereo
