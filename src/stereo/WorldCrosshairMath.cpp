#include "stereo/WorldCrosshairMath.h"

#include <algorithm>
#include <cmath>

namespace
{

using bfvr::stereo::Matrix4;
using bfvr::stereo::Vec3;

constexpr float kRigidTolerance = 0.01F;
constexpr float kMinimumClipW = 0.0001F;
constexpr float kPi = 3.14159265358979323846F;

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

bool IsRigid(const Matrix4& matrix) noexcept
{
    if (!IsFinite(matrix) ||
        std::fabs(matrix.values[0][3]) > kRigidTolerance ||
        std::fabs(matrix.values[1][3]) > kRigidTolerance ||
        std::fabs(matrix.values[2][3]) > kRigidTolerance ||
        std::fabs(matrix.values[3][3] - 1.0F) > kRigidTolerance)
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
        if (!IsFinite(lengthSquared) ||
            std::fabs(lengthSquared - 1.0F) > kRigidTolerance)
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
            if (!IsFinite(dot) || std::fabs(dot) > kRigidTolerance)
            {
                return false;
            }
        }
    }
    return true;
}

} // namespace

namespace bfvr::stereo
{

bool IsWorldCrosshairGadgetItemIndex(int itemIndex) noexcept
{
    return itemIndex == 4 || itemIndex == 5 || itemIndex == 6;
}

WorldCrosshairAimSource SelectWorldCrosshairAimSource(
    const WorldCrosshairEligibility& eligibility) noexcept
{
    if (!eligibility.localPlayerAlive ||
        !eligibility.controlObjectsReadable)
    {
        return WorldCrosshairAimSource::None;
    }
    if (!eligibility.currentIsDefaultControlObject)
    {
        return eligibility.nativeCrosshairRequested &&
                eligibility.mountedFirePoseReadable
            ? WorldCrosshairAimSource::MountedWeapon
            : WorldCrosshairAimSource::None;
    }
    return eligibility.nativeArmPoseFresh &&
            IsWorldCrosshairGadgetItemIndex(eligibility.activeItemIndex)
        ? WorldCrosshairAimSource::GadgetController
        : WorldCrosshairAimSource::None;
}

std::optional<Vec3> MakeWorldCrosshairEndpointFromFirePose(
    const Matrix4& firePose,
    float maximumDistance) noexcept
{
    if (!IsRigid(firePose) || !IsFinite(maximumDistance) ||
        maximumDistance <= 0.0F)
    {
        return std::nullopt;
    }
    const Vec3 result = {
        firePose.values[3][0] +
            firePose.values[2][0] * maximumDistance,
        firePose.values[3][1] +
            firePose.values[2][1] * maximumDistance,
        firePose.values[3][2] +
            firePose.values[2][2] * maximumDistance};
    return IsFinite(result.x) && IsFinite(result.y) && IsFinite(result.z)
        ? std::optional<Vec3>(result)
        : std::nullopt;
}

std::optional<WorldCrosshairProjection> ProjectWorldCrosshairEndpoint(
    const Vec3& endpoint,
    const Matrix4& eyeView,
    const Matrix4& eyeProjection,
    float viewportWidth,
    float viewportHeight,
    float angularDiameterDegrees) noexcept
{
    if (!IsFinite(endpoint.x) || !IsFinite(endpoint.y) ||
        !IsFinite(endpoint.z) || !IsFinite(eyeView) ||
        !IsFinite(eyeProjection) || !IsFinite(viewportWidth) ||
        !IsFinite(viewportHeight) ||
        !IsFinite(angularDiameterDegrees) || viewportWidth <= 0.0F ||
        viewportHeight <= 0.0F || angularDiameterDegrees <= 0.0F ||
        angularDiameterDegrees >= 90.0F)
    {
        return std::nullopt;
    }
    const Vec4 viewPosition = TransformRowVector(
        {endpoint.x, endpoint.y, endpoint.z, 1.0F},
        eyeView);
    const Vec4 clip = TransformRowVector(viewPosition, eyeProjection);
    if (!IsFinite(clip.x) || !IsFinite(clip.y) ||
        !IsFinite(clip.z) || !IsFinite(clip.w) || clip.w <= kMinimumClipW)
    {
        return std::nullopt;
    }
    const float inverseW = 1.0F / clip.w;
    const float normalizedX = clip.x * inverseW;
    const float normalizedY = clip.y * inverseW;
    const float depth = clip.z * inverseW;
    if (!IsFinite(normalizedX) || !IsFinite(normalizedY) ||
        !IsFinite(depth) || depth < 0.0F || depth > 1.0F)
    {
        return std::nullopt;
    }
    const float halfAngleRadians =
        angularDiameterDegrees * kPi / 360.0F;
    const float halfExtentPixels =
        std::tan(halfAngleRadians) *
        std::fabs(eyeProjection.values[1][1]) *
        viewportHeight * 0.5F;
    if (!IsFinite(halfExtentPixels) || halfExtentPixels <= 0.0F)
    {
        return std::nullopt;
    }
    return WorldCrosshairProjection{
        (normalizedX + 1.0F) * viewportWidth * 0.5F - 0.5F,
        (1.0F - normalizedY) * viewportHeight * 0.5F - 0.5F,
        depth,
        std::clamp(halfExtentPixels, 4.0F, 256.0F)};
}

} // namespace bfvr::stereo
