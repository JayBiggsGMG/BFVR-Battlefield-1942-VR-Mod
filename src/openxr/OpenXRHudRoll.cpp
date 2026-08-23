#include "openxr/OpenXRHudRoll.h"

#include <cmath>

namespace
{
bool Normalize(XrQuaternionf& orientation) noexcept
{
    const float lengthSquared =
        orientation.x * orientation.x + orientation.y * orientation.y +
        orientation.z * orientation.z + orientation.w * orientation.w;
    if (!std::isfinite(lengthSquared) || lengthSquared < 0.25F ||
        lengthSquared > 2.25F)
    {
        return false;
    }
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    orientation.x *= inverseLength;
    orientation.y *= inverseLength;
    orientation.z *= inverseLength;
    orientation.w *= inverseLength;
    return true;
}

XrVector3f Rotate(
    const XrQuaternionf& orientation,
    const XrVector3f& value) noexcept
{
    const XrVector3f axis = {
        orientation.x,
        orientation.y,
        orientation.z};
    const XrVector3f cross = {
        axis.y * value.z - axis.z * value.y,
        axis.z * value.x - axis.x * value.z,
        axis.x * value.y - axis.y * value.x};
    const XrVector3f doubled = {
        cross.x * 2.0F,
        cross.y * 2.0F,
        cross.z * 2.0F};
    const XrVector3f secondCross = {
        axis.y * doubled.z - axis.z * doubled.y,
        axis.z * doubled.x - axis.x * doubled.z,
        axis.x * doubled.y - axis.y * doubled.x};
    return {
        value.x + doubled.x * orientation.w + secondCross.x,
        value.y + doubled.y * orientation.w + secondCross.y,
        value.z + doubled.z * orientation.w + secondCross.z};
}
} // namespace

namespace bfvr
{

bool BuildOpenXRGravityUprightViewRoll(
    const XrQuaternionf& headInLocalSpace,
    XrQuaternionf& viewRoll) noexcept
{
    XrQuaternionf inverseHead = {
        -headInLocalSpace.x,
        -headInLocalSpace.y,
        -headInLocalSpace.z,
        headInLocalSpace.w};
    if (!Normalize(inverseHead))
    {
        return false;
    }
    const XrVector3f gravityUpInView = Rotate(
        inverseHead,
        {0.0F, 1.0F, 0.0F});
    const float projectedLength = std::sqrt(
        gravityUpInView.x * gravityUpInView.x +
        gravityUpInView.y * gravityUpInView.y);
    if (!std::isfinite(projectedLength) || projectedLength < 0.001F)
    {
        return false;
    }
    const float angle = std::atan2(
        -gravityUpInView.x,
        gravityUpInView.y);
    const float halfAngle = angle * 0.5F;
    viewRoll = {0.0F, 0.0F, std::sin(halfAngle), std::cos(halfAngle)};
    return Normalize(viewRoll);
}

} // namespace bfvr
