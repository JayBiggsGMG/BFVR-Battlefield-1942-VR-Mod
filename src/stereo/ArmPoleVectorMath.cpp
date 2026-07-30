#include "stereo/ArmPoleVectorMath.h"

#include <cmath>

namespace bfvr::stereo
{
namespace
{

constexpr float kMinimumLengthSquared = 1.0e-6F;
constexpr float kMinimumPoleProjectionSquared = 2.5e-3F;

bool IsFinite(const std::array<float, 3>& value) noexcept
{
    return std::isfinite(value[0]) &&
        std::isfinite(value[1]) &&
        std::isfinite(value[2]);
}

float Dot(
    const std::array<float, 3>& left,
    const std::array<float, 3>& right) noexcept
{
    return left[0] * right[0] +
        left[1] * right[1] +
        left[2] * right[2];
}

std::array<float, 3> Subtract(
    const std::array<float, 3>& left,
    const std::array<float, 3>& right) noexcept
{
    return {
        left[0] - right[0],
        left[1] - right[1],
        left[2] - right[2]};
}

bool ProjectPole(
    const std::array<float, 3>& candidate,
    const std::array<float, 3>& handDirection,
    std::array<float, 3>& result) noexcept
{
    const float alongHand = Dot(candidate, handDirection);
    result = {
        candidate[0] - handDirection[0] * alongHand,
        candidate[1] - handDirection[1] * alongHand,
        candidate[2] - handDirection[2] * alongHand};
    const float lengthSquared = Dot(result, result);
    if (!std::isfinite(lengthSquared) ||
        lengthSquared < kMinimumPoleProjectionSquared)
    {
        result = {};
        return false;
    }
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    for (float& component : result)
    {
        component *= inverseLength;
    }
    return IsFinite(result);
}

} // namespace

std::optional<ArmPoleVectorResult> ComputeArmPoleVector(
    const ArmPoleVectorInput& input) noexcept
{
    if (input.preserveNative ||
        !IsFinite(input.shoulder) ||
        !IsFinite(input.handTarget))
    {
        return std::nullopt;
    }

    auto handDirection = Subtract(input.handTarget, input.shoulder);
    const float handLengthSquared = Dot(handDirection, handDirection);
    if (!std::isfinite(handLengthSquared) ||
        handLengthSquared < kMinimumLengthSquared)
    {
        return std::nullopt;
    }
    const float inverseHandLength = 1.0F / std::sqrt(handLengthSquared);
    for (float& component : handDirection)
    {
        component *= inverseHandLength;
    }

    // BF1942 local axes are +X right, +Y up, +Z forward. This mirrors the
    // outward component while keeping both elbows down and back toward the
    // player. The proportions use the fixed singularity-safe direction from
    // Parger et al. (VRST 2018), mapped into BF's component convention.
    const std::array<float, 3> bendDirection = {
        input.leftArm ? -0.133F : 0.133F,
        -0.443F,
        -0.886F};

    ArmPoleVectorResult result = {};
    if (ProjectPole(bendDirection, handDirection, result.pole))
    {
        return result;
    }
    if (input.hasPreviousPole &&
        IsFinite(input.previousPole) &&
        ProjectPole(input.previousPole, handDirection, result.pole))
    {
        result.usedPreviousPole = true;
        return result;
    }

    const std::array<float, 3> outward = {
        input.leftArm ? -1.0F : 1.0F, 0.0F, 0.0F};
    if (ProjectPole(outward, handDirection, result.pole))
    {
        result.usedFallbackAxis = true;
        return result;
    }
    const std::array<float, 3> down = {0.0F, -1.0F, 0.0F};
    if (ProjectPole(down, handDirection, result.pole))
    {
        result.usedFallbackAxis = true;
        return result;
    }
    return std::nullopt;
}

} // namespace bfvr::stereo
