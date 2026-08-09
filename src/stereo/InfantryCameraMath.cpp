#include "stereo/InfantryCameraMath.h"

#include <cmath>

namespace
{
using bfvr::stereo::Matrix4;

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

bool IsAffine(const Matrix4& matrix) noexcept
{
    constexpr float tolerance = 0.001F;
    return IsFinite(matrix) &&
        std::fabs(matrix.values[0][3]) <= tolerance &&
        std::fabs(matrix.values[1][3]) <= tolerance &&
        std::fabs(matrix.values[2][3]) <= tolerance &&
        std::fabs(matrix.values[3][3] - 1.0F) <= tolerance;
}

float NormalizeRadians(float radians) noexcept
{
    constexpr float pi = 3.14159265358979323846F;
    constexpr float twoPi = pi * 2.0F;
    while (radians > pi)
    {
        radians -= twoPi;
    }
    while (radians < -pi)
    {
        radians += twoPi;
    }
    return radians;
}

std::optional<float> ReadHorizontalFacingYaw(
    const Matrix4& soldierBodyWorld) noexcept
{
    if (!IsAffine(soldierBodyWorld))
    {
        return std::nullopt;
    }
    const float forwardX = soldierBodyWorld.values[2][0];
    const float forwardZ = soldierBodyWorld.values[2][2];
    const float horizontalLength = std::hypot(forwardX, forwardZ);
    if (!std::isfinite(horizontalLength) || horizontalLength < 0.5F)
    {
        return std::nullopt;
    }
    return std::atan2(forwardX, forwardZ);
}

Matrix4 MakeCameraFromYaw(
    const Matrix4& sourceCameraWorld,
    float yaw) noexcept
{
    const float forwardX = std::sin(yaw);
    const float forwardZ = std::cos(yaw);
    Matrix4 result = {};
    result.values[0][0] = forwardZ;
    result.values[0][2] = -forwardX;
    result.values[1][1] = 1.0F;
    result.values[2][0] = forwardX;
    result.values[2][2] = forwardZ;
    result.values[3][0] = sourceCameraWorld.values[3][0];
    result.values[3][1] = sourceCameraWorld.values[3][1];
    result.values[3][2] = sourceCameraWorld.values[3][2];
    result.values[3][3] = 1.0F;
    return result;
}

} // namespace

namespace bfvr::stereo
{

std::optional<Matrix4> MakeD3D8InfantryComfortCamera(
    const Matrix4& sourceCameraWorld,
    const Matrix4& soldierBodyWorld) noexcept
{
    if (!IsAffine(sourceCameraWorld))
    {
        return std::nullopt;
    }
    const auto yaw = ReadHorizontalFacingYaw(soldierBodyWorld);
    if (!yaw.has_value())
    {
        return std::nullopt;
    }
    return MakeCameraFromYaw(sourceCameraWorld, *yaw);
}

std::optional<Matrix4> MakeD3D8FilteredInfantryComfortCamera(
    const Matrix4& sourceCameraWorld,
    const Matrix4& soldierBodyWorld,
    bool suppressObservedYawDelta,
    InfantryCameraHeadingState& headingState) noexcept
{
    if (!IsAffine(sourceCameraWorld))
    {
        return std::nullopt;
    }
    const auto observedYaw = ReadHorizontalFacingYaw(soldierBodyWorld);
    if (!observedYaw.has_value())
    {
        return std::nullopt;
    }

    InfantryCameraHeadingState next = headingState;
    if (!next.initialized || !std::isfinite(next.presentedYaw) ||
        !std::isfinite(next.observedYaw))
    {
        next.presentedYaw = *observedYaw;
        next.observedYaw = *observedYaw;
        next.initialized = true;
    }
    else
    {
        const float observedDelta = NormalizeRadians(
            *observedYaw - next.observedYaw);
        next.observedYaw = *observedYaw;
        if (!suppressObservedYawDelta)
        {
            next.presentedYaw = NormalizeRadians(
                next.presentedYaw + observedDelta);
        }
    }
    headingState = next;
    return MakeCameraFromYaw(sourceCameraWorld, next.presentedYaw);
}

} // namespace bfvr::stereo
