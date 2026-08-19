#include "stereo/ArmVrPoseMath.h"

#include <cmath>
#include <cstdio>

namespace
{

bool Near(float left, float right, float tolerance = 1.0e-4F)
{
    return std::fabs(left - right) <= tolerance;
}

bool Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
    return condition;
}

} // namespace

int main()
{
    bool passed = true;
    bfvr::stereo::ArmVrShoulderAnchorInput anchors = {};
    anchors.trackedHead = {0.02F, 0.01F, -0.03F};
    anchors.trackingToSkeleton = {0.0973F, 0.6767F, -0.1299F};
    anchors.stanceTranslation = {0.0F, -0.15F, 0.02F};
    const auto result = bfvr::stereo::ComputeArmVrShoulderAnchors(anchors);
    passed &= Check(result.has_value(), "valid shoulder anchors rejected");
    if (result.has_value())
    {
        passed &= Check(
            Near(result->right[0] - result->left[0], 0.36F),
            "shoulder width changed");
        passed &= Check(
            Near(result->right[1], 0.3367F) &&
                Near(result->left[2], -0.1399F),
            "tracked head/origin/stance composition changed");
    }

    auto invalid = anchors;
    invalid.halfShoulderWidth = 2.0F;
    passed &= Check(
        !bfvr::stereo::ComputeArmVrShoulderAnchors(invalid).has_value(),
        "unsafe shoulder width accepted");

    bfvr::stereo::ArmVrWristOffsetInput wrist = {};
    const auto neutral = bfvr::stereo::ComputeArmVrWristOffsetDelta(wrist);
    passed &= Check(
        neutral.has_value() && Near((*neutral)[0], 0.0F) &&
            Near((*neutral)[1], 0.0F) && Near((*neutral)[2], 0.0F),
        "reference wrist pose introduced a jump");

    constexpr float kHalfSqrtTwo = 0.70710678F;
    wrist.currentGripOrientation = {
        0.0F, kHalfSqrtTwo, 0.0F, kHalfSqrtTwo};
    const auto rotated = bfvr::stereo::ComputeArmVrWristOffsetDelta(wrist);
    passed &= Check(
        rotated.has_value() && Near((*rotated)[0], 0.08F) &&
            Near((*rotated)[1], 0.0F) && Near((*rotated)[2], 0.08F),
        "controller-local wrist lever arm did not rotate into D3D8 space");

    wrist.currentGripOrientation = {};
    wrist.currentGripOrientation.w = 0.0F;
    passed &= Check(
        !bfvr::stereo::ComputeArmVrWristOffsetDelta(wrist).has_value(),
        "zero wrist quaternion accepted");

    const auto calibratedRight =
        bfvr::stereo::ApplyArmVrHandPositionCalibration(
            {0.25F, 0.50F, -0.10F},
            bfvr::stereo::kRightHandPositionCalibrationCentimeters);
    passed &= Check(
        calibratedRight.has_value() &&
            Near((*calibratedRight)[0], 0.20F) &&
            Near((*calibratedRight)[1], 0.54F) &&
            Near((*calibratedRight)[2], -0.08F),
        "accepted right-hand calibration changed");
    const auto calibratedLeft =
        bfvr::stereo::ApplyArmVrHandPositionCalibration(
            {0.25F, 0.50F, -0.10F},
            bfvr::stereo::kLeftHandPositionCalibrationCentimeters);
    passed &= Check(
        calibratedLeft.has_value() &&
            Near((*calibratedLeft)[0], 0.24F) &&
            Near((*calibratedLeft)[1], 0.58F) &&
            Near((*calibratedLeft)[2], -0.18F),
        "accepted left-hand calibration changed");

    if (passed)
    {
        std::puts("VR arm pose math tests passed.");
        return 0;
    }
    return 1;
}
