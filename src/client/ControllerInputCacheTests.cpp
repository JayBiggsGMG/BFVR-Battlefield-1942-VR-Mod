#include "client/ControllerInputCache.h"

#include <cmath>
#include <cstdio>

namespace
{

bool Near(const float left, const float right) noexcept
{
    return std::fabs(left - right) <= 0.0001F;
}

bool TestNativeAimSampleIsIndependentFromPresentationRebase() noexcept
{
    bfvr::D3D8RuntimeControllerSample presentation = {};
    presentation.valid = true;
    presentation.sessionFocused = true;
    presentation.hands[1].aimPose.orientationY = 0.41F;
    presentation.hands[1].aimPose.orientationW = 0.91F;

    bfvr::D3D8RuntimeControllerSample nativeAim = presentation;
    nativeAim.hands[1].aimPose.orientationY = -0.17F;
    nativeAim.hands[1].aimPose.orientationW = 0.98F;

    bfvr::D3D8RuntimeView matchingHead = {};
    matchingHead.orientationW = 1.0F;
    bfvr::PublishAcceptedControllerInput(
        presentation,
        nativeAim,
        matchingHead,
        true,
        0.75F,
        true);

    bfvr::D3D8RuntimeControllerSample readPresentation = {};
    bfvr::D3D8RuntimeControllerSample readNativeAim = {};
    LONG presentationGeneration = 0;
    LONG nativeAimGeneration = 0;
    const bool presentationRead = bfvr::ReadFreshAcceptedControllerInput(
        readPresentation,
        presentationGeneration,
        1000);
    const bool nativeAimRead =
        bfvr::ReadFreshAcceptedNativeInfantryAimInput(
            readNativeAim,
            nativeAimGeneration,
            1000);
    bfvr::D3D8RuntimeControllerSample pairedPresentation = {};
    float pairedBodyYaw = 0.0F;
    bool pairedBodyYawValid = false;
    LONG pairedGeneration = 0;
    const bool pairedRead =
        bfvr::ReadFreshAcceptedInfantryPresentationInput(
            pairedPresentation,
            pairedBodyYaw,
            pairedBodyYawValid,
            pairedGeneration,
            1000);
    return presentationRead && nativeAimRead && pairedRead &&
        presentationGeneration > 0 &&
        presentationGeneration == nativeAimGeneration &&
        presentationGeneration == pairedGeneration &&
        Near(readPresentation.hands[1].aimPose.orientationY, 0.41F) &&
        Near(readNativeAim.hands[1].aimPose.orientationY, -0.17F) &&
        Near(pairedPresentation.hands[1].aimPose.orientationY, 0.41F) &&
        pairedBodyYawValid && Near(pairedBodyYaw, 0.75F);
}

bool TestClearFailsClosed() noexcept
{
    bfvr::ClearAcceptedControllerInput();
    bfvr::D3D8RuntimeControllerSample sample = {};
    float bodyYaw = 0.0F;
    bool bodyYawValid = true;
    LONG generation = 0;
    return !bfvr::ReadFreshAcceptedControllerInput(sample, generation, 1000) &&
        !bfvr::ReadFreshAcceptedInfantryPresentationInput(
            sample,
            bodyYaw,
            bodyYawValid,
            generation,
            1000) &&
        !bfvr::ReadFreshAcceptedNativeInfantryAimInput(
            sample,
            generation,
            1000);
}

} // namespace

int main()
{
    if (!TestNativeAimSampleIsIndependentFromPresentationRebase() ||
        !TestClearFailsClosed())
    {
        std::fprintf(stderr, "Controller-input cache tests failed.\n");
        return 1;
    }
    std::printf("Controller-input cache tests passed.\n");
    return 0;
}
