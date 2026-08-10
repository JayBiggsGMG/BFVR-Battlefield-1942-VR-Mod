#pragma once

#include "stereo/StereoMath.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>

namespace bfvr::native_arm_math
{

using Matrix4 = stereo::Matrix4;

inline bool IsFinite(const Matrix4& matrix) noexcept
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

inline Matrix4 Multiply(const Matrix4& left, const Matrix4& right) noexcept
{
    Matrix4 result = {};
    for (std::size_t row = 0; row < result.values.size(); ++row)
    {
        for (std::size_t column = 0;
             column < result.values[row].size();
             ++column)
        {
            for (std::size_t inner = 0; inner < result.values.size(); ++inner)
            {
                result.values[row][column] +=
                    left.values[row][inner] * right.values[inner][column];
            }
        }
    }
    return result;
}

inline Matrix4 IdentityMatrix() noexcept
{
    Matrix4 result = {};
    for (std::size_t index = 0; index < result.values.size(); ++index)
    {
        result.values[index][index] = 1.0F;
    }
    return result;
}

inline std::optional<Matrix4> Invert(const Matrix4& matrix) noexcept
{
    if (!IsFinite(matrix))
    {
        return std::nullopt;
    }
    std::array<std::array<float, 8>, 4> augmented = {};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            augmented[row][column] = matrix.values[row][column];
            augmented[row][column + 4] = row == column ? 1.0F : 0.0F;
        }
    }

    constexpr float pivotEpsilon = 0.000001F;
    for (std::size_t column = 0; column < 4; ++column)
    {
        std::size_t pivotRow = column;
        for (std::size_t candidate = column + 1; candidate < 4; ++candidate)
        {
            if (std::fabs(augmented[candidate][column]) >
                std::fabs(augmented[pivotRow][column]))
            {
                pivotRow = candidate;
            }
        }
        const float pivot = augmented[pivotRow][column];
        if (!std::isfinite(pivot) || std::fabs(pivot) <= pivotEpsilon)
        {
            return std::nullopt;
        }
        if (pivotRow != column)
        {
            std::swap(augmented[pivotRow], augmented[column]);
        }
        for (float& value : augmented[column])
        {
            value /= pivot;
        }
        for (std::size_t row = 0; row < 4; ++row)
        {
            if (row == column)
            {
                continue;
            }
            const float factor = augmented[row][column];
            for (std::size_t index = 0; index < augmented[row].size(); ++index)
            {
                augmented[row][index] -=
                    factor * augmented[column][index];
            }
        }
    }

    Matrix4 inverse = {};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            inverse.values[row][column] = augmented[row][column + 4];
        }
    }
    return IsFinite(inverse) ? std::optional<Matrix4>(inverse) : std::nullopt;
}

inline std::optional<float> ExtractBodyYaw(const Matrix4& matrix) noexcept
{
    const float forwardX = matrix.values[2][0];
    const float forwardZ = matrix.values[2][2];
    const float horizontalLength = std::hypot(forwardX, forwardZ);
    if (!std::isfinite(horizontalLength) || horizontalLength < 0.5F)
    {
        return std::nullopt;
    }
    const float yaw = std::atan2(forwardX, forwardZ);
    return std::isfinite(yaw)
        ? std::optional<float>(yaw)
        : std::nullopt;
}

inline float DistanceSquared(
    const std::array<float, 3>& left,
    const std::array<float, 3>& right) noexcept
{
    const float x = left[0] - right[0];
    const float y = left[1] - right[1];
    const float z = left[2] - right[2];
    return x * x + y * y + z * z;
}

} // namespace bfvr::native_arm_math
