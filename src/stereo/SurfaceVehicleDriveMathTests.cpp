#include "stereo/SurfaceVehicleDriveMath.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
constexpr float kDegreesToRadians = 0.01745329251994329577F;
constexpr float kTolerance = 0.0001F;

bfvr::stereo::SurfaceVehicleDriveInput AtClockAngle(
    float clockwiseDegreesFromForward) noexcept
{
    const float angle = clockwiseDegreesFromForward * kDegreesToRadians;
    return bfvr::stereo::MapSurfaceVehicleDrive(
        std::sin(angle),
        std::cos(angle));
}

bool NearlyEqual(float first, float second) noexcept
{
    return std::fabs(first - second) <= kTolerance;
}

bool TestFullThrottleAndProgressiveForwardSteering()
{
    const auto twelve = AtClockAngle(0.0F);
    const auto one = AtClockAngle(30.0F);
    const auto halfPastOne = AtClockAngle(45.0F);
    const auto two = AtClockAngle(60.0F);
    return NearlyEqual(twelve.steering, 0.0F) &&
        NearlyEqual(twelve.throttle, 1.0F) &&
        one.steering > 0.15F && one.steering < 0.25F &&
        NearlyEqual(one.throttle, 1.0F) &&
        halfPastOne.steering > one.steering &&
        halfPastOne.steering < 0.65F &&
        NearlyEqual(halfPastOne.throttle, 1.0F) &&
        NearlyEqual(two.steering, 1.0F) &&
        NearlyEqual(two.throttle, 1.0F);
}

bool TestLeftAndReverseSymmetry()
{
    const auto ten = AtClockAngle(-60.0F);
    const auto eight = AtClockAngle(-120.0F);
    const auto four = AtClockAngle(120.0F);
    return NearlyEqual(ten.steering, -1.0F) &&
        NearlyEqual(ten.throttle, 1.0F) &&
        NearlyEqual(eight.steering, -1.0F) &&
        NearlyEqual(eight.throttle, -1.0F) &&
        NearlyEqual(four.steering, 1.0F) &&
        NearlyEqual(four.throttle, -1.0F);
}

bool TestHorizontalSteeringAndDeadzone()
{
    const auto three = AtClockAngle(90.0F);
    const auto nine = AtClockAngle(-90.0F);
    const auto centre = bfvr::stereo::MapSurfaceVehicleDrive(0.10F, -0.20F);
    return NearlyEqual(three.steering, 1.0F) &&
        NearlyEqual(three.throttle, 0.0F) &&
        NearlyEqual(nine.steering, -1.0F) &&
        NearlyEqual(nine.throttle, 0.0F) &&
        NearlyEqual(centre.steering, 0.0F) &&
        NearlyEqual(centre.throttle, 0.0F);
}

bool TestInvalidInputFailsClosed()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    auto invalid = bfvr::stereo::MapSurfaceVehicleDrive(nan, 1.0F);
    if (!NearlyEqual(invalid.steering, 0.0F) ||
        !NearlyEqual(invalid.throttle, 0.0F))
    {
        return false;
    }

    bfvr::stereo::SurfaceVehicleDriveConfiguration configuration = {};
    configuration.fullSteeringAngleRadians = 0.1F;
    invalid = bfvr::stereo::MapSurfaceVehicleDrive(1.0F, 1.0F, configuration);
    return NearlyEqual(invalid.steering, 0.0F) &&
        NearlyEqual(invalid.throttle, 0.0F);
}
} // namespace

int main()
{
    const bool passed =
        TestFullThrottleAndProgressiveForwardSteering() &&
        TestLeftAndReverseSymmetry() &&
        TestHorizontalSteeringAndDeadzone() &&
        TestInvalidInputFailsClosed();
    if (!passed)
    {
        std::fprintf(stderr, "Surface vehicle drive math tests failed.\n");
        return 1;
    }
    std::printf(
        "Surface vehicle steering reaches full at 10/2 o'clock while retaining full throttle.\n");
    return 0;
}
