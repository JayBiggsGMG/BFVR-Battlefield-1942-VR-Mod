#include "openxr/OpenXRHudRoll.h"
#include "openxr/OpenXRScopeOverlayLayer.h"

#include <cmath>
#include <cstdio>

namespace
{
constexpr float kTolerance = 0.0001F;

bool NearlyEqual(float left, float right) noexcept
{
    return std::fabs(left - right) <= kTolerance;
}

std::array<XrView, 2> MakeViews() noexcept
{
    std::array<XrView, 2> views = {};
    for (std::size_t eye = 0; eye < views.size(); ++eye)
    {
        views[eye].type = XR_TYPE_VIEW;
        views[eye].pose.orientation.w = 1.0F;
        views[eye].pose.position = {
            eye == 0 ? 9.968F : 10.032F,
            2.0F,
            -5.0F};
        views[eye].fov = {
            -0.7853982F,
            0.7853982F,
            0.7853982F,
            -0.7853982F};
    }
    return views;
}

bool TestBuildsCentredEyeExclusiveLayers() noexcept
{
    XrPosef head = {};
    head.orientation.w = 1.0F;
    head.position = {10.0F, 2.0F, -5.0F};
    const auto views = MakeViews();
    std::array<XrCompositionLayerQuad, 2> layers = {};
    const XrSpace viewSpace = static_cast<XrSpace>(1);
    const XrSwapchain swapchain = static_cast<XrSwapchain>(2);
    if (!bfvr::BuildOpenXREyeFillingScopeLayers(
            viewSpace,
            swapchain,
            1872,
            2016,
            head,
            views,
            {0.0F, 0.0F, 0.0F, 1.0F},
            layers))
    {
        return false;
    }
    return layers[0].type == XR_TYPE_COMPOSITION_LAYER_QUAD &&
        layers[1].type == XR_TYPE_COMPOSITION_LAYER_QUAD &&
        layers[0].eyeVisibility == XR_EYE_VISIBILITY_LEFT &&
        layers[1].eyeVisibility == XR_EYE_VISIBILITY_RIGHT &&
        layers[0].space == viewSpace && layers[1].space == viewSpace &&
        layers[0].subImage.swapchain == swapchain &&
        layers[1].subImage.swapchain == swapchain &&
        NearlyEqual(layers[0].pose.position.x, -0.032F) &&
        NearlyEqual(layers[1].pose.position.x, 0.032F) &&
        NearlyEqual(layers[0].pose.position.z, -1.0F) &&
        NearlyEqual(layers[1].pose.position.z, -1.0F) &&
        NearlyEqual(layers[0].pose.orientation.w, 1.0F) &&
        NearlyEqual(layers[0].size.width, 2.04F) &&
        NearlyEqual(layers[0].size.height, 2.04F);
}

bool TestInvalidInputsFailClosed() noexcept
{
    XrPosef head = {};
    auto views = MakeViews();
    std::array<XrCompositionLayerQuad, 2> layers = {};
    const XrSpace viewSpace = static_cast<XrSpace>(1);
    const XrSwapchain swapchain = static_cast<XrSwapchain>(2);
    return !bfvr::BuildOpenXREyeFillingScopeLayers(
                XR_NULL_HANDLE,
                swapchain,
                1872,
                2016,
                head,
                views,
                {0.0F, 0.0F, 0.0F, 1.0F},
                layers) &&
        !bfvr::BuildOpenXREyeFillingScopeLayers(
                viewSpace,
                swapchain,
                1872,
                2016,
                head,
                views,
                {0.0F, 0.0F, 0.0F, 1.0F},
                layers);
}

bool TestScopeLayerRollCancelsHeadAndFollowsWeapon() noexcept
{
    constexpr float headRoll = 0.40F;
    constexpr float weaponRoll = -0.65F;
    const auto Roll = [](const float radians)
    {
        return XrQuaternionf{
            0.0F,
            0.0F,
            std::sin(radians * 0.5F),
            std::cos(radians * 0.5F)};
    };
    XrPosef head = {};
    head.orientation = Roll(headRoll);
    head.position = {10.0F, 2.0F, -5.0F};
    std::array<XrView, 2> views = {};
    for (std::size_t eye = 0; eye < views.size(); ++eye)
    {
        views[eye].type = XR_TYPE_VIEW;
        XrPosef eyeLocal = {};
        eyeLocal.orientation.w = 1.0F;
        eyeLocal.position.x = eye == 0 ? -0.032F : 0.032F;
        views[eye].pose = bfvr::ComposeOpenXRPose(head, eyeLocal);
        views[eye].fov = {
            -0.7853982F,
            0.7853982F,
            0.7853982F,
            -0.7853982F};
    }
    std::array<XrCompositionLayerQuad, 2> layers = {};
    if (!bfvr::BuildOpenXREyeFillingScopeLayers(
            static_cast<XrSpace>(1),
            static_cast<XrSwapchain>(2),
            1872,
            2016,
            head,
            views,
            Roll(weaponRoll - headRoll),
            layers))
    {
        return false;
    }
    const XrPosef physicalScope = bfvr::ComposeOpenXRPose(
        head,
        layers[0].pose);
    const XrQuaternionf expectedWeapon = Roll(weaponRoll);
    return NearlyEqual(
               layers[0].pose.orientation.z,
               std::sin((weaponRoll - headRoll) * 0.5F)) &&
        NearlyEqual(physicalScope.orientation.z, expectedWeapon.z) &&
        NearlyEqual(physicalScope.orientation.w, expectedWeapon.w);
}

bool TestHudRollCancelsOnlyHeadRoll() noexcept
{
    constexpr float halfRoll = 0.523598776F;
    const XrQuaternionf head = {
        0.0F,
        0.0F,
        std::sin(halfRoll),
        std::cos(halfRoll)};
    XrQuaternionf upright = {};
    return bfvr::BuildOpenXRGravityUprightViewRoll(head, upright) &&
        NearlyEqual(upright.x, 0.0F) &&
        NearlyEqual(upright.y, 0.0F) &&
        NearlyEqual(upright.z, -std::sin(halfRoll)) &&
        NearlyEqual(upright.w, std::cos(halfRoll));
}
} // namespace

int main()
{
    if (!TestBuildsCentredEyeExclusiveLayers() ||
        !TestInvalidInputsFailClosed() ||
        !TestScopeLayerRollCancelsHeadAndFollowsWeapon() ||
        !TestHudRollCancelsOnlyHeadRoll())
    {
        std::fprintf(stderr, "OpenXR scope-overlay layer tests failed.\n");
        return 1;
    }
    std::printf("OpenXR scope-overlay layer tests passed.\n");
    return 0;
}
