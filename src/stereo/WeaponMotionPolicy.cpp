#include "stereo/WeaponMotionPolicy.h"

#include <cmath>
#include <limits>

namespace
{

bool IsFinite(float value) noexcept
{
    return std::isfinite(value);
}

bool HasUsableConfiguration(
    const bfvr::stereo::WeaponMotionTrackingInput& input) noexcept
{
    return input.gripTrackingValid && input.predictedDisplayTime > 0 &&
        IsFinite(input.worldUnitsPerMeter) && input.worldUnitsPerMeter > 0.0F;
}

constexpr float kUnboundedFiniteTranslationMeters =
    std::numeric_limits<float>::max();

} // namespace

namespace bfvr::stereo
{

std::optional<Matrix4> WeaponMotionTracker::Update(
    const WeaponMotionTrackingInput& input) noexcept
{
    if (!HasUsableConfiguration(input) ||
        !MakeD3D8CalibrationSpaceWeaponDelta(
             input.head,
             input.grip,
             input.grip,
             input.worldUnitsPerMeter,
             kUnboundedFiniteTranslationMeters)
             .has_value())
    {
        Reset();
        return std::nullopt;
    }

    if (!calibrated_ || input.predictedDisplayTime < lastPredictedDisplayTime_)
    {
        calibrated_ = true;
        lastPredictedDisplayTime_ = input.predictedDisplayTime;
        referenceHead_ = input.head;
        referenceGrip_ = input.grip;
        return std::nullopt;
    }

    const auto delta = MakeD3D8CalibrationSpaceWeaponDelta(
        referenceHead_,
        referenceGrip_,
        input.grip,
        input.worldUnitsPerMeter,
        kUnboundedFiniteTranslationMeters);
    if (!delta.has_value())
    {
        // A malformed/non-finite pose is never applied. Recenter at the newest
        // usable pose and await the next predicted display time.
        lastPredictedDisplayTime_ = input.predictedDisplayTime;
        referenceHead_ = input.head;
        referenceGrip_ = input.grip;
        return std::nullopt;
    }

    lastPredictedDisplayTime_ = input.predictedDisplayTime;
    return delta;
}

void WeaponMotionTracker::Reset() noexcept
{
    calibrated_ = false;
    lastPredictedDisplayTime_ = 0;
    referenceHead_ = {};
    referenceGrip_ = {};
}

bool WeaponMotionTracker::IsCalibrated() const noexcept
{
    return calibrated_;
}

} // namespace bfvr::stereo
