#include "stereo/ComfortVignette.h"

#include <cmath>
#include <cstdio>

namespace
{
constexpr float kTolerance = 0.0001F;

bool NearlyEqual(float left, float right) noexcept
{
    return std::fabs(left - right) <= kTolerance;
}

bfvr::stereo::ComfortVignetteMotionSample Sample(
    std::uint64_t context,
    std::int64_t time,
    bfvr::stereo::Vec3 position) noexcept
{
    return {true, context, time, position};
}

bool TestTranslationOnlyMovementTarget() noexcept
{
    bfvr::stereo::ComfortVignetteMotionState state = {};
    if (!NearlyEqual(
            bfvr::stereo::UpdateComfortVignetteMotionTarget(
                state,
                Sample(1, 1'000'000'000LL, {10.0F, 2.0F, -3.0F})),
            0.0F))
    {
        return false;
    }
    // A rotation-only frame has the same control-object origin and therefore
    // remains completely clear.
    if (!NearlyEqual(
            bfvr::stereo::UpdateComfortVignetteMotionTarget(
                state,
                Sample(1, 1'100'000'000LL, {10.0F, 2.0F, -3.0F})),
            0.0F))
    {
        return false;
    }
    const float walking = bfvr::stereo::UpdateComfortVignetteMotionTarget(
        state,
        Sample(1, 1'200'000'000LL, {10.10F, 2.0F, -3.0F}));
    const float fastVehicle =
        bfvr::stereo::UpdateComfortVignetteMotionTarget(
            state,
            Sample(1, 1'300'000'000LL, {11.10F, 2.0F, -3.0F}));
    return NearlyEqual(walking, 1.0F) &&
        NearlyEqual(fastVehicle, 1.0F);
}

bool TestQuantizedMovementDoesNotFlicker() noexcept
{
    bfvr::stereo::ComfortVignetteMotionState state = {};
    (void)bfvr::stereo::UpdateComfortVignetteMotionTarget(
        state,
        Sample(1, 1'000'000'000LL, {}));
    if (!NearlyEqual(
            bfvr::stereo::UpdateComfortVignetteMotionTarget(
                state,
                Sample(1, 1'020'000'000LL, {0.03F, 0.0F, 0.0F})),
            1.0F))
    {
        return false;
    }
    // BF1942 can publish an unchanged transform on render frames between
    // simulation steps. Those zero deltas must not repeatedly open the iris.
    for (std::int64_t time = 1'040'000'000LL;
         time <= 1'100'000'000LL;
         time += 20'000'000LL)
    {
        if (!NearlyEqual(
                bfvr::stereo::UpdateComfortVignetteMotionTarget(
                    state,
                    Sample(1, time, {0.03F, 0.0F, 0.0F})),
                1.0F))
        {
            return false;
        }
    }
    return true;
}

bool TestSustainedStopReleasesMovementState() noexcept
{
    bfvr::stereo::ComfortVignetteMotionState state = {};
    (void)bfvr::stereo::UpdateComfortVignetteMotionTarget(
        state,
        Sample(1, 1'000'000'000LL, {}));
    (void)bfvr::stereo::UpdateComfortVignetteMotionTarget(
        state,
        Sample(1, 1'020'000'000LL, {0.03F, 0.0F, 0.0F}));
    float target = 1.0F;
    for (std::int64_t time = 1'040'000'000LL;
         time <= 1'700'000'000LL;
         time += 20'000'000LL)
    {
        target = bfvr::stereo::UpdateComfortVignetteMotionTarget(
            state,
            Sample(1, time, {0.03F, 0.0F, 0.0F}));
    }
    return NearlyEqual(target, 0.0F);
}

bool TestContextAndDiscontinuityRebaseline() noexcept
{
    bfvr::stereo::ComfortVignetteMotionState state = {};
    (void)bfvr::stereo::UpdateComfortVignetteMotionTarget(
        state,
        Sample(1, 1'000'000'000LL, {}));
    if (bfvr::stereo::UpdateComfortVignetteMotionTarget(
            state,
            Sample(2, 1'100'000'000LL, {100.0F, 0.0F, 0.0F})) != 0.0F)
    {
        return false;
    }
    return bfvr::stereo::UpdateComfortVignetteMotionTarget(
        state,
        Sample(2, 2'000'000'000LL, {101.0F, 0.0F, 0.0F})) == 0.0F;
}

bool TestEasedTransitions() noexcept
{
    float strength = 0.0F;
    strength = bfvr::stereo::AdvanceComfortVignetteStrength(
        strength,
        1.0F,
        0.10F);
    if (!NearlyEqual(strength, 0.5F))
    {
        return false;
    }
    strength = bfvr::stereo::AdvanceComfortVignetteStrength(
        strength,
        1.0F,
        0.10F);
    if (!NearlyEqual(strength, 1.0F))
    {
        return false;
    }
    strength = bfvr::stereo::AdvanceComfortVignetteStrength(
        strength,
        0.0F,
        0.225F);
    return NearlyEqual(strength, 0.5F);
}
} // namespace

int main()
{
    if (!TestTranslationOnlyMovementTarget() ||
        !TestQuantizedMovementDoesNotFlicker() ||
        !TestSustainedStopReleasesMovementState() ||
        !TestContextAndDiscontinuityRebaseline() ||
        !TestEasedTransitions())
    {
        std::fprintf(stderr, "BFVR comfort-vignette policy tests failed.\n");
        return 1;
    }
    std::printf("BFVR comfort-vignette policy tests passed.\n");
    return 0;
}
