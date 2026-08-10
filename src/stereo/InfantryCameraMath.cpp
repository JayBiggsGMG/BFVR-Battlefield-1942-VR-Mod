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

std::optional<Matrix4> MakeD3D8InfantryPresentationCamera(
    const Matrix4& sourceCameraWorld,
    const float presentationYawRadians) noexcept
{
    if (!IsAffine(sourceCameraWorld) ||
        !std::isfinite(presentationYawRadians))
    {
        return std::nullopt;
    }
    return MakeCameraFromYaw(
        sourceCameraWorld,
        NormalizeRadians(presentationYawRadians));
}

} // namespace bfvr::stereo
