#include "client/BFSoldierTrackedHandPose.h"

#include "stereo/WeaponPoseMath.h"

#include <cmath>

namespace bfvr
{

namespace
{

bool IsFinite(const stereo::Matrix4& matrix) noexcept
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

stereo::Matrix4 IdentityMatrix() noexcept
{
    stereo::Matrix4 result = {};
    for (std::size_t index = 0; index < 4; ++index)
    {
        result.values[index][index] = 1.0F;
    }
    return result;
}

stereo::Matrix4 Multiply(
    const stereo::Matrix4& left,
    const stereo::Matrix4& right) noexcept
{
    stereo::Matrix4 result = {};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            for (std::size_t inner = 0; inner < 4; ++inner)
            {
                result.values[row][column] +=
                    left.values[row][inner] *
                    right.values[inner][column];
            }
        }
    }
    return result;
}

} // namespace

std::optional<BFSoldierTrackedHandPose>
MakeBFSoldierTrackedHandPose(
    const D3D8RuntimeControllerHand& hand,
    const stereo::Matrix4& soldierWorld,
    const std::array<float, 3>& trackingOriginOffset,
    const std::array<float, 3>& stanceTranslation,
    const float worldUnitsPerMetre) noexcept
{
    const stereo::Pose gripPose = {
        {
            hand.gripPose.positionX,
            hand.gripPose.positionY,
            hand.gripPose.positionZ},
        {
            hand.gripPose.orientationX,
            hand.gripPose.orientationY,
            hand.gripPose.orientationZ,
            hand.gripPose.orientationW}};
    const auto grip =
        stereo::MakeD3D8AbsoluteGripWeaponDelta(
            IdentityMatrix(),
            gripPose,
            worldUnitsPerMetre);
    if (!grip.has_value() || !IsFinite(soldierWorld))
    {
        return std::nullopt;
    }

    BFSoldierTrackedHandPose result = {};
    result.local = *grip;
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        if (!std::isfinite(trackingOriginOffset[axis]) ||
            !std::isfinite(stanceTranslation[axis]))
        {
            return std::nullopt;
        }
        result.local.values[3][axis] +=
            trackingOriginOffset[axis] +
            stanceTranslation[axis];
    }
    result.local.values[3][3] = 1.0F;
    result.world = Multiply(result.local, soldierWorld);
    return IsFinite(result.local) && IsFinite(result.world)
        ? std::optional<BFSoldierTrackedHandPose>(result)
        : std::nullopt;
}

} // namespace bfvr
