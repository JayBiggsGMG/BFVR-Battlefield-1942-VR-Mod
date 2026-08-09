#include "stereo/MountedCameraMath.h"

#include <cmath>
#include <cstdio>

namespace
{
using bfvr::stereo::Matrix4;

constexpr float kTolerance = 0.0001F;
constexpr float kHalfPi = 1.57079632679F;

Matrix4 Identity() noexcept
{
    Matrix4 result = {};
    for (std::size_t index = 0; index < 4; ++index)
    {
        result.values[index][index] = 1.0F;
    }
    return result;
}

Matrix4 Translation(float x, float y, float z) noexcept
{
    Matrix4 result = Identity();
    result.values[3][0] = x;
    result.values[3][1] = y;
    result.values[3][2] = z;
    return result;
}

Matrix4 Yaw(float radians) noexcept
{
    Matrix4 result = Identity();
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    result.values[0][0] = cosine;
    result.values[0][2] = -sine;
    result.values[2][0] = sine;
    result.values[2][2] = cosine;
    return result;
}

Matrix4 Multiply(const Matrix4& left, const Matrix4& right) noexcept
{
    Matrix4 result = {};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            for (std::size_t inner = 0; inner < 4; ++inner)
            {
                result.values[row][column] +=
                    left.values[row][inner] * right.values[inner][column];
            }
        }
    }
    return result;
}

bool NearlyEqual(const Matrix4& first, const Matrix4& second) noexcept
{
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            if (std::fabs(
                    first.values[row][column] -
                    second.values[row][column]) > kTolerance)
            {
                return false;
            }
        }
    }
    return true;
}

bool TestStationMotionSurvivesAndLaterGunAimDoesNot() noexcept
{
    const Matrix4 stationAtCapture = Translation(10.0F, 2.0F, -4.0F);
    const Matrix4 cameraInStation = Multiply(
        Translation(0.0F, 1.4F, -0.5F),
        Yaw(0.35F));
    const Matrix4 sourceAtCapture =
        Multiply(cameraInStation, stationAtCapture);
    const auto anchor = bfvr::stereo::CaptureD3D8MountedCameraAnchor(
        sourceAtCapture,
        stationAtCapture);
    if (!anchor.has_value() || !NearlyEqual(*anchor, cameraInStation))
    {
        return false;
    }

    const Matrix4 movedStation = Multiply(
        Yaw(-0.6F),
        Translation(14.0F, 2.5F, 9.0F));
    const Matrix4 expected = Multiply(cameraInStation, movedStation);
    const auto decoupled =
        bfvr::stereo::ComposeD3D8MountedCameraFromAnchor(
            *anchor,
            movedStation);

    // This is the native camera if a child turret yaws after capture. It must
    // differ from the reconstructed station-relative camera.
    const Matrix4 nativeAimed = Multiply(
        Multiply(cameraInStation, Yaw(kHalfPi)),
        movedStation);
    return decoupled.has_value() && NearlyEqual(*decoupled, expected) &&
        !NearlyEqual(*decoupled, nativeAimed);
}

bool TestGunPivotTranslationIsNotInherited() noexcept
{
    const Matrix4 station = Identity();
    const Matrix4 cameraInStation = Translation(0.0F, 1.2F, -0.8F);
    const auto anchor = bfvr::stereo::CaptureD3D8MountedCameraAnchor(
        cameraInStation,
        station);
    if (!anchor.has_value())
    {
        return false;
    }
    const Matrix4 nativeAimed = Multiply(cameraInStation, Yaw(kHalfPi));
    const auto decoupled =
        bfvr::stereo::ComposeD3D8MountedCameraFromAnchor(*anchor, station);
    return decoupled.has_value() &&
        NearlyEqual(*decoupled, cameraInStation) &&
        !NearlyEqual(*decoupled, nativeAimed);
}

bool TestInvalidTransformsFailClosed() noexcept
{
    Matrix4 invalid = Identity();
    invalid.values[0][0] = 0.0F;
    return !bfvr::stereo::CaptureD3D8MountedCameraAnchor(
                Identity(),
                invalid).has_value() &&
        !bfvr::stereo::ComposeD3D8MountedCameraFromAnchor(
                invalid,
                Identity()).has_value();
}

bool TestDefaultCoupledSeatLocalTogglePolicy() noexcept
{
    bfvr::stereo::MountedCameraControlState state = {};
    auto transition = bfvr::stereo::UpdateMountedCameraControl(
        state,
        0x1000,
        0);
    if (!transition.stationChanged || state.decoupled)
    {
        return false;
    }
    transition = bfvr::stereo::UpdateMountedCameraControl(
        state,
        0x1000,
        1);
    if (!transition.toggleApplied || !transition.decouplingChanged ||
        !state.decoupled)
    {
        return false;
    }
    transition = bfvr::stereo::UpdateMountedCameraControl(
        state,
        0x1000,
        2);
    if (!transition.toggleApplied || state.decoupled)
    {
        return false;
    }
    (void)bfvr::stereo::UpdateMountedCameraControl(state, 0x1000, 3);
    transition = bfvr::stereo::UpdateMountedCameraControl(
        state,
        0x2000,
        3);
    if (!transition.stationChanged || !transition.decouplingChanged ||
        state.decoupled)
    {
        return false;
    }
    transition = bfvr::stereo::UpdateMountedCameraControl(
        state,
        0x2000,
        4);
    if (!transition.toggleApplied || !state.decoupled)
    {
        return false;
    }
    transition = bfvr::stereo::UpdateMountedCameraControl(state, 0, 4);
    return transition.stationChanged && transition.decouplingChanged &&
        !state.decoupled;
}

bool TestToggleOutsideStationIsConsumedAndIgnored() noexcept
{
    bfvr::stereo::MountedCameraControlState state = {};
    const auto ignored = bfvr::stereo::UpdateMountedCameraControl(
        state,
        0,
        1);
    const auto entered = bfvr::stereo::UpdateMountedCameraControl(
        state,
        0x3000,
        1);
    return ignored.toggleIgnored && !state.decoupled &&
        entered.stationChanged && !entered.toggleApplied;
}

bool TestMountedVisibilityMarginIsStateLocal() noexcept
{
    return std::fabs(
               bfvr::stereo::SelectD3D8VisibilityFrustumVerticalScale(false) -
               1.50F) <= kTolerance &&
        std::fabs(
            bfvr::stereo::SelectD3D8VisibilityFrustumVerticalScale(true) -
            2.00F) <= kTolerance;
}
} // namespace

int main()
{
    if (!TestStationMotionSurvivesAndLaterGunAimDoesNot() ||
        !TestGunPivotTranslationIsNotInherited() ||
        !TestInvalidTransformsFailClosed() ||
        !TestDefaultCoupledSeatLocalTogglePolicy() ||
        !TestToggleOutsideStationIsConsumedAndIgnored() ||
        !TestMountedVisibilityMarginIsStateLocal())
    {
        std::fprintf(stderr, "Mounted-camera math tests failed.\n");
        return 1;
    }
    std::printf("Mounted-camera math tests passed.\n");
    return 0;
}
