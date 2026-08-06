#include "client/D3D8TrackingAnchor.h"

#include "stereo/StereoMath.h"

#include <cmath>
#include <cstdio>

namespace
{
bfvr::stereo::Pose ToPose(const bfvr::D3D8RuntimeView& view) noexcept
{
    return {
        {view.positionX, view.positionY, view.positionZ},
        {view.orientationX, view.orientationY,
         view.orientationZ, view.orientationW}};
}

bfvr::stereo::Pose ToPose(
    const bfvr::D3D8RuntimeControllerPose& pose) noexcept
{
    return {
        {pose.positionX, pose.positionY, pose.positionZ},
        {pose.orientationX, pose.orientationY,
         pose.orientationZ, pose.orientationW}};
}

bool NearlyEqual(float left, float right) noexcept
{
    return std::fabs(left - right) <= 0.0001F;
}

bool TestMenuAnchorAndControllerUseOneRebasedSpace() noexcept
{
    bfvr::D3D8RuntimeView head = {};
    head.positionX = 2.0F;
    head.positionY = 0.35F;
    head.positionZ = 3.0F;
    head.orientationW = 1.0F;

    bfvr::D3D8TrackingAnchor anchor = {};
    anchor.Update(
        head,
        true,
        {bfvr::D3D8TrackingContextKind::Infantry, 0x1000},
        1'000'000'000,
        0,
        false,
        true,
        1.10F,
        1.70F,
        0.10F);

    bfvr::D3D8RuntimeView rawMenuAnchor = head;
    bfvr::D3D8RuntimeControllerSample rawController = {};
    rawController.hands[1].aimPose.positionX = 2.25F;
    rawController.hands[1].aimPose.positionY = 0.10F;
    rawController.hands[1].aimPose.positionZ = 2.60F;
    rawController.hands[1].aimPose.orientationW = 1.0F;

    const auto rawRelative = bfvr::stereo::MakeRelativePose(
        ToPose(rawMenuAnchor),
        ToPose(rawController.hands[1].aimPose));
    const bfvr::D3D8RuntimeView rebasedMenuAnchor =
        anchor.RebaseView(rawMenuAnchor);
    const bfvr::D3D8RuntimeControllerSample rebasedController =
        anchor.RebaseControllerSample(rawController);
    const auto rebasedRelative = bfvr::stereo::MakeRelativePose(
        ToPose(rebasedMenuAnchor),
        ToPose(rebasedController.hands[1].aimPose));

    return rawRelative.has_value() && rebasedRelative.has_value() &&
        NearlyEqual(
            rebasedRelative->position.x,
            rawRelative->position.x) &&
        NearlyEqual(
            rebasedRelative->position.y,
            rawRelative->position.y) &&
        NearlyEqual(
            rebasedRelative->position.z,
            rawRelative->position.z);
}
} // namespace

int main()
{
    if (!TestMenuAnchorAndControllerUseOneRebasedSpace())
    {
        std::fprintf(
            stderr,
            "Tracking-anchor menu/controller space test failed.\n");
        return 1;
    }
    std::printf("Tracking-anchor tests passed.\n");
    return 0;
}
