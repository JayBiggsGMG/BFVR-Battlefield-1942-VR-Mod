#include "stereo/QuickMenuMirrorMath.h"

#include <algorithm>
#include <cmath>

namespace
{
using bfvr::stereo::Quaternion;
using bfvr::stereo::Vec3;

Vec3 Add(const Vec3& lhs, const Vec3& rhs) noexcept
{
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

Vec3 Subtract(const Vec3& lhs, const Vec3& rhs) noexcept
{
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

Vec3 Scale(const Vec3& value, float scalar) noexcept
{
    return {value.x * scalar, value.y * scalar, value.z * scalar};
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
    const Vec3 axis = {orientation.x, orientation.y, orientation.z};
    const Vec3 doubledCross = Scale(Cross(axis, value), 2.0F);
    return Add(
        value,
        Add(
            Scale(doubledCross, orientation.w),
            Cross(axis, doubledCross)));
}

Vec3 InverseRotate(const Quaternion& orientation, const Vec3& value) noexcept
{
    return Rotate(
        {-orientation.x, -orientation.y, -orientation.z, orientation.w},
        value);
}

bool IsFinite(const Vec3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}
} // namespace

namespace bfvr::stereo
{

bool ProjectQuickMenuQuadToMirror(
    const Pose& quadPose,
    float widthMeters,
    float heightMeters,
    const QuickMenuMirrorView& eye,
    const QuickMenuMirrorCrop& crop,
    std::array<QuickMenuMirrorVertex, 4>& vertices) noexcept
{
    vertices = {};
    if (!(widthMeters > 0.0F) || !(heightMeters > 0.0F) ||
        !(crop.sourceScaleX > 0.0F) || !(crop.sourceScaleY > 0.0F) ||
        !std::isfinite(widthMeters) || !std::isfinite(heightMeters))
    {
        return false;
    }

    const float tangentLeft = std::tan(eye.angleLeft);
    const float tangentRight = std::tan(eye.angleRight);
    const float tangentUp = std::tan(eye.angleUp);
    const float tangentDown = std::tan(eye.angleDown);
    const float tangentWidth = tangentRight - tangentLeft;
    const float tangentHeight = tangentUp - tangentDown;
    if (!(tangentWidth > 0.0F) || !(tangentHeight > 0.0F) ||
        !std::isfinite(tangentWidth) || !std::isfinite(tangentHeight))
    {
        return false;
    }

    const Vec3 quadRight = Rotate(quadPose.orientation, {1.0F, 0.0F, 0.0F});
    const Vec3 quadUp = Rotate(quadPose.orientation, {0.0F, 1.0F, 0.0F});
    constexpr std::array<std::array<float, 2>, 4> kCorners = {{
        {-0.5F, -0.5F},
        {-0.5F, 0.5F},
        {0.5F, -0.5F},
        {0.5F, 0.5F}}};
    for (std::size_t index = 0; index < kCorners.size(); ++index)
    {
        const Vec3 localPoint = Add(
            quadPose.position,
            Add(
                Scale(quadRight, kCorners[index][0] * widthMeters),
                Scale(quadUp, kCorners[index][1] * heightMeters)));
        const Vec3 eyePoint = InverseRotate(
            eye.pose.orientation,
            Subtract(localPoint, eye.pose.position));
        const float depth = -eyePoint.z;
        if (!IsFinite(eyePoint) || !(depth > 0.01F))
        {
            vertices = {};
            return false;
        }

        const float tangentX = eyePoint.x / depth;
        const float tangentY = eyePoint.y / depth;
        const float sourceU = (tangentX - tangentLeft) / tangentWidth;
        const float sourceV = (tangentUp - tangentY) / tangentHeight;
        const float destinationU =
            (sourceU - crop.sourceOffsetX) / crop.sourceScaleX;
        const float destinationV =
            (sourceV - crop.sourceOffsetY) / crop.sourceScaleY;
        QuickMenuMirrorVertex& vertex = vertices[index];
        vertex.clipX = (destinationU * 2.0F - 1.0F) * depth;
        vertex.clipY = (1.0F - destinationV * 2.0F) * depth;
        vertex.clipZ = depth * 0.5F;
        vertex.clipW = depth;
    }
    return true;
}

} // namespace bfvr::stereo
