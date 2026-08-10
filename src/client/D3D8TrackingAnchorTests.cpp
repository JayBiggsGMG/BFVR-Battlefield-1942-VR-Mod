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

float ViewYaw(const bfvr::D3D8RuntimeView& view) noexcept
{
    return std::atan2(
        2.0F * (view.orientationW * view.orientationY +
                view.orientationX * view.orientationZ),
        1.0F - 2.0F *
            (view.orientationY * view.orientationY +
             view.orientationZ * view.orientationZ));
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

bool TestTransientVehicleContextsDoNotPinHeadPose() noexcept
{
    bfvr::D3D8RuntimeView head = {};
    head.positionY = 0.30F;
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
        0.0F);

    // BF1942 may expose different current-control pointers while its seat
    // assignment transaction is incomplete. None may repeatedly redefine
    // the current headset pose as zero.
    head.positionX = 0.10F;
    anchor.Update(
        head, true, {bfvr::D3D8TrackingContextKind::Seat, 0x2000},
        2'000'000'000, 0, false, true, 1.10F, 1.70F, 0.0F);
    head.positionX = 0.20F;
    anchor.Update(
        head, true, {bfvr::D3D8TrackingContextKind::Seat, 0x2001},
        3'000'000'000, 0, false, true, 1.10F, 1.70F, 0.0F);
    head.positionX = 0.30F;
    anchor.Update(
        head, true, {bfvr::D3D8TrackingContextKind::Seat, 0x2000},
        4'000'000'000, 0, false, true, 1.10F, 1.70F, 0.0F);
    if (!NearlyEqual(anchor.RebaseView(head).positionX, 0.30F) ||
        anchor.Context().kind != bfvr::D3D8TrackingContextKind::Infantry ||
        anchor.ContextGeneration() != 1)
    {
        return false;
    }

    // One stable seat commits one neutral pose after three samples. Further
    // physical motion must immediately remain visible relative to it.
    head.positionX = 0.40F;
    anchor.Update(
        head, true, {bfvr::D3D8TrackingContextKind::Seat, 0x2000},
        5'000'000'000, 0, false, true, 1.10F, 1.70F, 0.0F);
    head.positionX = 0.50F;
    anchor.Update(
        head, true, {bfvr::D3D8TrackingContextKind::Seat, 0x2000},
        6'000'000'000, 0, false, true, 1.10F, 1.70F, 0.0F);
    if (!NearlyEqual(anchor.RebaseView(head).positionX, 0.0F) ||
        anchor.Context().kind != bfvr::D3D8TrackingContextKind::Seat ||
        anchor.ContextGeneration() != 2)
    {
        return false;
    }
    head.positionX = 0.60F;
    anchor.Update(
        head, true, {bfvr::D3D8TrackingContextKind::Seat, 0x2000},
        7'000'000'000, 0, false, true, 1.10F, 1.70F, 0.0F);
    return NearlyEqual(anchor.RebaseView(head).positionX, 0.10F) &&
        anchor.ContextGeneration() == 2;
}

bool TestVehicleSeatChangeAndExitShareCommittedGeneration() noexcept
{
    bfvr::D3D8RuntimeView head = {};
    head.orientationW = 1.0F;
    bfvr::D3D8TrackingAnchor anchor = {};
    const auto update = [&](const float x,
                            const bfvr::D3D8TrackingContext context) {
        head.positionX = x;
        anchor.Update(
            head, true, context, 1'000'000'000, 0,
            false, true, 1.10F, 1.70F, 0.0F);
    };

    const bfvr::D3D8TrackingContext infantry = {
        bfvr::D3D8TrackingContextKind::Infantry, 0x1000};
    const bfvr::D3D8TrackingContext seat = {
        bfvr::D3D8TrackingContextKind::Seat, 0x2000};
    const bfvr::D3D8TrackingContext secondSeat = {
        bfvr::D3D8TrackingContextKind::Seat, 0x2001};
    update(0.0F, infantry);
    update(0.1F, seat);
    update(0.2F, seat);
    update(0.3F, seat);
    if (anchor.ContextGeneration() != 2 ||
        anchor.Context().kind != bfvr::D3D8TrackingContextKind::Seat)
    {
        return false;
    }

    // A tank/transport/aircraft station or passenger-seat identity change is
    // the same committed boundary as a stationary gun. It must not borrow the
    // prior seat's headset/controller/body lifetime.
    update(0.31F, secondSeat);
    update(0.32F, secondSeat);
    update(0.33F, secondSeat);
    if (anchor.ContextGeneration() != 3 ||
        anchor.Context().token != secondSeat.token)
    {
        return false;
    }

    // The body/camera lifetime consumer must still see the seat generation
    // during the first two exit samples. The third sample atomically commits
    // the new infantry reference and generation; no separately timed station
    // resolver is needed to guess this boundary.
    update(0.4F, infantry);
    update(0.5F, infantry);
    if (anchor.ContextGeneration() != 3 ||
        anchor.Context().kind != bfvr::D3D8TrackingContextKind::Seat)
    {
        return false;
    }
    update(0.6F, infantry);
    return anchor.ContextGeneration() == 4 &&
        anchor.Context().kind == bfvr::D3D8TrackingContextKind::Infantry &&
        NearlyEqual(anchor.RebaseView(head).positionX, 0.0F);
}


bool TestInfantryPresentationIgnoresNativeAimAndOwnsArtificialTurn() noexcept
{
    constexpr float degreesToRadians = 0.01745329251994329577F;
    bfvr::D3D8RuntimeView head = {};
    head.orientationW = 1.0F;
    bfvr::D3D8TrackingAnchor anchor = {};
    const bfvr::D3D8TrackingContext infantry = {
        bfvr::D3D8TrackingContextKind::Infantry, 0x1000};
    const auto update = [&](float requestedTurnDegrees,
                            float bodyDegrees,
                            LONG recenterSequence = 0) {
        anchor.Update(
            head,
            true,
            infantry,
            1'000'000'000,
            recenterSequence,
            false,
            true,
            1.10F,
            1.70F,
            0.0F,
            {
                requestedTurnDegrees,
                bodyDegrees * degreesToRadians,
                true});
    };

    update(0, 0.0F);
    float presentationYaw = 0.0F;
    if (!anchor.ReadInfantryPresentationYaw(presentationYaw) ||
        !NearlyEqual(presentationYaw, 0.0F))
    {
        return false;
    }

    // Native hand aim may rotate the authoritative root through any value.
    // It changes the shared controller/body correction, never the camera's
    // physical reference or presented heading.
    update(0, 25.0F);
    if (!anchor.ReadInfantryPresentationYaw(presentationYaw) ||
        !NearlyEqual(presentationYaw, 0.0F) ||
        !NearlyEqual(
            ViewYaw(anchor.PresentationReferenceHead(head)),
            0.0F) ||
        !NearlyEqual(
            ViewYaw(anchor.ReferenceHead(head)),
            -25.0F * degreesToRadians))
    {
        return false;
    }

    // Smooth/Snap intent advances presentation immediately. Later native body
    // catch-up and unrelated hand-driven yaw cannot replay or hitch that turn.
    update(45.0F, 25.0F);
    if (!anchor.ReadInfantryPresentationYaw(presentationYaw) ||
        !NearlyEqual(presentationYaw, 45.0F * degreesToRadians))
    {
        return false;
    }
    update(0.0F, 70.0F);
    update(0.0F, 110.0F);
    if (!anchor.ReadInfantryPresentationYaw(presentationYaw) ||
        !NearlyEqual(presentationYaw, 45.0F * degreesToRadians))
    {
        return false;
    }

    // Recenter changes only the physical HMD neutral yaw. It must preserve the
    // presentation heading and the body-compensation offset.
    const float recenterYaw = 10.0F * degreesToRadians;
    head.orientationY = std::sin(recenterYaw * 0.5F);
    head.orientationW = std::cos(recenterYaw * 0.5F);
    update(0.0F, 110.0F, 1);
    return anchor.ReadInfantryPresentationYaw(presentationYaw) &&
        NearlyEqual(presentationYaw, 45.0F * degreesToRadians) &&
        NearlyEqual(
            ViewYaw(anchor.PresentationReferenceHead(head)),
            recenterYaw);
}

bool TestControllerBodyBasisCorrectionMatchesFreshAnchorRebase() noexcept
{
    constexpr float degreesToRadians = 0.01745329251994329577F;
    bfvr::D3D8RuntimeView head = {};
    head.orientationW = 1.0F;
    const bfvr::D3D8TrackingContext infantry = {
        bfvr::D3D8TrackingContextKind::Infantry, 0x1000};
    bfvr::D3D8TrackingAnchor anchor = {};
    anchor.Update(
        head, true, infantry, 1'000'000'000, 0,
        false, true, 1.10F, 1.70F, 0.0F,
        {0.0F, 10.0F * degreesToRadians, true});

    bfvr::D3D8RuntimeControllerSample raw = {};
    raw.valid = true;
    raw.sessionFocused = true;
    raw.hands[0].gripPose.positionX = -0.25F;
    raw.hands[0].gripPose.positionZ = -0.40F;
    raw.hands[0].gripPose.orientationW = 1.0F;
    raw.hands[1].aimPose.positionX = 0.30F;
    raw.hands[1].aimPose.positionZ = -0.55F;
    raw.hands[1].aimPose.orientationW = 1.0F;
    const auto observedBodySample = anchor.RebaseControllerSample(raw);

    // Simulate PlayerAction advancing the native body after publication but
    // before the arm skeleton callback. A fresh anchor observation is the
    // reference result the callback-local correction must reproduce.
    anchor.Update(
        head, true, infantry, 1'010'000'000, 0,
        false, true, 1.10F, 1.70F, 0.0F,
        {0.0F, 35.0F * degreesToRadians, true});
    const auto expected = anchor.RebaseControllerSample(raw);
    bfvr::D3D8RuntimeControllerSample adjusted = {};
    if (!bfvr::RebaseInfantryControllerSampleToCurrentBodyYaw(
            observedBodySample,
            10.0F * degreesToRadians,
            35.0F * degreesToRadians,
            adjusted))
    {
        return false;
    }

    const auto& adjustedLeft = adjusted.hands[0].gripPose;
    const auto& expectedLeft = expected.hands[0].gripPose;
    const auto& adjustedRight = adjusted.hands[1].aimPose;
    const auto& expectedRight = expected.hands[1].aimPose;
    bfvr::D3D8RuntimeControllerSample invalidResult = {};
    const bool invalidRejected =
        !bfvr::RebaseInfantryControllerSampleToCurrentBodyYaw(
            observedBodySample,
            NAN,
            0.0F,
            invalidResult);
    return NearlyEqual(adjustedLeft.positionX, expectedLeft.positionX) &&
        NearlyEqual(adjustedLeft.positionZ, expectedLeft.positionZ) &&
        NearlyEqual(adjustedLeft.orientationY, expectedLeft.orientationY) &&
        NearlyEqual(adjustedLeft.orientationW, expectedLeft.orientationW) &&
        NearlyEqual(adjustedRight.positionX, expectedRight.positionX) &&
        NearlyEqual(adjustedRight.positionZ, expectedRight.positionZ) &&
        NearlyEqual(adjustedRight.orientationY, expectedRight.orientationY) &&
        NearlyEqual(adjustedRight.orientationW, expectedRight.orientationW) &&
        invalidRejected &&
        NearlyEqual(
            invalidResult.hands[1].aimPose.positionX,
            observedBodySample.hands[1].aimPose.positionX);
}
} // namespace

int main()
{
    if (!TestMenuAnchorAndControllerUseOneRebasedSpace() ||
        !TestTransientVehicleContextsDoNotPinHeadPose() ||
        !TestVehicleSeatChangeAndExitShareCommittedGeneration() ||
        !TestInfantryPresentationIgnoresNativeAimAndOwnsArtificialTurn() ||
        !TestControllerBodyBasisCorrectionMatchesFreshAnchorRebase())
    {
        std::fprintf(
            stderr,
            "Tracking-anchor menu/controller space test failed.\n");
        return 1;
    }
    std::printf("Tracking-anchor tests passed.\n");
    return 0;
}
