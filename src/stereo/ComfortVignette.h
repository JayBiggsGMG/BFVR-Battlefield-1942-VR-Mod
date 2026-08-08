#pragma once

#include "stereo/StereoMath.h"

#include <cstdint>

namespace bfvr::stereo
{

// A short low-pass filter plus separate start/stop thresholds turns the
// legacy engine's quantized object-transform updates into one stable
// locomotion state. The rendered aperture itself is fixed while moving;
// speed is used only to reject stationary transform noise.
constexpr float kComfortVignetteMovementStartMetersPerSecond = 0.08F;
constexpr float kComfortVignetteMovementStopMetersPerSecond = 0.02F;
constexpr float kComfortVignetteSpeedFilterSeconds = 0.10F;
constexpr float kComfortVignetteMaximumSampleIntervalSeconds = 0.25F;
constexpr float kComfortVignetteMaximumPlausibleSpeedMetersPerSecond = 200.0F;
constexpr float kComfortVignetteEaseInSeconds = 0.20F;
constexpr float kComfortVignetteEaseOutSeconds = 0.45F;

struct ComfortVignetteMotionSample
{
    bool valid = false;
    std::uint64_t contextToken = 0;
    std::int64_t predictedDisplayTime = 0;
    Vec3 worldPosition = {};
};

struct ComfortVignetteMotionState
{
    std::uint64_t contextToken = 0;
    std::int64_t predictedDisplayTime = 0;
    Vec3 worldPosition = {};
    float filteredSpeedMetersPerSecond = 0.0F;
    bool moving = false;
    bool valid = false;
};

// Returns a stable binary target from translation alone. Orientation is
// deliberately absent from the sample: HMD look, artificial turning, turret
// aim, and vehicle rotation in place cannot activate the effect. A context
// switch, respawn/teleport-sized discontinuity, or stale sample re-baselines.
[[nodiscard]] float UpdateComfortVignetteMotionTarget(
    ComfortVignetteMotionState& state,
    const ComfortVignetteMotionSample& sample) noexcept;

// Applies asymmetric non-zero ease times so changes in peripheral FOV are not
// instantaneous or distracting. This is evaluated at OpenXR frame cadence.
[[nodiscard]] float AdvanceComfortVignetteStrength(
    float currentStrength,
    float targetStrength,
    float deltaSeconds) noexcept;

} // namespace bfvr::stereo
