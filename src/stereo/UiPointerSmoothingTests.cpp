#include "stereo/UiPointerSmoothing.h"

#include <cmath>
#include <iostream>

namespace
{
bool NearlyEqual(float lhs, float rhs, float tolerance = 0.00001F)
{
    return std::fabs(lhs - rhs) <= tolerance;
}

bool TestFirstSampleAndBypass()
{
    bfvr::stereo::UiPointerSmoother smoother;
    const auto first = smoother.Update(0.25F, 0.75F, 1'000'000'000, true);
    const auto bypass = smoother.Update(0.90F, 0.10F, 1'011'111'111, false);
    return NearlyEqual(first.x, 0.25F) && NearlyEqual(first.y, 0.75F) &&
        NearlyEqual(bypass.x, 0.90F) && NearlyEqual(bypass.y, 0.10F);
}

bool TestTremorDeadzone()
{
    bfvr::stereo::UiPointerSmoother smoother;
    (void)smoother.Update(0.5F, 0.5F, 1'000'000'000, true);
    const auto filtered = smoother.Update(
        0.5009F,
        0.4991F,
        1'011'111'111,
        true);
    return NearlyEqual(filtered.x, 0.5F) && NearlyEqual(filtered.y, 0.5F);
}

bool TestDeliberateMotionRemainsResponsive()
{
    bfvr::stereo::UiPointerSmoother smoother;
    (void)smoother.Update(0.1F, 0.5F, 1'000'000'000, true);
    const auto moved = smoother.Update(0.9F, 0.5F, 1'011'111'111, true);
    return moved.x > 0.78F && moved.x < 0.9F && NearlyEqual(moved.y, 0.5F);
}
} // namespace

int main()
{
    if (!TestFirstSampleAndBypass() || !TestTremorDeadzone() ||
        !TestDeliberateMotionRemainsResponsive())
    {
        std::cerr << "BFVR UI pointer smoothing tests failed.\n";
        return 1;
    }
    std::cout << "BFVR UI pointer smoothing tests passed.\n";
    return 0;
}
