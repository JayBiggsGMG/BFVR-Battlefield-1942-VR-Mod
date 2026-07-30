#include "stereo/OffHandWeaponSteeringMath.h"

#include <algorithm>
#include <cmath>

namespace bfvr::stereo
{

namespace
{

bool IsFinite(const Matrix4& matrix) noexcept
{
    for (const auto& row : matrix.values)
    {
        for (const float value : row)
        {
            if (!std::isfinite(value))
            {
                return false;
            }
        }
    }
    return true;
}

Matrix4 Multiply(
    const Matrix4& left,
    const Matrix4& right) noexcept
{
    Matrix4 result = {};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            for (std::size_t inner = 0; inner < 4; ++inner)
            {
                result.values[row][column] +=
                    left.values[row][inner] *
                    right.values[inner][column];
            }
        }
    }
    return result;
}

struct Direction
{
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

float Dot(
    const Direction& left,
    const Direction& right) noexcept
{
    return left.x * right.x +
        left.y * right.y +
        left.z * right.z;
}

Direction Cross(
    const Direction& left,
    const Direction& right) noexcept
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x};
}

bool Normalize(
    Direction& direction,
    const float minimumLength) noexcept
{
    const float lengthSquared = Dot(direction, direction);
    if (!std::isfinite(lengthSquared) ||
        lengthSquared < minimumLength * minimumLength)
    {
        return false;
    }
    const float inverseLength =
        1.0F / std::sqrt(lengthSquared);
    direction.x *= inverseLength;
    direction.y *= inverseLength;
    direction.z *= inverseLength;
    return std::isfinite(direction.x) &&
        std::isfinite(direction.y) &&
        std::isfinite(direction.z);
}

Matrix4 MakeRowVectorAxisRotation(
    const Direction& axis,
    const float angleRadians) noexcept
{
    const float sine = std::sin(angleRadians);
    const float cosine = std::cos(angleRadians);
    const float oneMinusCosine = 1.0F - cosine;
    const float x = axis.x;
    const float y = axis.y;
    const float z = axis.z;

    Matrix4 result = {};
    result.values[0][0] = cosine + x * x * oneMinusCosine;
    result.values[0][1] = x * y * oneMinusCosine + z * sine;
    result.values[0][2] = x * z * oneMinusCosine - y * sine;
    result.values[1][0] = y * x * oneMinusCosine - z * sine;
    result.values[1][1] = cosine + y * y * oneMinusCosine;
    result.values[1][2] = y * z * oneMinusCosine + x * sine;
    result.values[2][0] = z * x * oneMinusCosine + y * sine;
    result.values[2][1] = z * y * oneMinusCosine - x * sine;
    result.values[2][2] = cosine + z * z * oneMinusCosine;
    result.values[3][3] = 1.0F;
    return result;
}

} // namespace

std::optional<OffHandWeaponSteeringResult>
ComputeBoundedOffHandWeaponSteering(
    const Matrix4& controllerGunWorld,
    const Matrix4& predictedSupportWorld,
    const Matrix4& trackedLeftHandWorld,
    const float maximumSwingRadians,
    const float worldUnitsPerMetre) noexcept
{
    constexpr float kPi = 3.14159265358979323846F;
    constexpr float kMinimumSupportSpanMetres = 0.05F;
    constexpr float kDirectionEpsilon = 0.000001F;
    if (!IsFinite(controllerGunWorld) ||
        !IsFinite(predictedSupportWorld) ||
        !IsFinite(trackedLeftHandWorld) ||
        !std::isfinite(maximumSwingRadians) ||
        maximumSwingRadians < 0.0F ||
        maximumSwingRadians > kPi ||
        !std::isfinite(worldUnitsPerMetre) ||
        worldUnitsPerMetre <= 0.0F)
    {
        return std::nullopt;
    }

    const float pivotX = controllerGunWorld.values[3][0];
    const float pivotY = controllerGunWorld.values[3][1];
    const float pivotZ = controllerGunWorld.values[3][2];
    Direction authored = {
        predictedSupportWorld.values[3][0] - pivotX,
        predictedSupportWorld.values[3][1] - pivotY,
        predictedSupportWorld.values[3][2] - pivotZ};
    Direction tracked = {
        trackedLeftHandWorld.values[3][0] - pivotX,
        trackedLeftHandWorld.values[3][1] - pivotY,
        trackedLeftHandWorld.values[3][2] - pivotZ};
    const float minimumSpan =
        kMinimumSupportSpanMetres * worldUnitsPerMetre;
    if (!Normalize(authored, minimumSpan) ||
        !Normalize(tracked, minimumSpan))
    {
        return std::nullopt;
    }

    const float cosine =
        std::clamp(Dot(authored, tracked), -1.0F, 1.0F);
    const float requestedAngle = std::acos(cosine);
    if (!std::isfinite(requestedAngle))
    {
        return std::nullopt;
    }
    const float appliedAngle =
        std::min(requestedAngle, maximumSwingRadians);
    if (appliedAngle <= kDirectionEpsilon)
    {
        return OffHandWeaponSteeringResult{
            controllerGunWorld,
            requestedAngle,
            0.0F};
    }

    Direction axis = Cross(authored, tracked);
    if (!Normalize(axis, kDirectionEpsilon))
    {
        // The nearly opposite case has no unique minimal swing axis.
        return std::nullopt;
    }
    const Matrix4 swing =
        MakeRowVectorAxisRotation(axis, appliedAngle);
    Matrix4 result = Multiply(controllerGunWorld, swing);
    // Rotation is about the right-grip pivot, not the world origin.
    result.values[3][0] = pivotX;
    result.values[3][1] = pivotY;
    result.values[3][2] = pivotZ;
    result.values[3][3] = controllerGunWorld.values[3][3];
    return IsFinite(result)
        ? std::optional<OffHandWeaponSteeringResult>(
              OffHandWeaponSteeringResult{
                  result,
                  requestedAngle,
                  appliedAngle})
        : std::nullopt;
}

} // namespace bfvr::stereo
