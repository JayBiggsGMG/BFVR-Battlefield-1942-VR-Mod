#pragma once

#include "stereo/WeaponPoseMath.h"

#include <cstdint>
#include <optional>

namespace bfvr::stereo
{

// The tracking input contains only the already synchronized HMD and controller
// grip poses for a single predicted display time. The HMD establishes the
// fixed coordinate basis when calibration starts; later controller motion is
// deliberately independent of current HMD motion. `gripTrackingValid` refers
// to the OpenXR grip *pose*; it is deliberately unrelated to a physical grip
// button or squeeze action.
struct WeaponMotionTrackingInput
{
    bool gripTrackingValid = false;
    std::int64_t predictedDisplayTime = 0;
    Pose head = {};
    Pose grip = {};
    float worldUnitsPerMeter = 1.0F;
};

// Maintains one calibrated right-hand reference for the shared first-person
// weapon family. A missing/invalid sample, predicted-time reversal, or
// invalid pose resets calibration and returns no transform, causing callers to
// leave the game draw untouched. Finite tracked arm travel is deliberately
// unrestricted. The first valid sample calibrates in place, so equipping or
// switching a weapon never causes a visual jump.
class WeaponMotionTracker
{
public:
    [[nodiscard]] std::optional<Matrix4> Update(
        const WeaponMotionTrackingInput& input) noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool IsCalibrated() const noexcept;

private:
    bool calibrated_ = false;
    std::int64_t lastPredictedDisplayTime_ = 0;
    // The HMD pose captured with referenceGrip_. It is the controller mapping's
    // fixed basis, not a parent updated by later HMD motion.
    Pose referenceHead_ = {};
    Pose referenceGrip_ = {};
};

} // namespace bfvr::stereo
