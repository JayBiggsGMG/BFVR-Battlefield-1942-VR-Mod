#pragma once

#include "stereo/StereoMath.h"

#include <cstdint>
#include <optional>

namespace bfvr::stereo
{

struct UiCanvasPoint
{
    // Normalized coordinates in the original BF1942 UI canvas. The origin is
    // top-left and both values are within [0, 1].
    float normalizedX = 0.0F;
    float normalizedY = 0.0F;
    // Continuous pixel coordinates in the original BF1942 UI canvas.
    float pixelX = 0.0F;
    float pixelY = 0.0F;
};

// A gravity-aligned LOCAL-space anchor for a large menu panel. Position is
// preserved, but pitch and roll are removed so a tilted head cannot cant the
// panel or its controller-ray coordinate frame.
[[nodiscard]] std::optional<Pose> MakeYawOnlyUiAnchor(
    const Pose& headPose) noexcept;

struct UiMenuAnchorTracker
{
    Pose anchor = {};
    std::int64_t lastPredictedDisplayTime = 0;
    bool valid = false;
};

// Starts a world-locked menu anchor in front of the opening head pose, then
// moves it only when it has become substantially off-centre. The target is
// yaw-only; the caller uses the same anchor for panel presentation and ray
// mapping. predictedDisplayTime is OpenXR nanoseconds.
[[nodiscard]] bool UpdateUiMenuAnchor(
    UiMenuAnchorTracker& tracker,
    const Pose& headPose,
    std::int64_t predictedDisplayTime,
    float followStartRadians,
    float followRadiansPerSecond) noexcept;

void ResetUiMenuAnchor(UiMenuAnchorTracker& tracker) noexcept;

// Expresses current in reference-local coordinates. This is the pose-space
// conversion needed when a controller aim pose and centre-head pose were both
// sampled in LOCAL space, while the UI itself is submitted in VIEW space.
[[nodiscard]] std::optional<Pose> MakePoseRelativeToReference(
    const Pose& reference,
    const Pose& current) noexcept;

// Intersects the OpenXR aim ray (-Z forward) with a quad in the same reference
// space, then removes the exact aspect-fit padding used when BFVR places the
// source UI raster into the OpenXR UI texture. The resulting normalized point
// is then expressed in the separate logical canvas used by BF1942 input.
//
// This is an absolute, stateless mapping: the same ray/quad relationship always
// returns the same canvas coordinate regardless of prior mouse or controller
// movement. Hits on transparent aspect-fit padding return no point.
[[nodiscard]] std::optional<UiCanvasPoint>
MapOpenXRAimPoseToAspectFitUiCanvas(
    const Pose& aimPose,
    const Pose& quadPose,
    float quadWidthMeters,
    float quadHeightMeters,
    std::uint32_t uiTextureWidth,
    std::uint32_t uiTextureHeight,
    std::uint32_t sourceTextureWidth,
    std::uint32_t sourceTextureHeight,
    std::uint32_t logicalCanvasWidth,
    std::uint32_t logicalCanvasHeight) noexcept;

} // namespace bfvr::stereo
