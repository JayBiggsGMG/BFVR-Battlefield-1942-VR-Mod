#pragma once

#include "stereo/StereoMath.h"

#include <cstdint>

namespace bfvr::stereo
{

// Converts relative right-hand movement into a small native mouse-look delta.
// The tracked position remains in OpenXR LOCAL space: +X is right and +Y is
// up. Surface-weapon motion deliberately mirrors the hand across the virtual
// pivot, so hand-left produces positive/right traverse while hand-down
// produces negative/up elevation in BF1942's mouse-look convention.
struct VehicleMotionAimConfiguration
{
    float inputPerMetre = 24.0F;
    float movementDeadzoneMetres = 0.0005F;
    float maximumInputPerSample = 0.35F;
    float maximumTrackedStepMetres = 0.15F;
};

struct VehicleMotionAimTracker
{
    Vec3 previousPosition = {};
    float pendingX = 0.0F;
    float pendingY = 0.0F;
    std::int64_t previousDisplayTime = 0;
    bool hasPreviousPosition = false;
};

struct VehicleMotionAimOutput
{
    float mouseLookX = 0.0F;
    float mouseLookY = 0.0F;
    bool trackingAccepted = false;
    bool sampleAdvanced = false;
};

void ResetVehicleMotionAim(VehicleMotionAimTracker& tracker) noexcept;

// Only a newly timestamped tracked sample may produce an input delta. The
// first sample, a tracking reacquisition, or an implausibly large tracking
// step becomes a fresh zero-input reference so mode changes cannot jerk a
// turret. Holding the controller still therefore holds the barrel still.
[[nodiscard]] VehicleMotionAimOutput UpdateVehicleMotionAim(
    VehicleMotionAimTracker& tracker,
    bool enabled,
    bool positionTracked,
    Vec3 position,
    std::int64_t predictedDisplayTime,
    const VehicleMotionAimConfiguration& configuration = {}) noexcept;

} // namespace bfvr::stereo
