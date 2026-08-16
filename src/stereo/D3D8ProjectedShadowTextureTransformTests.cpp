#include "stereo/D3D8ProjectedShadowTextureTransform.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
using bfvr::stereo::Matrix4;
using bfvr::stereo::Vec4;

constexpr float kTolerance = 0.0002F;

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

bool PreservesProjectedCoordinate(
    const Matrix4& sourceView,
    const Matrix4& replayView,
    const Matrix4& originalTexture,
    const Matrix4& correctedTexture) noexcept
{
    const Vec4 worldPosition = {31.0F, -7.5F, 118.0F, 1.0F};
    const Vec4 nativeCamera = bfvr::stereo::TransformRowVector(
        worldPosition,
        sourceView);
    const Vec4 expected = bfvr::stereo::TransformRowVector(
        nativeCamera,
        originalTexture);
    const Vec4 replayCamera = bfvr::stereo::TransformRowVector(
        worldPosition,
        replayView);
    const Vec4 actual = bfvr::stereo::TransformRowVector(
        replayCamera,
        correctedTexture);
    return NearlyEqual(actual, expected);
}

bool TestIdentityPreservesOriginalTexture() noexcept
{
    Matrix4 texture = Identity();
    texture.values[0][0] = 0.35F;
    texture.values[1][1] = -0.42F;
    texture.values[2][3] = 0.18F;
    texture.values[3][0] = 0.5F;
    const auto corrected =
        bfvr::stereo::MakeD3D8ProjectedShadowTextureTransform(
            Identity(),
            Identity(),
            texture);
    return corrected.has_value() && NearlyEqual(*corrected, texture);
}

bool TestCameraSpaceChangePreservesProjection() noexcept
{
    Matrix4 sourceView = Multiply(Pitch(-0.17F), Yaw(0.41F));
    sourceView.values[3][0] = -14.0F;
    sourceView.values[3][1] = 2.75F;
    sourceView.values[3][2] = 83.0F;
    Matrix4 replayView = Multiply(Pitch(0.09F), Yaw(0.52F));
    replayView.values[3][0] = -14.032F;
    replayView.values[3][1] = 2.71F;
    replayView.values[3][2] = 82.97F;
    Matrix4 texture = Identity();
    texture.values[0][0] = 0.029F;
    texture.values[0][2] = -0.013F;
    texture.values[1][1] = 0.041F;
    texture.values[2][0] = 0.007F;
    texture.values[2][2] = 0.025F;
    texture.values[3][0] = 0.43F;
    texture.values[3][1] = 0.54F;

    const auto corrected =
        bfvr::stereo::MakeD3D8ProjectedShadowTextureTransform(
            sourceView,
            replayView,
            texture);
    return corrected.has_value() && PreservesProjectedCoordinate(
        sourceView,
        replayView,
        texture,
        *corrected);
}

bool TestStereoEyesReceiveDistinctValidTransforms() noexcept
{
    Matrix4 sourceView = Yaw(0.22F);
    sourceView.values[3][2] = 12.0F;
    Matrix4 leftView = sourceView;
    Matrix4 rightView = sourceView;
    leftView.values[3][0] += 0.032F;
    rightView.values[3][0] -= 0.032F;
    Matrix4 texture = Identity();
    texture.values[0][0] = 0.2F;
    texture.values[1][1] = 0.2F;
    texture.values[3][0] = 0.5F;
    texture.values[3][1] = 0.5F;
    const auto left =
        bfvr::stereo::MakeD3D8ProjectedShadowTextureTransform(
            sourceView,
            leftView,
            texture);
    const auto right =
        bfvr::stereo::MakeD3D8ProjectedShadowTextureTransform(
            sourceView,
            rightView,
            texture);
    return left.has_value() && right.has_value() &&
        !NearlyEqual(*left, *right) &&
        PreservesProjectedCoordinate(sourceView, leftView, texture, *left) &&
        PreservesProjectedCoordinate(sourceView, rightView, texture, *right);
}

bool TestInvalidMatricesFailClosed() noexcept
{
    Matrix4 scaledView = Identity();
    scaledView.values[0][0] = 2.0F;
    Matrix4 nonFiniteTexture = Identity();
    nonFiniteTexture.values[2][1] =
        std::numeric_limits<float>::infinity();
    return !bfvr::stereo::MakeD3D8ProjectedShadowTextureTransform(
                scaledView,
                Identity(),
                Identity()).has_value() &&
        !bfvr::stereo::MakeD3D8ProjectedShadowTextureTransform(
                Identity(),
                Identity(),
                nonFiniteTexture).has_value();
}

bool TestExactWinPcTextureStateContract() noexcept
{
    return bfvr::stereo::IsD3D8ProjectedShadowTextureState(
               0x00020000,
               2) &&
        !bfvr::stereo::IsD3D8ProjectedShadowTextureState(
            0x00020000,
            3) &&
        !bfvr::stereo::IsD3D8ProjectedShadowTextureState(0, 2);
}

} // namespace

int main()
{
    const bool passed =
        TestIdentityPreservesOriginalTexture() &&
        TestCameraSpaceChangePreservesProjection() &&
        TestStereoEyesReceiveDistinctValidTransforms() &&
        TestInvalidMatricesFailClosed() &&
        TestExactWinPcTextureStateContract();
    if (!passed)
    {
        std::fprintf(stderr, "D3D8 projected-shadow texture tests failed.\n");
        return 1;
    }
    std::printf("D3D8 projected-shadow texture tests passed.\n");
    return 0;
}
