#include "stereo/WeaponFireAimMath.h"

#include <cmath>

namespace
{

using bfvr::stereo::Matrix4;

bool IsFinite(float value) noexcept
{
    return std::isfinite(value);
}

bool IsFinite(const Matrix4& matrix) noexcept
{
    for (const auto& row : matrix.values)
    {
        for (float value : row)
        {
            if (!IsFinite(value))
            {
                return false;
            }
        }
    }
    return true;
}

bool IsRigidTransform(const Matrix4& matrix) noexcept
{
    if (!IsFinite(matrix) ||
        std::fabs(matrix.values[0][3]) > 0.001F ||
        std::fabs(matrix.values[1][3]) > 0.001F ||
        std::fabs(matrix.values[2][3]) > 0.001F ||
        std::fabs(matrix.values[3][3] - 1.0F) > 0.001F)
    {
        return false;
    }

    constexpr float kLengthTolerance = 0.02F;
    constexpr float kDotTolerance = 0.02F;
    for (std::size_t row = 0; row < 3; ++row)
    {
        float lengthSquared = 0.0F;
        for (std::size_t column = 0; column < 3; ++column)
        {
            lengthSquared +=
                matrix.values[row][column] * matrix.values[row][column];
        }
        if (std::fabs(lengthSquared - 1.0F) > kLengthTolerance)
        {
            return false;
        }
        for (std::size_t other = row + 1; other < 3; ++other)
        {
            float dot = 0.0F;
            for (std::size_t column = 0; column < 3; ++column)
            {
                dot +=
                    matrix.values[row][column] *
                    matrix.values[other][column];
            }
            if (std::fabs(dot) > kDotTolerance)
            {
                return false;
            }
        }
    }

    const float determinant =
        matrix.values[0][0] *
            (matrix.values[1][1] * matrix.values[2][2] -
             matrix.values[1][2] * matrix.values[2][1]) -
        matrix.values[0][1] *
            (matrix.values[1][0] * matrix.values[2][2] -
             matrix.values[1][2] * matrix.values[2][0]) +
        matrix.values[0][2] *
            (matrix.values[1][0] * matrix.values[2][1] -
             matrix.values[1][1] * matrix.values[2][0]);
    return IsFinite(determinant) && determinant > 0.98F &&
        determinant < 1.02F;
}

void MultiplyRotation(
    const Matrix4& lhs,
    const Matrix4& rhs,
    Matrix4& result) noexcept
{
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            result.values[row][column] = 0.0F;
            for (std::size_t inner = 0; inner < 3; ++inner)
            {
                result.values[row][column] +=
                    lhs.values[row][inner] * rhs.values[inner][column];
            }
        }
    }
}

} // namespace

namespace bfvr::stereo
{

std::optional<Matrix4> MakeD3D8VisualWeaponFireMatrix(
    const Matrix4& nativeFireMatrix,
    const Matrix4& visualWeaponViewOffset) noexcept
{
    if (!IsRigidTransform(nativeFireMatrix) ||
        !IsRigidTransform(visualWeaponViewOffset))
    {
        return std::nullopt;
    }

    Matrix4 result = nativeFireMatrix;
    // The visual row-vector chain applies this view-space attachment before
    // the native player/body orientation. Apply the identical rotation order
    // to the native fire basis while deliberately retaining its origin.
    MultiplyRotation(visualWeaponViewOffset, nativeFireMatrix, result);
    result.values[0][3] = 0.0F;
    result.values[1][3] = 0.0F;
    result.values[2][3] = 0.0F;
    result.values[3][0] = nativeFireMatrix.values[3][0];
    result.values[3][1] = nativeFireMatrix.values[3][1];
    result.values[3][2] = nativeFireMatrix.values[3][2];
    result.values[3][3] = 1.0F;
    return IsRigidTransform(result)
        ? std::optional<Matrix4>(result)
        : std::nullopt;
}

} // namespace bfvr::stereo
