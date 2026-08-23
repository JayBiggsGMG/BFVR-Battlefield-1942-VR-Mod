#include "stereo/ArmPoleVectorMath.h"

#include <cmath>
#include <cstdio>

namespace
{

bool Near(const float left, const float right, const float tolerance = 1.0e-4F)
{
    return std::fabs(left - right) <= tolerance;
}

float Dot(
    const std::array<float, 3>& left,
    const std::array<float, 3>& right)
{
    return left[0] * right[0] +
        left[1] * right[1] +
        left[2] * right[2];
}

bool Check(const bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        return false;
    }
    return true;
}

} // namespace

int main()
{
    bool passed = true;

    bfvr::stereo::ArmPoleVectorInput right = {};
    right.shoulder = {0.15F, 0.35F, 0.05F};
    right.handTarget = {0.35F, 0.20F, 0.55F};
    const auto rightResult = bfvr::stereo::ComputeArmPoleVector(right);
    passed &= Check(rightResult.has_value(), "right pole should solve");
    if (rightResult.has_value())
    {
        passed &= Check(
            Near(Dot(rightResult->pole, rightResult->pole), 1.0F),
            "right pole must be unit length");
        passed &= Check(
            rightResult->pole[2] < 0.0F,
            "right pole should retain a backward component");
    }

    auto left = right;
    left.leftArm = true;
    left.shoulder[0] = -right.shoulder[0];
    left.handTarget[0] = -right.handTarget[0];
    const auto leftResult = bfvr::stereo::ComputeArmPoleVector(left);
    passed &= Check(leftResult.has_value(), "left pole should solve");
    if (rightResult.has_value() && leftResult.has_value())
    {
        passed &= Check(
            Near(leftResult->pole[0], -rightResult->pole[0]) &&
                Near(leftResult->pole[1], rightResult->pole[1]) &&
                Near(leftResult->pole[2], rightResult->pole[2]),
            "left/right poles should mirror laterally");
    }

    auto preserved = right;
    preserved.preserveNative = true;
    passed &= Check(
        !bfvr::stereo::ComputeArmPoleVector(preserved).has_value(),
        "preserved native arm must not receive a pole");

    auto collapsed = right;
    collapsed.handTarget = collapsed.shoulder;
    passed &= Check(
        !bfvr::stereo::ComputeArmPoleVector(collapsed).has_value(),
        "collapsed hand span must fail closed");

    auto limited = right;
    limited.hasPreviousPole = true;
    limited.previousPole = {-1.0F, 0.0F, 0.0F};
    limited.maximumAngularStepRadians = 0.10F;
    const auto previousResult = bfvr::stereo::ComputeArmPoleVector(limited);
    passed &= Check(
        previousResult.has_value() &&
            previousResult->usedPreviousPole && previousResult->rateLimited,
        "new XR intent should be angularly rate limited");

    auto fallback = right;
    fallback.shoulder = {};
    fallback.handTarget = {0.0F, 1.0F, 0.01F};
    const auto fallbackResult = bfvr::stereo::ComputeArmPoleVector(fallback);
    passed &= Check(
        fallbackResult.has_value() &&
            fallbackResult->usedFallbackAxis,
        "near-vertical hand should use the singularity fallback");

    auto moved = right;
    moved.handTarget = {-0.15F, 0.05F, 0.60F};
    const auto movedResult = bfvr::stereo::ComputeArmPoleVector(moved);
    passed &= Check(
        movedResult.has_value() && rightResult.has_value() &&
            !Near(movedResult->pole[0], rightResult->pole[0]),
        "ordinary elbow intent did not respond to hand position");

    auto nonFinite = right;
    nonFinite.handTarget[1] = std::nanf("");
    passed &= Check(
        !bfvr::stereo::ComputeArmPoleVector(nonFinite).has_value(),
        "non-finite input must fail closed");

    if (passed)
    {
        std::puts("Arm pole vector math tests passed.");
        return 0;
    }
    return 1;
}
