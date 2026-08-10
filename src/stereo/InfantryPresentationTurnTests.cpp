#include "stereo/InfantryPresentationTurn.h"

#include <cmath>
#include <cstdio>

namespace
{

bool Near(const float left, const float right, const float tolerance = 0.0001F)
{
    return std::fabs(left - right) <= tolerance;
}

bool TestSmoothIsFrameRateIndependent() noexcept
{
    constexpr std::uintptr_t infantry = 0x1000;
    bfvr::stereo::InfantryPresentationTurnState sixtyHz = {};
    bfvr::stereo::InfantryPresentationTurnState ninetyHz = {};
    std::int64_t sixtyTime = 1'000'000'000;
    std::int64_t ninetyTime = 1'000'000'000;
    (void)bfvr::stereo::UpdateInfantryPresentationTurn(
        sixtyHz, true, true, true, false, 1.0F, 100, 45,
        sixtyTime, infantry);
    (void)bfvr::stereo::UpdateInfantryPresentationTurn(
        ninetyHz, true, true, true, false, 1.0F, 100, 45,
        ninetyTime, infantry);

    float sixtyDegrees = 0.0F;
    float ninetyDegrees = 0.0F;
    for (int frame = 0; frame < 60; ++frame)
    {
        sixtyTime += 16'666'667;
        sixtyDegrees += bfvr::stereo::UpdateInfantryPresentationTurn(
            sixtyHz, true, true, true, false, 1.0F, 100, 45,
            sixtyTime, infantry).deltaDegrees;
    }
    for (int frame = 0; frame < 90; ++frame)
    {
        ninetyTime += 11'111'111;
        ninetyDegrees += bfvr::stereo::UpdateInfantryPresentationTurn(
            ninetyHz, true, true, true, false, 1.0F, 100, 45,
            ninetyTime, infantry).deltaDegrees;
    }
    return Near(sixtyDegrees, 63.0F, 0.001F) &&
        Near(ninetyDegrees, 63.0F, 0.001F) &&
        Near(sixtyDegrees, ninetyDegrees, 0.001F);
}

bool TestSmoothRejectsTimingGapAndMenuOwnership() noexcept
{
    constexpr std::uintptr_t infantry = 0x1000;
    bfvr::stereo::InfantryPresentationTurnState state = {};
    (void)bfvr::stereo::UpdateInfantryPresentationTurn(
        state, true, true, true, false, 1.0F, 100, 45,
        1'000'000'000, infantry);
    const auto menu = bfvr::stereo::UpdateInfantryPresentationTurn(
        state, true, true, true, true, 1.0F, 100, 45,
        1'010'000'000, infantry);
    const auto gap = bfvr::stereo::UpdateInfantryPresentationTurn(
        state, true, true, true, false, 1.0F, 100, 45,
        1'200'000'000, infantry);
    const auto resumed = bfvr::stereo::UpdateInfantryPresentationTurn(
        state, true, true, true, false, 1.0F, 100, 45,
        1'210'000'000, infantry);
    return menu.deltaDegrees == 0.0F &&
        gap.deltaDegrees == 0.0F && gap.timingDiscontinuity &&
        Near(resumed.deltaDegrees, 0.63F);
}

bool TestSnapEmitsOnceAndRearms() noexcept
{
    constexpr std::uintptr_t infantry = 0x1000;
    bfvr::stereo::InfantryPresentationTurnState state = {};
    (void)bfvr::stereo::UpdateInfantryPresentationTurn(
        state, true, false, true, false, 0.0F, 100, 45,
        1'000'000'000, infantry);
    const auto first = bfvr::stereo::UpdateInfantryPresentationTurn(
        state, true, false, true, false, -0.8F, 100, 45,
        1'010'000'000, infantry);
    const auto held = bfvr::stereo::UpdateInfantryPresentationTurn(
        state, true, false, true, false, -0.8F, 100, 45,
        1'020'000'000, infantry);
    (void)bfvr::stereo::UpdateInfantryPresentationTurn(
        state, true, false, true, false, 0.0F, 100, 45,
        1'030'000'000, infantry);
    const auto second = bfvr::stereo::UpdateInfantryPresentationTurn(
        state, true, false, true, false, 0.8F, 100, 45,
        1'040'000'000, infantry);
    return first.snapApplied && Near(first.deltaDegrees, -45.0F) &&
        !held.snapApplied && held.deltaDegrees == 0.0F &&
        second.snapApplied && Near(second.deltaDegrees, 45.0F);
}

bool TestLifetimeAndTrackingLossDoNotReplay() noexcept
{
    bfvr::stereo::InfantryPresentationTurnState state = {};
    (void)bfvr::stereo::UpdateInfantryPresentationTurn(
        state, true, true, true, false, 1.0F, 100, 45,
        1'000'000'000, 0x1000);
    const auto applied = bfvr::stereo::UpdateInfantryPresentationTurn(
        state, true, true, true, false, 1.0F, 100, 45,
        1'010'000'000, 0x1000);
    const auto lost = bfvr::stereo::UpdateInfantryPresentationTurn(
        state, false, true, true, false, 1.0F, 100, 45,
        1'020'000'000, 0x1000);
    const auto reacquired = bfvr::stereo::UpdateInfantryPresentationTurn(
        state, true, true, true, false, 1.0F, 100, 45,
        2'000'000'000, 0x2000);
    return applied.smoothApplied && lost.deltaDegrees == 0.0F &&
        reacquired.lifetimeCaptured && reacquired.deltaDegrees == 0.0F;
}

} // namespace

int main()
{
    if (!TestSmoothIsFrameRateIndependent() ||
        !TestSmoothRejectsTimingGapAndMenuOwnership() ||
        !TestSnapEmitsOnceAndRearms() ||
        !TestLifetimeAndTrackingLossDoNotReplay())
    {
        std::fprintf(stderr, "Infantry presentation-turn tests failed.\n");
        return 1;
    }
    std::printf("Infantry presentation-turn tests passed.\n");
    return 0;
}
