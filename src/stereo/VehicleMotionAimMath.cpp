#include "stereo/VehicleMotionAimMath.h"

#include <algorithm>
#include <cmath>

namespace
{
bool IsFinite(const bfvr::stereo::Vec3& value) noexcept
{
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

bool IsValid(
    const bfvr::stereo::VehicleMotionAimConfiguration& configuration) noexcept
{
    return std::isfinite(configuration.inputPerMetre) &&
        configuration.inputPerMetre > 0.0F &&
        std::isfinite(configuration.movementDeadzoneMetres) &&
        configuration.movementDeadzoneMetres >= 0.0F &&
        std::isfinite(configuration.maximumInputPerSample) &&
        configuration.maximumInputPerSample > 0.0F &&
        std::isfinite(configuration.maximumTrackedStepMetres) &&
        configuration.maximumTrackedStepMetres > 0.0F;
}

float ReleaseAccumulatedMovement(float& value, float deadzone) noexcept
{
    if (std::fabs(value) <= deadzone)
    {
        return 0.0F;
    }
    const float released = value;
    value = 0.0F;
    return released;
}
} // namespace

namespace bfvr::stereo
{
void ResetVehicleMotionAim(VehicleMotionAimTracker& tracker) noexcept
{
    tracker = {};
}

VehicleMotionAimOutput UpdateVehicleMotionAim(
    VehicleMotionAimTracker& tracker,
    bool enabled,
    bool positionTracked,
    Vec3 position,
    std::int64_t predictedDisplayTime,
    const VehicleMotionAimConfiguration& configuration) noexcept
{
    VehicleMotionAimOutput output = {};
    if (!enabled || !positionTracked || predictedDisplayTime <= 0 ||
        !IsFinite(position) || !IsValid(configuration))
    {
        ResetVehicleMotionAim(tracker);
        return output;
    }

    output.trackingAccepted = true;
    if (tracker.hasPreviousPosition &&
        predictedDisplayTime == tracker.previousDisplayTime)
    {
        return output;
    }

    output.sampleAdvanced = true;
    if (!tracker.hasPreviousPosition ||
        predictedDisplayTime < tracker.previousDisplayTime)
    {
        tracker.pendingX = 0.0F;
        tracker.pendingY = 0.0F;
        tracker.previousPosition = position;
        tracker.previousDisplayTime = predictedDisplayTime;
        tracker.hasPreviousPosition = true;
        return output;
    }

    const Vec3 delta = {
        position.x - tracker.previousPosition.x,
        position.y - tracker.previousPosition.y,
        position.z - tracker.previousPosition.z};
    tracker.previousPosition = position;
    tracker.previousDisplayTime = predictedDisplayTime;

    const float trackedStep = std::sqrt(
        delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
    if (!std::isfinite(trackedStep) ||
        trackedStep > configuration.maximumTrackedStepMetres)
    {
        tracker.pendingX = 0.0F;
        tracker.pendingY = 0.0F;
        return output;
    }

    // Accumulate sub-deadzone changes rather than throwing them away. Slow,
    // deliberate fine movement will eventually cross the threshold, while
    // alternating sub-millimetre tracking noise cancels in the accumulator.
    tracker.pendingX += delta.x;
    tracker.pendingY += delta.y;
    const float horizontal =
        -ReleaseAccumulatedMovement(
            tracker.pendingX,
            configuration.movementDeadzoneMetres) *
        configuration.inputPerMetre;
    const float vertical =
        ReleaseAccumulatedMovement(
            tracker.pendingY,
            configuration.movementDeadzoneMetres) *
        configuration.inputPerMetre;
    output.mouseLookX = std::clamp(
        horizontal,
        -configuration.maximumInputPerSample,
        configuration.maximumInputPerSample);
    output.mouseLookY = std::clamp(
        vertical,
        -configuration.maximumInputPerSample,
        configuration.maximumInputPerSample);
    return output;
}
} // namespace bfvr::stereo
