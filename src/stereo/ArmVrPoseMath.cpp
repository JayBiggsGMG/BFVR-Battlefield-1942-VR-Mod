#include "stereo/ArmVrPoseMath.h"

#include <cmath>
#include <cstddef>

namespace bfvr::stereo
{
namespace
{

bool IsFinite(const std::array<float, 3>& value) noexcept
{
    return std::isfinite(value[0]) &&
        std::isfinite(value[1]) &&
        std::isfinite(value[2]);
}

bool IsFinite(const Vec3& value) noexcept
{
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

std::optional<Quaternion> Normalize(const Quaternion& value) noexcept
{
    const float lengthSquared =
        value.x * value.x + value.y * value.y +
        value.z * value.z + value.w * value.w;
    if (!std::isfinite(lengthSquared) || lengthSquared < 0.25F)
    {
        return std::nullopt;
    }
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    const Quaternion result = {
        value.x * inverseLength,
        value.y * inverseLength,
        value.z * inverseLength,
        value.w * inverseLength};
    return std::isfinite(result.x) && std::isfinite(result.y) &&
            std::isfinite(result.z) && std::isfinite(result.w)
        ? std::optional<Quaternion>(result)
        : std::nullopt;
}

Vec3 Cross(const Vec3& left, const Vec3& right) noexcept
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x};
}

Vec3 Rotate(const Quaternion& orientation, const Vec3& value) noexcept
{
    const Vec3 axis = {orientation.x, orientation.y, orientation.z};
    const Vec3 first = Cross(axis, value);
    const Vec3 doubled = {first.x * 2.0F, first.y * 2.0F, first.z * 2.0F};
    const Vec3 second = Cross(axis, doubled);
    return {
        value.x + doubled.x * orientation.w + second.x,
        value.y + doubled.y * orientation.w + second.y,
        value.z + doubled.z * orientation.w + second.z};
}

} // namespace

std::optional<ArmVrShoulderAnchors> ComputeArmVrShoulderAnchors(
    const ArmVrShoulderAnchorInput& input) noexcept
{
    if (!IsFinite(input.trackedHead) ||
        !IsFinite(input.trackingToSkeleton) ||
        !IsFinite(input.stanceTranslation) ||
        !std::isfinite(input.halfShoulderWidth) ||
        !std::isfinite(input.headToShoulderDrop) ||
        input.halfShoulderWidth < 0.10F ||
        input.halfShoulderWidth > 0.30F ||
        input.headToShoulderDrop < 0.10F ||
        input.headToShoulderDrop > 0.35F)
    {
        return std::nullopt;
    }

    const std::array<float, 3> midpoint = {
        input.trackedHead[0] + input.trackingToSkeleton[0] +
            input.stanceTranslation[0],
        input.trackedHead[1] + input.trackingToSkeleton[1] +
            input.stanceTranslation[1] - input.headToShoulderDrop,
        input.trackedHead[2] + input.trackingToSkeleton[2] +
            input.stanceTranslation[2]};
    ArmVrShoulderAnchors result = {};
    result.right = midpoint;
    result.left = midpoint;
    result.right[0] += input.halfShoulderWidth;
    result.left[0] -= input.halfShoulderWidth;
    return IsFinite(result.right) && IsFinite(result.left)
        ? std::optional<ArmVrShoulderAnchors>(result)
        : std::nullopt;
}

std::optional<std::array<float, 3>> ComputeArmVrWristOffsetDelta(
    const ArmVrWristOffsetInput& input) noexcept
{
    const auto reference = Normalize(input.referenceGripOrientation);
    const auto current = Normalize(input.currentGripOrientation);
    if (!reference.has_value() || !current.has_value() ||
        !IsFinite(input.gripLocalWristOffset))
    {
        return std::nullopt;
    }
    const Vec3 referenceOffset = Rotate(
        *reference, input.gripLocalWristOffset);
    const Vec3 currentOffset = Rotate(
        *current, input.gripLocalWristOffset);
    if (!IsFinite(referenceOffset) || !IsFinite(currentOffset))
    {
        return std::nullopt;
    }

    // OpenXR reference space is right-handed; BF1942's Skeleton component
    // convention preserves X/Y and negates Z.
    const std::array<float, 3> result = {
        currentOffset.x - referenceOffset.x,
        currentOffset.y - referenceOffset.y,
        -(currentOffset.z - referenceOffset.z)};
    return IsFinite(result)
        ? std::optional<std::array<float, 3>>(result)
        : std::nullopt;
}

std::optional<std::array<float, 3>> ApplyArmVrHandPositionCalibration(
    const std::array<float, 3>& target,
    const std::array<std::int32_t, 3>& offsetCentimeters) noexcept
{
    if (!IsFinite(target))
    {
        return std::nullopt;
    }
    std::array<float, 3> result = target;
    for (std::size_t axis = 0; axis < result.size(); ++axis)
    {
        result[axis] += static_cast<float>(offsetCentimeters[axis]) / 100.0F;
    }
    return IsFinite(result)
        ? std::optional<std::array<float, 3>>(result)
        : std::nullopt;
}

} // namespace bfvr::stereo
