#include "openxr/OpenXRScopeOverlayLayer.h"

#include "stereo/ScopeViewMath.h"

#include <cmath>

namespace
{
constexpr float kScopeOverlayDistanceMeters = 1.0F;

XrQuaternionf Multiply(
    const XrQuaternionf& left,
    const XrQuaternionf& right) noexcept
{
    return {
        left.w * right.x + left.x * right.w +
            left.y * right.z - left.z * right.y,
        left.w * right.y - left.x * right.z +
            left.y * right.w + left.z * right.x,
        left.w * right.z + left.x * right.y -
            left.y * right.x + left.z * right.w,
        left.w * right.w - left.x * right.x -
            left.y * right.y - left.z * right.z};
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

XrPosef ComposePose(const XrPosef& parent, const XrPosef& local) noexcept
{
    const XrVector3f translated = Rotate(parent.orientation, local.position);
    XrPosef result = {};
    result.orientation = Multiply(parent.orientation, local.orientation);
    result.position = {
        parent.position.x + translated.x,
        parent.position.y + translated.y,
        parent.position.z + translated.z};
    return result;
}

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

bool InvertPose(const XrPosef& pose, XrPosef& inverse) noexcept
{
    inverse = {};
    inverse.orientation = {
        -pose.orientation.x,
        -pose.orientation.y,
        -pose.orientation.z,
        pose.orientation.w};
    if (!Normalize(inverse.orientation) ||
        !std::isfinite(pose.position.x) ||
        !std::isfinite(pose.position.y) ||
        !std::isfinite(pose.position.z))
    {
        return false;
    }
    inverse.position = Rotate(
        inverse.orientation,
        {-pose.position.x, -pose.position.y, -pose.position.z});
    return true;
}
} // namespace

namespace bfvr
{

XrPosef ComposeOpenXRPose(
    const XrPosef& parent,
    const XrPosef& local) noexcept
{
    return ComposePose(parent, local);
}

bool BuildOpenXREyeFillingScopeLayers(
    XrSpace viewSpace,
    XrSwapchain uiSwapchain,
    std::uint32_t uiWidth,
    std::uint32_t uiHeight,
    const XrPosef& headInLocalSpace,
    const std::array<XrView, 2>& viewsInLocalSpace,
    std::array<XrCompositionLayerQuad, 2>& layers) noexcept
{
    if (viewSpace == XR_NULL_HANDLE || uiSwapchain == XR_NULL_HANDLE ||
        uiWidth == 0 || uiHeight == 0)
    {
        return false;
    }
    XrPosef localToView = {};
    if (!InvertPose(headInLocalSpace, localToView))
    {
        return false;
    }

    std::array<XrCompositionLayerQuad, 2> result = {};
    for (std::size_t eye = 0; eye < result.size(); ++eye)
    {
        const XrFovf& sourceFov = viewsInLocalSpace[eye].fov;
        const auto quadSize = stereo::ComputeEyeFillingScopeOverlayQuadSize(
            {
                sourceFov.angleLeft,
                sourceFov.angleRight,
                sourceFov.angleUp,
                sourceFov.angleDown},
            kScopeOverlayDistanceMeters);
        if (!quadSize.has_value())
        {
            return false;
        }

        XrPosef eyeOffset = {};
        eyeOffset.orientation.w = 1.0F;
        eyeOffset.position.z = -kScopeOverlayDistanceMeters;
        XrCompositionLayerQuad& layer = result[eye];
        layer.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
        layer.layerFlags =
            XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        layer.space = viewSpace;
        layer.eyeVisibility = eye == 0
            ? XR_EYE_VISIBILITY_LEFT
            : XR_EYE_VISIBILITY_RIGHT;
        layer.subImage.swapchain = uiSwapchain;
        layer.subImage.imageRect.extent.width =
            static_cast<std::int32_t>(uiWidth);
        layer.subImage.imageRect.extent.height =
            static_cast<std::int32_t>(uiHeight);
        layer.pose = ComposePose(
            ComposePose(localToView, viewsInLocalSpace[eye].pose),
            eyeOffset);
        if (!Normalize(layer.pose.orientation))
        {
            return false;
        }
        layer.size.width = quadSize->widthMeters;
        layer.size.height = quadSize->heightMeters;
    }
    layers = result;
    return true;
}

} // namespace bfvr
