#include "stereo/VehicleMotionAimMath.h"

#include <cmath>
#include <cstdio>

namespace
{
constexpr float kTolerance = 0.00001F;

bool NearlyEqual(float first, float second) noexcept
{
    return std::fabs(first - second) <= kTolerance;
}

bool TestMirroredPivotDirectionsAndNoDuplicateSample()
{
    bfvr::stereo::VehicleMotionAimTracker tracker = {};
    const auto reference = bfvr::stereo::UpdateVehicleMotionAim(
        tracker,
        true,
        true,
        {0.20F, 1.10F, -0.40F},
        100);
    const auto moved = bfvr::stereo::UpdateVehicleMotionAim(
        tracker,
        true,
        true,
        {0.19F, 1.08F, -0.40F},
        200);
    const auto duplicate = bfvr::stereo::UpdateVehicleMotionAim(
        tracker,
        true,
        true,
        {0.10F, 0.90F, -0.40F},
        200);
    return reference.trackingAccepted && reference.sampleAdvanced &&
        NearlyEqual(reference.mouseLookX, 0.0F) &&
        NearlyEqual(reference.mouseLookY, 0.0F) &&
        NearlyEqual(moved.mouseLookX, 0.24F) &&
        NearlyEqual(moved.mouseLookY, -0.35F) &&
        duplicate.trackingAccepted && !duplicate.sampleAdvanced &&
        NearlyEqual(duplicate.mouseLookX, 0.0F) &&
        NearlyEqual(duplicate.mouseLookY, 0.0F);
}

bool TestOppositeMovementAndJitterDeadzone()
{
    bfvr::stereo::VehicleMotionAimTracker tracker = {};
    (void)bfvr::stereo::UpdateVehicleMotionAim(
        tracker,
        true,
        true,
        {0.0F, 1.0F, 0.0F},
        100);
    const auto jitter = bfvr::stereo::UpdateVehicleMotionAim(
        tracker,
        true,
        true,
        {0.0004F, 0.9996F, 0.0F},
        200);
    const auto rightAndUp = bfvr::stereo::UpdateVehicleMotionAim(
        tracker,
        true,
        true,
        {0.0100F, 1.0100F, 0.0F},
        300);
    return NearlyEqual(jitter.mouseLookX, 0.0F) &&
        NearlyEqual(jitter.mouseLookY, 0.0F) &&
        NearlyEqual(rightAndUp.mouseLookX, -0.24F) &&
        NearlyEqual(rightAndUp.mouseLookY, 0.24F);
}

bool TestSlowMovementAccumulatesAcrossDeadzone()
{
    bfvr::stereo::VehicleMotionAimTracker tracker = {};
    (void)bfvr::stereo::UpdateVehicleMotionAim(
        tracker,
        true,
        true,
        {0.0F, 1.0F, 0.0F},
        100);
    const auto first = bfvr::stereo::UpdateVehicleMotionAim(
        tracker,
        true,
        true,
        {-0.0002F, 1.0F, 0.0F},
        200);
    const auto second = bfvr::stereo::UpdateVehicleMotionAim(
        tracker,
        true,
        true,
        {-0.0004F, 1.0F, 0.0F},
        300);
    const auto third = bfvr::stereo::UpdateVehicleMotionAim(
        tracker,
        true,
        true,
        {-0.0006F, 1.0F, 0.0F},
        400);
    return NearlyEqual(first.mouseLookX, 0.0F) &&
        NearlyEqual(second.mouseLookX, 0.0F) &&
        NearlyEqual(third.mouseLookX, 0.0144F);
}

bool TestTrackingLossAndModePauseRebaseline()
{
    bfvr::stereo::VehicleMotionAimTracker tracker = {};
    (void)bfvr::stereo::UpdateVehicleMotionAim(
        tracker,
        true,
        true,
        {0.0F, 1.0F, 0.0F},
        100);
    const auto lost = bfvr::stereo::UpdateVehicleMotionAim(
        tracker,
        true,
        false,
        {0.1F, 0.9F, 0.0F},
        200);
    const auto reacquired = bfvr::stereo::UpdateVehicleMotionAim(
        tracker,
        true,
        true,
        {0.1F, 0.9F, 0.0F},
        300);
    const auto paused = bfvr::stereo::UpdateVehicleMotionAim(
        tracker,
        false,
        true,
        {0.2F, 0.8F, 0.0F},
        400);
    const auto resumed = bfvr::stereo::UpdateVehicleMotionAim(
        tracker,
        true,
        true,
        {0.2F, 0.8F, 0.0F},
        500);
    return !lost.trackingAccepted &&
        reacquired.trackingAccepted &&
        NearlyEqual(reacquired.mouseLookX, 0.0F) &&
        NearlyEqual(reacquired.mouseLookY, 0.0F) &&
        !paused.trackingAccepted && resumed.trackingAccepted &&
        NearlyEqual(resumed.mouseLookX, 0.0F) &&
        NearlyEqual(resumed.mouseLookY, 0.0F);
}

bool TestTrackingJumpBecomesZeroInputReference()
{
    bfvr::stereo::VehicleMotionAimTracker tracker = {};
    (void)bfvr::stereo::UpdateVehicleMotionAim(
        tracker,
        true,
        true,
        {0.0F, 1.0F, 0.0F},
        100);
    const auto jump = bfvr::stereo::UpdateVehicleMotionAim(
        tracker,
        true,
        true,
        {0.4F, 1.0F, 0.0F},
        200);
    const auto afterJump = bfvr::stereo::UpdateVehicleMotionAim(
        tracker,
        true,
        true,
        {0.39F, 1.0F, 0.0F},
        300);
    return jump.trackingAccepted && jump.sampleAdvanced &&
        NearlyEqual(jump.mouseLookX, 0.0F) &&
        NearlyEqual(jump.mouseLookY, 0.0F) &&
        NearlyEqual(afterJump.mouseLookX, 0.24F) &&
        NearlyEqual(afterJump.mouseLookY, 0.0F);
}
} // namespace

int main()
{
    const bool passed =
        TestMirroredPivotDirectionsAndNoDuplicateSample() &&
        TestOppositeMovementAndJitterDeadzone() &&
        TestSlowMovementAccumulatesAcrossDeadzone() &&
        TestTrackingLossAndModePauseRebaseline() &&
        TestTrackingJumpBecomesZeroInputReference();
    if (!passed)
    {
        std::fprintf(stderr, "Vehicle motion-aim math tests failed.\n");
        return 1;
    }
    std::printf("Vehicle motion-aim math tests passed.\n");
    return 0;
}
