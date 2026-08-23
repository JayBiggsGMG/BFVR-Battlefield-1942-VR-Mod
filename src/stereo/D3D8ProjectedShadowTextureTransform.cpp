#include "stereo/D3D8ProjectedShadowTextureTransform.h"

#include <cmath>

namespace
{
constexpr float kAffineTolerance = 0.01F;
constexpr float kRotationTolerance = 0.02F;

bool IsFinite(const bfvr::stereo::Matrix4& matrix) noexcept
{
    for (const auto& row : matrix.values)
    {
        for (const float element : row)
        {
            if (!std::isfinite(element))
            {
                return false;
            }
        }
    }
    return true;
}

bool IsRigidAffine(const bfvr::stereo::Matrix4& matrix) noexcept
{
    if (!IsFinite(matrix) ||
        std::fabs(matrix.values[0][3]) > kAffineTolerance ||
        std::fabs(matrix.values[1][3]) > kAffineTolerance ||
        std::fabs(matrix.values[2][3]) > kAffineTolerance ||
        std::fabs(matrix.values[3][3] - 1.0F) > kAffineTolerance)
    {
        return false;
    }

    for (std::size_t row = 0; row < 3; ++row)
    {
        float lengthSquared = 0.0F;
        for (std::size_t column = 0; column < 3; ++column)
        {
            lengthSquared +=
                matrix.values[row][column] * matrix.values[row][column];
        }
        if (std::fabs(lengthSquared - 1.0F) > kRotationTolerance)
        {
            return false;
        }
        for (std::size_t other = row + 1; other < 3; ++other)
        {
            float dot = 0.0F;
            for (std::size_t column = 0; column < 3; ++column)
            {
                dot += matrix.values[row][column] *
                    matrix.values[other][column];
            }
            if (std::fabs(dot) > kRotationTolerance)
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
    return std::fabs(std::fabs(determinant) - 1.0F) <=
        kRotationTolerance;
}

bfvr::stereo::Matrix4 Identity() noexcept
{
    bfvr::stereo::Matrix4 result = {};
    for (std::size_t index = 0; index < 4; ++index)
    {
        result.values[index][index] = 1.0F;
    }
    return result;
}

bfvr::stereo::Matrix4 Multiply(
    const bfvr::stereo::Matrix4& left,
    const bfvr::stereo::Matrix4& right) noexcept
{
    bfvr::stereo::Matrix4 result = {};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            for (std::size_t inner = 0; inner < 4; ++inner)
            {
                result.values[row][column] +=
                    left.values[row][inner] * right.values[inner][column];
            }
        }
    }
    return result;
}

std::optional<bfvr::stereo::Matrix4> InvertRigidAffine(
    const bfvr::stereo::Matrix4& matrix) noexcept
{
    if (!IsRigidAffine(matrix))
    {
        return std::nullopt;
    }

    bfvr::stereo::Matrix4 result = Identity();
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            result.values[row][column] = matrix.values[column][row];
        }
    }
    for (std::size_t column = 0; column < 3; ++column)
    {
        result.values[3][column] = -(
            matrix.values[3][0] * result.values[0][column] +
            matrix.values[3][1] * result.values[1][column] +
            matrix.values[3][2] * result.values[2][column]);
    }
    return result;
}

} // namespace

namespace bfvr::stereo
{

std::optional<Matrix4> MakeD3D8ProjectedShadowTextureTransform(
    const Matrix4& sourceView,
    const Matrix4& replayView,
    const Matrix4& originalTextureTransform) noexcept
{
    if (!IsRigidAffine(sourceView) ||
        !IsRigidAffine(replayView) ||
        !IsFinite(originalTextureTransform))
    {
        return std::nullopt;
    }

    const auto inverseReplay = InvertRigidAffine(replayView);
    if (!inverseReplay.has_value())
    {
        return std::nullopt;
    }

    const Matrix4 corrected = Multiply(
        Multiply(*inverseReplay, sourceView),
        originalTextureTransform);
    return IsFinite(corrected)
        ? std::optional<Matrix4>(corrected)
        : std::nullopt;
}

} // namespace bfvr::stereo
