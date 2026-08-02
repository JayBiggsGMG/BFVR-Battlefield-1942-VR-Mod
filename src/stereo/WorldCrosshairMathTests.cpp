#include "stereo/WorldCrosshairMath.h"

#include <cmath>
#include <cstdio>

namespace
{

using bfvr::stereo::Matrix4;

constexpr float kTolerance = 0.0001F;

Matrix4 Identity() noexcept
{
    Matrix4 result = {};
    for (std::size_t index = 0; index < 4; ++index)
    {
        result.values[index][index] = 1.0F;
    }
    return result;
}

bool Near(float first, float second) noexcept
{
    return std::fabs(first - second) <= kTolerance;
}

bool TestStrictEligibility()
{
    using enum bfvr::stereo::WorldCrosshairAimSource;
    bfvr::stereo::WorldCrosshairEligibility input = {};
    input.localPlayerAlive = true;
    input.controlObjectsReadable = true;
    input.currentIsDefaultControlObject = true;
    input.nativeArmPoseFresh = true;
    for (int itemIndex : {4, 5, 6})
    {
        input.activeItemIndex = itemIndex;
        if (bfvr::stereo::SelectWorldCrosshairAimSource(input) !=
            GadgetController)
        {
            return false;
        }
    }
    for (int itemIndex : {1, 2, 3, 11})
    {
        input.activeItemIndex = itemIndex;
        if (bfvr::stereo::SelectWorldCrosshairAimSource(input) != None)
        {
            return false;
        }
    }
    input.activeItemIndex = 4;
    input.nativeArmPoseFresh = false;
    if (bfvr::stereo::SelectWorldCrosshairAimSource(input) != None)
    {
        return false;
    }
    input.nativeArmPoseFresh = true;
    input.localPlayerAlive = false;
    if (bfvr::stereo::SelectWorldCrosshairAimSource(input) != None)
    {
        return false;
    }
    input.localPlayerAlive = true;
    input.controlObjectsReadable = false;
    if (bfvr::stereo::SelectWorldCrosshairAimSource(input) != None)
    {
        return false;
    }
    input.controlObjectsReadable = true;
    input.currentIsDefaultControlObject = false;
    input.nativeCrosshairRequested = false;
    input.mountedFirePoseReadable = true;
    if (bfvr::stereo::SelectWorldCrosshairAimSource(input) != None)
    {
        return false;
    }
    input.nativeCrosshairRequested = true;
    input.mountedFirePoseReadable = false;
    if (bfvr::stereo::SelectWorldCrosshairAimSource(input) != None)
    {
        return false;
    }
    input.mountedFirePoseReadable = true;
    return bfvr::stereo::SelectWorldCrosshairAimSource(input) == MountedWeapon;
}

bool TestFireEndpoints()
{
    Matrix4 fire = Identity();
    fire.values[3][0] = 2.0F;
    fire.values[3][1] = 3.0F;
    fire.values[3][2] = 4.0F;
    const auto fireEndpoint =
        bfvr::stereo::MakeWorldCrosshairEndpointFromFirePose(fire, 50.0F);

    Matrix4 invalid = fire;
    invalid.values[0][0] = 2.0F;
    return fireEndpoint.has_value() &&
        Near(fireEndpoint->x, 2.0F) && Near(fireEndpoint->y, 3.0F) &&
        Near(fireEndpoint->z, 54.0F) &&
        !bfvr::stereo::MakeWorldCrosshairEndpointFromFirePose(
            invalid, 50.0F).has_value() &&
        !bfvr::stereo::MakeWorldCrosshairEndpointFromFirePose(
            fire, 0.0F).has_value();
}

bool TestPerEyeProjectionAndRejection()
{
    Matrix4 projection = {};
    projection.values[0][0] = 1.0F;
    projection.values[1][1] = 1.0F;
    projection.values[2][2] = 1.001F;
    projection.values[2][3] = 1.0F;
    projection.values[3][2] = -0.1001F;

    Matrix4 left = Identity();
    Matrix4 right = Identity();
    left.values[3][0] = 0.032F;
    right.values[3][0] = -0.032F;
    const auto leftProjection = bfvr::stereo::ProjectWorldCrosshairEndpoint(
        {0.0F, 0.0F, 10.0F}, left, projection, 1000.0F, 1000.0F, 2.0F);
    const auto rightProjection = bfvr::stereo::ProjectWorldCrosshairEndpoint(
        {0.0F, 0.0F, 10.0F}, right, projection, 1000.0F, 1000.0F, 2.0F);
    const auto behind = bfvr::stereo::ProjectWorldCrosshairEndpoint(
        {0.0F, 0.0F, -10.0F}, left, projection, 1000.0F, 1000.0F, 2.0F);
    const auto invalidViewport =
        bfvr::stereo::ProjectWorldCrosshairEndpoint(
            {0.0F, 0.0F, 10.0F}, left, projection, 0.0F, 1000.0F, 2.0F);
    return leftProjection.has_value() && rightProjection.has_value() &&
        leftProjection->centerX > rightProjection->centerX &&
        Near(leftProjection->centerY, 499.5F) &&
        leftProjection->halfExtentPixels > 8.0F &&
        leftProjection->halfExtentPixels < 10.0F && !behind.has_value() &&
        !invalidViewport.has_value();
}

} // namespace

int main()
{
    if (!TestStrictEligibility() || !TestFireEndpoints() ||
        !TestPerEyeProjectionAndRejection())
    {
        std::fprintf(stderr, "World crosshair math tests failed.\n");
        return 1;
    }
    std::printf("World crosshair math tests passed.\n");
    return 0;
}
