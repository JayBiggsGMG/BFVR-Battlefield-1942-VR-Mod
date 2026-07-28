#include "stereo/UiPointerMath.h"

#include <algorithm>
#include <cmath>

namespace
{

using bfvr::stereo::Pose;
using bfvr::stereo::Quaternion;
using bfvr::stereo::Vec3;

bool IsFinite(float value) noexcept
{
    return std::isfinite(value);
}

bool IsFinite(const Vec3& value) noexcept
{
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
}

bool IsFinite(const Quaternion& value) noexcept
{
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z) &&
        IsFinite(value.w);
}

std::optional<Quaternion> Normalize(const Quaternion& value) noexcept
{
    if (!IsFinite(value))
    {
        return std::nullopt;
    }
    const float lengthSquared =
        value.x * value.x + value.y * value.y +
        value.z * value.z + value.w * value.w;
    if (!IsFinite(lengthSquared) || lengthSquared <= 0.000001F)
    {
        return std::nullopt;
    }
    const float reciprocalLength = 1.0F / std::sqrt(lengthSquared);
    if (!IsFinite(reciprocalLength))
    {
        return std::nullopt;
    }
    return Quaternion{
        value.x * reciprocalLength,
        value.y * reciprocalLength,
        value.z * reciprocalLength,
        value.w * reciprocalLength};
}

Quaternion Conjugate(const Quaternion& value) noexcept
{
    return {-value.x, -value.y, -value.z, value.w};
}

Quaternion Multiply(
    const Quaternion& lhs,
    const Quaternion& rhs) noexcept
{
    return {
        lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.w * rhs.y - lhs.x * rhs.z + lhs.y * rhs.w + lhs.z * rhs.x,
        lhs.w * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.w,
        lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z};
}

Vec3 Subtract(const Vec3& lhs, const Vec3& rhs) noexcept
{
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

Vec3 Scale(const Vec3& value, float scalar) noexcept
{
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

Vec3 Add(const Vec3& lhs, const Vec3& rhs) noexcept
{
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

Vec3 Cross(const Vec3& lhs, const Vec3& rhs) noexcept
{
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x};
}

Vec3 Rotate(const Quaternion& orientation, const Vec3& value) noexcept
{
    const Vec3 axis{orientation.x, orientation.y, orientation.z};
    const Vec3 doubledCross = Scale(Cross(axis, value), 2.0F);
    return Add(
        value,
        Add(Scale(doubledCross, orientation.w), Cross(axis, doubledCross)));
}

std::optional<float> ExtractYawRadians(const Quaternion& orientation) noexcept
{
    const auto normalized = Normalize(orientation);
    if (!normalized.has_value())
    {
        return std::nullopt;
    }
    const float sinYaw = 2.0F *
        (normalized->w * normalized->y + normalized->x * normalized->z);
    const float cosYaw = 1.0F - 2.0F *
        (normalized->y * normalized->y + normalized->z * normalized->z);
    const float yaw = std::atan2(sinYaw, cosYaw);
    return IsFinite(yaw) ? std::optional<float>(yaw) : std::nullopt;
}

Quaternion MakeYawQuaternion(float yaw) noexcept
{
    const float halfYaw = yaw * 0.5F;
    return {0.0F, std::sin(halfYaw), 0.0F, std::cos(halfYaw)};
}

float WrapRadians(float angle) noexcept
{
    constexpr float kPi = 3.14159265358979323846F;
    constexpr float kTwoPi = 2.0F * kPi;
    while (angle > kPi)
    {
        angle -= kTwoPi;
    }
    while (angle < -kPi)
    {
        angle += kTwoPi;
    }
    return angle;
}

struct AspectFitRect
{
    float left = 0.0F;
    float top = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

std::optional<AspectFitRect> MakeAspectFitRect(
    std::uint32_t destinationWidth,
    std::uint32_t destinationHeight,
    std::uint32_t sourceWidth,
    std::uint32_t sourceHeight) noexcept
{
    if (destinationWidth == 0 || destinationHeight == 0 ||
        sourceWidth == 0 || sourceHeight == 0)
    {
        return std::nullopt;
    }
    const float destinationWidthFloat =
        static_cast<float>(destinationWidth);
    const float destinationHeightFloat =
        static_cast<float>(destinationHeight);
    const float sourceAspect =
        static_cast<float>(sourceWidth) / static_cast<float>(sourceHeight);
    const float destinationAspect =
        destinationWidthFloat / destinationHeightFloat;
    if (!IsFinite(sourceAspect) || !IsFinite(destinationAspect) ||
        sourceAspect <= 0.0F || destinationAspect <= 0.0F)
    {
        return std::nullopt;
    }

    AspectFitRect result = {};
    if (sourceAspect >= destinationAspect)
    {
        result.width = destinationWidthFloat;
        result.height = destinationWidthFloat / sourceAspect;
        result.top = (destinationHeightFloat - result.height) * 0.5F;
    }
    else
    {
        result.height = destinationHeightFloat;
        result.width = destinationHeightFloat * sourceAspect;
        result.left = (destinationWidthFloat - result.width) * 0.5F;
    }
    return IsFinite(result.left) && IsFinite(result.top) &&
            IsFinite(result.width) && IsFinite(result.height) &&
            result.width > 0.0F && result.height > 0.0F
        ? std::optional<AspectFitRect>(result)
        : std::nullopt;
}

} // namespace

namespace bfvr::stereo
{

std::optional<Pose> MakeYawOnlyUiAnchor(
    const Pose& headPose) noexcept
{
    if (!IsFinite(headPose.position))
    {
        return std::nullopt;
    }
    const auto yaw = ExtractYawRadians(headPose.orientation);
    if (!yaw.has_value())
    {
        return std::nullopt;
    }
    return Pose{headPose.position, MakeYawQuaternion(*yaw)};
}

bool UpdateUiMenuAnchor(
    UiMenuAnchorTracker& tracker,
    const Pose& headPose,
    std::int64_t predictedDisplayTime,
    float followStartRadians,
    float followRadiansPerSecond) noexcept
{
    if (predictedDisplayTime <= 0 ||
        !IsFinite(followStartRadians) ||
        !IsFinite(followRadiansPerSecond) ||
        followStartRadians < 0.0F ||
        followRadiansPerSecond <= 0.0F)
    {
        return false;
    }
    const auto headAnchor = MakeYawOnlyUiAnchor(headPose);
    if (!headAnchor.has_value())
    {
        return false;
    }
    if (!tracker.valid)
    {
        tracker.anchor = *headAnchor;
        tracker.lastPredictedDisplayTime = predictedDisplayTime;
        tracker.valid = true;
        return true;
    }

    const auto currentYaw = ExtractYawRadians(tracker.anchor.orientation);
    const auto targetYaw = ExtractYawRadians(headAnchor->orientation);
    if (!currentYaw.has_value() || !targetYaw.has_value())
    {
        return false;
    }
    const std::int64_t elapsedNanoseconds =
        predictedDisplayTime - tracker.lastPredictedDisplayTime;
    tracker.lastPredictedDisplayTime = predictedDisplayTime;
    if (elapsedNanoseconds <= 0)
    {
        return true;
    }
    const float elapsedSeconds = std::clamp(
        static_cast<float>(elapsedNanoseconds) * 0.000000001F,
        0.0F,
        0.100F);
    const float yawDelta = WrapRadians(*targetYaw - *currentYaw);
    if (std::fabs(yawDelta) <= followStartRadians || elapsedSeconds <= 0.0F)
    {
        return true;
    }
    const float maximumStep = followRadiansPerSecond * elapsedSeconds;
    const float appliedStep = std::clamp(
        yawDelta,
        -maximumStep,
        maximumStep);
    const float newYaw = WrapRadians(*currentYaw + appliedStep);
    tracker.anchor.position = headAnchor->position;
    tracker.anchor.orientation = MakeYawQuaternion(newYaw);
    return true;
}

void ResetUiMenuAnchor(UiMenuAnchorTracker& tracker) noexcept
{
    tracker = {};
}

std::optional<Pose> MakePoseRelativeToReference(
    const Pose& reference,
    const Pose& current) noexcept
{
    if (!IsFinite(reference.position) || !IsFinite(current.position))
    {
        return std::nullopt;
    }
    const auto referenceOrientation = Normalize(reference.orientation);
    const auto currentOrientation = Normalize(current.orientation);
    if (!referenceOrientation.has_value() ||
        !currentOrientation.has_value())
    {
        return std::nullopt;
    }
    const Quaternion inverseReference = Conjugate(*referenceOrientation);
    Pose relative = {};
    relative.position = Rotate(
        inverseReference,
        Subtract(current.position, reference.position));
    relative.orientation =
        Multiply(inverseReference, *currentOrientation);
    return IsFinite(relative.position) && IsFinite(relative.orientation)
        ? std::optional<Pose>(relative)
        : std::nullopt;
}

std::optional<UiCanvasPoint> MapOpenXRAimPoseToAspectFitUiCanvas(
    const Pose& aimPose,
    const Pose& quadPose,
    float quadWidthMeters,
    float quadHeightMeters,
    std::uint32_t uiTextureWidth,
    std::uint32_t uiTextureHeight,
    std::uint32_t sourceTextureWidth,
    std::uint32_t sourceTextureHeight,
    std::uint32_t logicalCanvasWidth,
    std::uint32_t logicalCanvasHeight) noexcept
{
    if (!IsFinite(aimPose.position) || !IsFinite(quadPose.position) ||
        !IsFinite(quadWidthMeters) || !IsFinite(quadHeightMeters) ||
        quadWidthMeters <= 0.0F || quadHeightMeters <= 0.0F ||
        logicalCanvasWidth == 0 || logicalCanvasHeight == 0)
    {
        return std::nullopt;
    }
    const auto aimOrientation = Normalize(aimPose.orientation);
    const auto quadOrientation = Normalize(quadPose.orientation);
    const auto aspectFit = MakeAspectFitRect(
        uiTextureWidth,
        uiTextureHeight,
        sourceTextureWidth,
        sourceTextureHeight);
    if (!aimOrientation.has_value() || !quadOrientation.has_value() ||
        !aspectFit.has_value())
    {
        return std::nullopt;
    }

    // OpenXR defines -Z as forward for the aim pose. Work in quad-local space
    // so the same mapping remains correct if the panel is later repositioned.
    const Vec3 worldDirection =
        Rotate(*aimOrientation, {0.0F, 0.0F, -1.0F});
    const Quaternion inverseQuad = Conjugate(*quadOrientation);
    const Vec3 localOrigin =
        Rotate(inverseQuad, Subtract(aimPose.position, quadPose.position));
    const Vec3 localDirection = Rotate(inverseQuad, worldDirection);
    constexpr float kParallelEpsilon = 0.000001F;
    if (!IsFinite(localOrigin) || !IsFinite(localDirection) ||
        localDirection.z >= -kParallelEpsilon)
    {
        return std::nullopt;
    }
    const float distanceAlongRay = -localOrigin.z / localDirection.z;
    if (!IsFinite(distanceAlongRay) || distanceAlongRay < 0.0F)
    {
        return std::nullopt;
    }
    const Vec3 hit = Add(
        localOrigin,
        Scale(localDirection, distanceAlongRay));
    const float quadU = hit.x / quadWidthMeters + 0.5F;
    const float quadV = 0.5F - hit.y / quadHeightMeters;
    if (!IsFinite(quadU) || !IsFinite(quadV) ||
        quadU < 0.0F || quadU > 1.0F ||
        quadV < 0.0F || quadV > 1.0F)
    {
        return std::nullopt;
    }

    const float textureX = quadU * static_cast<float>(uiTextureWidth);
    const float textureY = quadV * static_cast<float>(uiTextureHeight);
    const float sourceU =
        (textureX - aspectFit->left) / aspectFit->width;
    const float sourceV =
        (textureY - aspectFit->top) / aspectFit->height;
    if (!IsFinite(sourceU) || !IsFinite(sourceV) ||
        sourceU < 0.0F || sourceU > 1.0F ||
        sourceV < 0.0F || sourceV > 1.0F)
    {
        return std::nullopt;
    }

    UiCanvasPoint result = {};
    result.normalizedX = std::clamp(sourceU, 0.0F, 1.0F);
    result.normalizedY = std::clamp(sourceV, 0.0F, 1.0F);
    result.pixelX =
        result.normalizedX * static_cast<float>(logicalCanvasWidth);
    result.pixelY =
        result.normalizedY * static_cast<float>(logicalCanvasHeight);
    return result;
}

} // namespace bfvr::stereo
