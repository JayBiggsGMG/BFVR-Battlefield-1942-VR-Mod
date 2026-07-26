#include "stereo/TreeAngleSlicePolicy.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace
{
constexpr float kTreeAngleQuarterTurn = 0.25F;
constexpr float kTreeAngleScale = 0.249975F;
constexpr float kMinimumHorizontalDistanceSquared = 1.0e-12F;
constexpr std::uint32_t kMaximumReasonableAngleCount = 256;

bool IsFiniteContext(
    const bfvr::stereo::BF1942TreeAngleSliceContext& context) noexcept
{
    const float referenceLengthSquared =
        context.referenceAxisX * context.referenceAxisX +
        context.referenceAxisZ * context.referenceAxisZ;
    const float angleLengthSquared =
        context.angleAxisX * context.angleAxisX +
        context.angleAxisZ * context.angleAxisZ;
    return
        std::isfinite(context.origin.x) &&
        std::isfinite(context.origin.y) &&
        std::isfinite(context.origin.z) &&
        std::isfinite(context.referenceAxisX) &&
        std::isfinite(context.referenceAxisZ) &&
        std::isfinite(context.angleAxisX) &&
        std::isfinite(context.angleAxisZ) &&
        std::isfinite(referenceLengthSquared) &&
        std::isfinite(angleLengthSquared) &&
        referenceLengthSquared > kMinimumHorizontalDistanceSquared &&
        angleLengthSquared > kMinimumHorizontalDistanceSquared;
}
} // namespace

namespace bfvr::stereo
{

std::optional<std::uint32_t> SelectBF1942TreeAngleSlice(
    const BF1942TreeAngleSliceContext& context,
    const Vec3& cameraPosition) noexcept
{
    if (!IsFiniteContext(context) ||
        !std::isfinite(cameraPosition.x) ||
        !std::isfinite(cameraPosition.y) ||
        !std::isfinite(cameraPosition.z) ||
        context.angleCount == 0 ||
        context.angleCount > kMaximumReasonableAngleCount ||
        context.centreAngleIndex >= context.angleCount)
    {
        return std::nullopt;
    }

    float directionX = cameraPosition.x - context.origin.x;
    float directionZ = cameraPosition.z - context.origin.z;
    const float lengthSquared =
        directionX * directionX + directionZ * directionZ;
    if (!std::isfinite(lengthSquared) ||
        lengthSquared <= kMinimumHorizontalDistanceSquared)
    {
        return std::nullopt;
    }
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    directionX *= inverseLength;
    directionZ *= inverseLength;

    float angle =
        kTreeAngleQuarterTurn -
        (directionX * context.angleAxisX +
         directionZ * context.angleAxisZ) *
            kTreeAngleScale;
    if (directionX * context.referenceAxisX +
            directionZ * context.referenceAxisZ <
        0.0F)
    {
        angle = 1.0F - angle;
    }
    if (!std::isfinite(angle))
    {
        return std::nullopt;
    }

    angle = std::clamp(angle, 0.0F, 1.0F);
    const float scaled = angle * static_cast<float>(context.angleCount);
    std::uint32_t index = static_cast<std::uint32_t>(scaled);
    if (index >= context.angleCount)
    {
        index = context.angleCount - 1;
    }
    return index;
}

std::optional<std::uint32_t> RemapBF1942TreeAngleSliceStartIndex(
    std::uint32_t originalStartIndex,
    std::uint32_t primitiveCount,
    std::uint32_t centreAngleIndex,
    std::uint32_t eyeAngleIndex) noexcept
{
    if (primitiveCount == 0)
    {
        return std::nullopt;
    }
    const std::uint64_t indexStride =
        static_cast<std::uint64_t>(primitiveCount) * 3U;
    const std::uint64_t centreOffset =
        indexStride * centreAngleIndex;
    if (centreOffset > originalStartIndex)
    {
        return std::nullopt;
    }
    const std::uint64_t angleZeroStart =
        static_cast<std::uint64_t>(originalStartIndex) - centreOffset;
    const std::uint64_t eyeStart =
        angleZeroStart + indexStride * eyeAngleIndex;
    if (eyeStart > std::numeric_limits<std::uint32_t>::max())
    {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(eyeStart);
}

} // namespace bfvr::stereo
