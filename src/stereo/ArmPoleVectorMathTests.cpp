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
        std::array<float, 3> span = {
            right.handTarget[0] - right.shoulder[0],
            right.handTarget[1] - right.shoulder[1],
            right.handTarget[2] - right.shoulder[2]};
        const float spanLength = std::sqrt(Dot(span, span));
        for (float& component : span)
        {
            component /= spanLength;
        }
        passed &= Check(
            Near(Dot(rightResult->pole, span), 0.0F),
            "right pole must be perpendicular to hand span");
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

    auto parallel = right;
    parallel.shoulder = {};
    parallel.handTarget = {0.133F, -0.443F, -0.886F};
    parallel.hasPreviousPole = true;
    parallel.previousPole = {1.0F, 0.0F, 0.0F};
    const auto previousResult =
        bfvr::stereo::ComputeArmPoleVector(parallel);
    passed &= Check(
        previousResult.has_value() &&
            previousResult->usedPreviousPole,
        "near-parallel bend direction should reuse previous continuity");

    auto fallback = parallel;
    fallback.hasPreviousPole = false;
    const auto fallbackResult =
        bfvr::stereo::ComputeArmPoleVector(fallback);
    passed &= Check(
        fallbackResult.has_value() &&
            fallbackResult->usedFallbackAxis,
        "near-parallel bend without history should use a fixed axis");

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
