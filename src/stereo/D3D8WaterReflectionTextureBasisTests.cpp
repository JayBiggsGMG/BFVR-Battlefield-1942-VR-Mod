#include "stereo/D3D8WaterReflectionTextureBasis.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
using bfvr::stereo::Matrix4;
using bfvr::stereo::Vec4;

constexpr float kTolerance = 0.0001F;

Matrix4 Identity() noexcept
{
    Matrix4 result = {};
    for (std::size_t index = 0; index < 4; ++index)
    {
        result.values[index][index] = 1.0F;
    }
    return result;
}

Matrix4 Yaw(float radians) noexcept
{
    Matrix4 result = Identity();
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    result.values[0][0] = cosine;
    result.values[0][2] = -sine;
    result.values[2][0] = sine;
    result.values[2][2] = cosine;
    return result;
}

Matrix4 Pitch(float radians) noexcept
{
    Matrix4 result = Identity();
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    result.values[1][1] = cosine;
    result.values[1][2] = sine;
    result.values[2][1] = -sine;
    result.values[2][2] = cosine;
    return result;
}

Matrix4 Multiply(const Matrix4& left, const Matrix4& right) noexcept
{
    Matrix4 result = {};
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

Matrix4 TransposeRotation(const Matrix4& matrix) noexcept
{
    Matrix4 result = Identity();
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            result.values[row][column] = matrix.values[column][row];
        }
    }
    return result;
}

Vec4 TransformRotation(const Vec4& value, const Matrix4& matrix) noexcept
{
    Vec4 result = {};
    result.x = value.x * matrix.values[0][0] +
        value.y * matrix.values[1][0] +
        value.z * matrix.values[2][0];
    result.y = value.x * matrix.values[0][1] +
        value.y * matrix.values[1][1] +
        value.z * matrix.values[2][1];
    result.z = value.x * matrix.values[0][2] +
        value.y * matrix.values[1][2] +
        value.z * matrix.values[2][2];
    result.w = 1.0F;
    return result;
}

bool NearlyEqual(float first, float second) noexcept
{
    return std::fabs(first - second) <= kTolerance;
}

bool NearlyEqual(const Vec4& first, const Vec4& second) noexcept
{
    return NearlyEqual(first.x, second.x) &&
        NearlyEqual(first.y, second.y) &&
        NearlyEqual(first.z, second.z) &&
        NearlyEqual(first.w, second.w);
}

bool NearlyEqual(const Matrix4& first, const Matrix4& second) noexcept
{
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            if (!NearlyEqual(
                    first.values[row][column],
                    second.values[row][column]))
            {
                return false;
            }
        }
    }
    return true;
}

bool TestIdentityPreservesOriginalTextureTransform() noexcept
{
    Matrix4 texture = Identity();
    texture.values[0][0] = 0.7F;
    texture.values[1][2] = -0.4F;
    texture.values[3][0] = 0.25F;
    const auto result =
        bfvr::stereo::MakeD3D8WaterReflectionTextureTransform(
            Identity(),
            Identity(),
            texture);
    return result.has_value() && NearlyEqual(*result, texture);
}

bool TestTranslationDoesNotEnterDirectionBasis() noexcept
{
    Matrix4 logicalCamera = Identity();
    logicalCamera.values[3][0] = 500.0F;
    logicalCamera.values[3][1] = -25.0F;
    logicalCamera.values[3][2] = 4.0F;
    Matrix4 replayView = Identity();
    replayView.values[3][0] = -0.032F;
    replayView.values[3][1] = 1.5F;
    replayView.values[3][2] = -300.0F;
    Matrix4 texture = Identity();
    texture.values[2][0] = 0.33F;
    texture.values[3][1] = 0.5F;
    const auto result =
        bfvr::stereo::MakeD3D8WaterReflectionTextureTransform(
            logicalCamera,
            replayView,
            texture);
    return result.has_value() && NearlyEqual(*result, texture);
}

bool TestReplayCoordinatesMapToLogicalCameraBasis() noexcept
{
    Matrix4 logicalCamera = Yaw(0.61F);
    logicalCamera.values[3][0] = 12.0F;
    logicalCamera.values[3][2] = -8.0F;
    Matrix4 replayView = Multiply(Pitch(-0.42F), Yaw(0.18F));
    replayView.values[3][0] = -0.032F;
    replayView.values[3][1] = 2.0F;

    Matrix4 texture = Identity();
    texture.values[0][0] = 0.75F;
    texture.values[0][1] = -0.2F;
    texture.values[1][2] = 0.6F;
    texture.values[2][0] = 0.15F;
    texture.values[3][0] = 0.3F;
    texture.values[3][1] = 0.4F;

    const auto corrected =
        bfvr::stereo::MakeD3D8WaterReflectionTextureTransform(
            logicalCamera,
            replayView,
            texture);
    if (!corrected.has_value())
    {
        return false;
    }

    const Vec4 worldDirection = {0.31F, -0.72F, 0.62F, 0.0F};
    const Vec4 replayCoordinate =
        TransformRotation(worldDirection, replayView);
    const Vec4 logicalCoordinate = TransformRotation(
        worldDirection,
        TransposeRotation(logicalCamera));
    const Vec4 correctedOutput = bfvr::stereo::TransformRowVector(
        replayCoordinate,
        *corrected);
    const Vec4 expectedOutput = bfvr::stereo::TransformRowVector(
        logicalCoordinate,
        texture);
    return NearlyEqual(correctedOutput, expectedOutput);
}

bool TestInvalidMatricesFailClosed() noexcept
{
    Matrix4 scaledCamera = Identity();
    scaledCamera.values[0][0] = 2.0F;
    Matrix4 nonFiniteTexture = Identity();
    nonFiniteTexture.values[1][1] =
        std::numeric_limits<float>::infinity();
    return !bfvr::stereo::MakeD3D8WaterReflectionTextureTransform(
                scaledCamera,
                Identity(),
                Identity()).has_value() &&
        !bfvr::stereo::MakeD3D8WaterReflectionTextureTransform(
                Identity(),
                Identity(),
                nonFiniteTexture).has_value();
}

} // namespace

int main()
{
    const bool passed =
        TestIdentityPreservesOriginalTextureTransform() &&
        TestTranslationDoesNotEnterDirectionBasis() &&
        TestReplayCoordinatesMapToLogicalCameraBasis() &&
        TestInvalidMatricesFailClosed();
    if (!passed)
    {
        std::fprintf(stderr, "D3D8 water reflection texture-basis tests failed.\n");
        return 1;
    }
    std::printf("D3D8 water reflection texture-basis tests passed.\n");
    return 0;
}
