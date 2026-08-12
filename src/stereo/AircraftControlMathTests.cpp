#include "stereo/AircraftControlMath.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
bool NearlyEqual(float first, float second) noexcept
{
    return std::fabs(first - second) <= 0.0001F;
}

bool TestAllFourLayouts()
{
    constexpr float leftX = -0.75F;
    constexpr float leftY = 0.80F;
    constexpr float rightX = 0.25F;
    constexpr float rightY = -0.40F;
    const auto rightPitchYaw = bfvr::stereo::MapAircraftControlInput(
        leftX, leftY, rightX, rightY, false, false);
    const auto rightPitchRoll = bfvr::stereo::MapAircraftControlInput(
        leftX, leftY, rightX, rightY, true, false);
    const auto leftPitchYaw = bfvr::stereo::MapAircraftControlInput(
        leftX, leftY, rightX, rightY, false, true);
    const auto leftPitchRoll = bfvr::stereo::MapAircraftControlInput(
        leftX, leftY, rightX, rightY, true, true);
    return
        NearlyEqual(rightPitchYaw.roll, leftX) &&
        NearlyEqual(rightPitchYaw.yaw, rightX) &&
        NearlyEqual(rightPitchYaw.throttle, leftY) &&
        NearlyEqual(rightPitchYaw.pitch, rightY) &&
        NearlyEqual(rightPitchRoll.roll, rightX) &&
        NearlyEqual(rightPitchRoll.yaw, leftX) &&
        NearlyEqual(rightPitchRoll.throttle, leftY) &&
        NearlyEqual(rightPitchRoll.pitch, rightY) &&
        NearlyEqual(leftPitchYaw.roll, rightX) &&
        NearlyEqual(leftPitchYaw.yaw, leftX) &&
        NearlyEqual(leftPitchYaw.throttle, rightY) &&
        NearlyEqual(leftPitchYaw.pitch, leftY) &&
        NearlyEqual(leftPitchRoll.roll, leftX) &&
        NearlyEqual(leftPitchRoll.yaw, rightX) &&
        NearlyEqual(leftPitchRoll.throttle, rightY) &&
        NearlyEqual(leftPitchRoll.pitch, leftY);
}

bool TestInvalidInputFailsClosed()
{
    const auto mapped = bfvr::stereo::MapAircraftControlInput(
        std::numeric_limits<float>::quiet_NaN(),
        0.25F,
        0.5F,
        -0.25F,
        true,
        true);
    return NearlyEqual(mapped.roll, 0.0F) &&
        NearlyEqual(mapped.yaw, 0.0F) &&
        NearlyEqual(mapped.throttle, 0.0F) &&
        NearlyEqual(mapped.pitch, 0.0F);
}
} // namespace

int main()
{
    if (!TestAllFourLayouts() || !TestInvalidInputFailsClosed())
    {
        std::fprintf(stderr, "Aircraft control math tests failed.\n");
        return 1;
    }
    std::printf(
        "Aircraft control math covers pitch/yaw and pitch/roll on either stick.\n");
    return 0;
}
