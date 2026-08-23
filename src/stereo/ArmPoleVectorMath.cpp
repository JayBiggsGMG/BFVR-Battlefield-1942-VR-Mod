#include "stereo/ArmPoleVectorMath.h"

#include <cmath>

namespace bfvr::stereo
{
namespace
{

constexpr float kMinimumLengthSquared = 1.0e-6F;
constexpr float kPi = 3.14159265358979323846F;

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

std::optional<std::array<float, 3>> Normalize(
    const std::array<float, 3>& candidate) noexcept
{
    const float lengthSquared = Dot(candidate, candidate);
    if (!std::isfinite(lengthSquared) || lengthSquared < kMinimumLengthSquared)
    {
        return std::nullopt;
    }
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    std::array<float, 3> result = candidate;
    for (float& component : result)
    {
        component *= inverseLength;
    }
    return IsFinite(result)
        ? std::optional<std::array<float, 3>>(result)
        : std::nullopt;
}

float Clamp01(float value) noexcept
{
    return value < 0.0F ? 0.0F : value > 1.0F ? 1.0F : value;
}

std::array<float, 3> Blend(
    const std::array<float, 3>& from,
    const std::array<float, 3>& to,
    float amount) noexcept
{
    return {
        from[0] + (to[0] - from[0]) * amount,
        from[1] + (to[1] - from[1]) * amount,
        from[2] + (to[2] - from[2]) * amount};
}

std::optional<std::array<float, 3>> LimitAngularStep(
    const std::array<float, 3>& previousValue,
    const std::array<float, 3>& currentValue,
    float maximumStep,
    bool& limited) noexcept
{
    const auto previous = Normalize(previousValue);
    const auto current = Normalize(currentValue);
    if (!previous.has_value() || !current.has_value() ||
        !std::isfinite(maximumStep) || maximumStep <= 0.0F ||
        maximumStep > kPi)
    {
        return current;
    }
    const float dot = std::fmax(-1.0F, std::fmin(1.0F, Dot(*previous, *current)));
    const float angle = std::acos(dot);
    if (!std::isfinite(angle) || angle <= maximumStep)
    {
        return current;
    }
    const float amount = maximumStep / angle;
    const float sine = std::sin(angle);
    std::array<float, 3> stepped = {};
    if (std::fabs(sine) > 1.0e-4F)
    {
        const float previousWeight = std::sin((1.0F - amount) * angle) / sine;
        const float currentWeight = std::sin(amount * angle) / sine;
        stepped = {
            (*previous)[0] * previousWeight + (*current)[0] * currentWeight,
            (*previous)[1] * previousWeight + (*current)[1] * currentWeight,
            (*previous)[2] * previousWeight + (*current)[2] * currentWeight};
    }
    else
    {
        stepped = Blend(*previous, *current, amount);
    }
    limited = true;
    return Normalize(stepped);
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

    // BF1942's stable body frame is +X right, +Y up, +Z forward. In ordinary
    // poses, hand position strengthens outward/back bend while keeping the
    // elbow below the wrist. Only vertical or behind-shoulder poses blend to
    // Parger et al.'s singularity-safe direction.
    const float side = input.leftArm ? -1.0F : 1.0F;
    const float sameSide = handDirection[0] * side;
    const float forward = handDirection[2];
    const float vertical = handDirection[1];
    std::array<float, 3> bendDirection = {
        side * (0.45F + 0.25F * Clamp01(forward) +
            0.20F * Clamp01(-sameSide)),
        -0.70F - 0.15F * Clamp01(vertical),
        -0.40F - 0.35F * Clamp01(forward)};
    const std::array<float, 3> singularityDirection = {
        side * 0.133F,
        -0.443F,
        -0.886F};
    const float horizontalLength = std::hypot(
        handDirection[0], handDirection[2]);
    const float verticalBlend = Clamp01((0.35F - horizontalLength) / 0.25F);
    const float behindBlend = Clamp01((-forward - 0.10F) / 0.50F);
    const float singularityBlend = std::fmax(verticalBlend, behindBlend);
    if (singularityBlend > 0.0F)
    {
        bendDirection = Blend(
            bendDirection, singularityDirection, singularityBlend);
    }

    ArmPoleVectorResult result = {};
    const auto normalized = Normalize(bendDirection);
    if (!normalized.has_value())
    {
        if (!input.hasPreviousPole)
        {
            return std::nullopt;
        }
        const auto previous = Normalize(input.previousPole);
        if (!previous.has_value())
        {
            return std::nullopt;
        }
        result.pole = *previous;
        result.usedPreviousPole = true;
        return result;
    }
    result.pole = *normalized;
    result.usedFallbackAxis = singularityBlend > 0.0F;
    if (input.hasPreviousPole &&
        IsFinite(input.previousPole))
    {
        const auto limited = LimitAngularStep(
            input.previousPole,
            result.pole,
            input.maximumAngularStepRadians,
            result.rateLimited);
        if (!limited.has_value())
        {
            return std::nullopt;
        }
        result.pole = *limited;
        result.usedPreviousPole = result.rateLimited;
    }
    return result;
}

} // namespace bfvr::stereo
