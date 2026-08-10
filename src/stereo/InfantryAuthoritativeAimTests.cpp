#include "stereo/InfantryAuthoritativeAim.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace
{

constexpr float kTolerance = 0.0002F;
constexpr float kPi = 3.14159265358979323846F;

bool Near(const float left, const float right) noexcept
{
    return std::fabs(left - right) <= kTolerance;
}

bfvr::stereo::Vec3 Forward(const float yaw, const float pitch) noexcept
{
    const float horizontal = std::cos(pitch);
    return {
        std::sin(yaw) * horizontal,
        std::sin(pitch),
        std::cos(yaw) * horizontal};
}

bool TestLifetimeAndDuplicateSequence() noexcept
{
    bfvr::stereo::InfantryAuthoritativeAimState state = {};
    const auto first = bfvr::stereo::UpdateInfantryAuthoritativeAim(
        state, true, true, Forward(0.25F, 0.1F), 0.0F, 0.0F,
        0x1234, 0x5678, 10);
    const auto duplicate = bfvr::stereo::UpdateInfantryAuthoritativeAim(
        state, true, true, Forward(0.50F, 0.2F), 0.0F, 0.0F,
        0x1234, 0x5678, 10);
    return first.lifetimeCaptured && first.targetAccepted &&
        Near(first.mouseLookX, 0.0F) &&
        duplicate.targetAccepted && !duplicate.lifetimeCaptured &&
        Near(duplicate.mouseLookX, 0.0F) &&
        state.controllerSequence == 10;
}

bool TestAbsoluteYawAndPitchSigns() noexcept
{
    bfvr::stereo::InfantryAuthoritativeAimState yawState = {};
    (void)bfvr::stereo::UpdateInfantryAuthoritativeAim(
        yawState, true, true, Forward(0.0F, 0.0F), 0.0F, 0.0F,
        1, 2, 1);
    const auto yaw = bfvr::stereo::UpdateInfantryAuthoritativeAim(
        yawState, true, true, Forward(0.05F, 0.0F), 0.0F, 0.0F,
        1, 2, 2);

    bfvr::stereo::InfantryAuthoritativeAimState pitchState = {};
    (void)bfvr::stereo::UpdateInfantryAuthoritativeAim(
        pitchState, true, true, Forward(0.0F, 0.0F), 0.0F, 0.0F,
        1, 3, 1);
    const auto pitch = bfvr::stereo::UpdateInfantryAuthoritativeAim(
        pitchState, true, true, Forward(0.0F, 0.04F), 0.0F, 0.0F,
        1, 3, 2);
    return Near(yaw.targetYawRadians, 0.05F) &&
        Near(yaw.yawErrorRadians, 0.05F) &&
        Near(yaw.mouseLookX, 0.5335F) &&
        Near(yaw.mouseLookY, 0.0F) &&
        Near(pitch.targetPitchRadians, 0.04F) &&
        Near(pitch.pitchErrorRadians, 0.04F) &&
        Near(pitch.mouseLookX, 0.0F) &&
        Near(pitch.mouseLookY, -0.4268F);
}

bool TestFeedbackConvergesAndDeadzoneSettles() noexcept
{
    bfvr::stereo::InfantryAuthoritativeAimState state = {};
    (void)bfvr::stereo::UpdateInfantryAuthoritativeAim(
        state, true, true, Forward(0.2F, -0.1F), 0.0F, 0.0F,
        4, 5, 1);
    const auto first = bfvr::stereo::UpdateInfantryAuthoritativeAim(
        state, true, true, Forward(0.2F, -0.1F), 0.1F, -0.05F,
        4, 5, 2);
    const auto second = bfvr::stereo::UpdateInfantryAuthoritativeAim(
        state, true, true, Forward(0.2F, -0.1F), 0.19F, -0.095F,
        4, 5, 3);
    const auto settled = bfvr::stereo::UpdateInfantryAuthoritativeAim(
        state, true, true, Forward(0.2F, -0.1F), 0.199F, -0.099F,
        4, 5, 4);
    return Near(first.mouseLookX, 1.0F) &&
        Near(first.mouseLookY, 0.5335F) &&
        Near(second.mouseLookX, 0.1067F) &&
        Near(second.mouseLookY, 0.05335F) &&
        Near(settled.mouseLookX, 0.0F) &&
        Near(settled.mouseLookY, 0.0F);
}

bool TestShortestYawWrapAndClamp() noexcept
{
    bfvr::stereo::InfantryAuthoritativeAimState wrapState = {};
    (void)bfvr::stereo::UpdateInfantryAuthoritativeAim(
        wrapState, true, true, Forward(-179.0F * kPi / 180.0F, 0.0F),
        179.0F * kPi / 180.0F, 0.0F, 6, 7, 1);
    const auto wrap = bfvr::stereo::UpdateInfantryAuthoritativeAim(
        wrapState, true, true, Forward(-179.0F * kPi / 180.0F, 0.0F),
        179.0F * kPi / 180.0F, 0.0F, 6, 7, 2);

    bfvr::stereo::InfantryAuthoritativeAimState clampState = {};
    (void)bfvr::stereo::UpdateInfantryAuthoritativeAim(
        clampState, true, true, Forward(0.0F, 0.0F), 0.0F, 0.0F,
        8, 9, 1);
    const auto clamped = bfvr::stereo::UpdateInfantryAuthoritativeAim(
        clampState, true, true, Forward(kPi * 0.5F, kPi * 0.25F),
        0.0F, 0.0F, 8, 9, 2);
    return Near(wrap.yawErrorRadians, 2.0F * kPi / 180.0F) &&
        Near(wrap.mouseLookX, 0.372452F) &&
        Near(clamped.mouseLookX, 1.0F) &&
        Near(clamped.mouseLookY, -1.0F);
}

bool TestVerticalAimPreservesYaw() noexcept
{
    bfvr::stereo::InfantryAuthoritativeAimState state = {};
    const bfvr::stereo::Vec3 vertical = {0.0F, 1.0F, 0.0F};
    (void)bfvr::stereo::UpdateInfantryAuthoritativeAim(
        state, true, true, vertical, 0.7F, 0.0F, 10, 11, 1);
    const auto output = bfvr::stereo::UpdateInfantryAuthoritativeAim(
        state, true, true, vertical, 0.7F, 0.2F, 10, 11, 2);
    return Near(output.targetYawRadians, 0.7F) &&
        Near(output.mouseLookX, 0.0F) &&
        Near(output.mouseLookY, -1.0F);
}

bool TestLifetimeAndTrackingReset() noexcept
{
    bfvr::stereo::InfantryAuthoritativeAimState state = {};
    (void)bfvr::stereo::UpdateInfantryAuthoritativeAim(
        state, true, true, Forward(0.0F, 0.0F), 0.0F, 0.0F,
        12, 13, 1);
    const auto changedItem = bfvr::stereo::UpdateInfantryAuthoritativeAim(
        state, true, true, Forward(0.4F, 0.0F), 0.0F, 0.0F,
        12, 14, 2);
    const auto lost = bfvr::stereo::UpdateInfantryAuthoritativeAim(
        state, true, false, Forward(0.5F, 0.0F), 0.0F, 0.0F,
        12, 14, 3);
    const bool resetAfterLoss = !state.valid;
    const auto reacquired = bfvr::stereo::UpdateInfantryAuthoritativeAim(
        state, true, true, Forward(0.6F, 0.0F), 0.0F, 0.0F,
        12, 14, 4);
    return changedItem.lifetimeCaptured &&
        Near(changedItem.mouseLookX, 0.0F) &&
        !lost.targetAccepted && resetAfterLoss &&
        reacquired.lifetimeCaptured && Near(reacquired.mouseLookX, 0.0F);
}

bool TestInvalidInputFailsClosed() noexcept
{
    bfvr::stereo::InfantryAuthoritativeAimState state = {};
    const auto invalid = bfvr::stereo::UpdateInfantryAuthoritativeAim(
        state,
        true,
        true,
        {std::numeric_limits<float>::quiet_NaN(), 0.0F, 1.0F},
        0.0F,
        0.0F,
        15,
        16,
        1);
    return !invalid.targetAccepted && !state.valid;
}

} // namespace

int main()
{
    if (!TestLifetimeAndDuplicateSequence() ||
        !TestAbsoluteYawAndPitchSigns() ||
        !TestFeedbackConvergesAndDeadzoneSettles() ||
        !TestShortestYawWrapAndClamp() ||
        !TestVerticalAimPreservesYaw() ||
        !TestLifetimeAndTrackingReset() ||
        !TestInvalidInputFailsClosed())
    {
        std::fputs("Infantry authoritative aim tests failed.\n", stderr);
        return 1;
    }
    std::puts("Infantry authoritative aim tests passed.");
    return 0;
}
