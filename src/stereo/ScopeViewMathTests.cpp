#include "stereo/ScopeViewMath.h"

#include <cmath>
#include <cstdio>
#include <limits>

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

bool NearlyEqual(float left, float right) noexcept
{
    return std::fabs(left - right) <= kTolerance;
}

bool TestConfiguredFovBecomesExactRelativeScale() noexcept
{
    const auto scale =
        bfvr::stereo::ComputeD3D8ScopeProjectionScale(1.0F, 0.7F);
    const float expected = std::tan(0.5F) / std::tan(0.35F);
    return scale.has_value() && NearlyEqual(*scale, expected) &&
        *scale > 1.49F && *scale < 1.50F;
}

bool TestScopeAimSourceKeepsModeIndependentFromTransientFreshness() noexcept
{
    using bfvr::stereo::ScopeAimSource;
    using bfvr::stereo::SelectScopeAimSource;
    return SelectScopeAimSource(true, true, false, false, false, false) ==
            ScopeAimSource::Fresh &&
        SelectScopeAimSource(true, true, false, false, true, true) ==
            ScopeAimSource::Tracked &&
        SelectScopeAimSource(true, false, false, false, true, true) ==
            ScopeAimSource::Tracked &&
        SelectScopeAimSource(true, false, false, false, false, true) ==
            ScopeAimSource::Latched &&
        SelectScopeAimSource(true, false, true, false, true, true) ==
            ScopeAimSource::None &&
        SelectScopeAimSource(true, false, true, true, true, true) ==
            ScopeAimSource::Tracked &&
        SelectScopeAimSource(true, false, true, true, false, true) ==
            ScopeAimSource::Latched &&
        SelectScopeAimSource(true, false, true, true, false, false) ==
            ScopeAimSource::None &&
        SelectScopeAimSource(true, false, false, false, false, false) ==
            ScopeAimSource::None &&
        SelectScopeAimSource(false, true, false, true, true, true) ==
            ScopeAimSource::None;
}

bool TestDeathIsAHardScopeLifetimeBoundary() noexcept
{
    using bfvr::stereo::ShouldReleaseD3D8ScopeForPlayerLifecycle;
    return ShouldReleaseD3D8ScopeForPlayerLifecycle(
               true, false, true) &&
        !ShouldReleaseD3D8ScopeForPlayerLifecycle(
            true, true, true) &&
        !ShouldReleaseD3D8ScopeForPlayerLifecycle(
            false, false, true) &&
        !ShouldReleaseD3D8ScopeForPlayerLifecycle(
            true, false, false);
}

bool TestScopedFireRequiresExactWeaponAndSoldierLifetime() noexcept
{
    const int fireWeapon = 1;
    const int otherWeapon = 2;
    const int currentSoldier = 3;
    const int otherSoldier = 4;
    using bfvr::stereo::IsExactScopeFirePoseEligible;
    return IsExactScopeFirePoseEligible(
               true,
               &fireWeapon,
               &fireWeapon,
               &currentSoldier,
               &currentSoldier) &&
        !IsExactScopeFirePoseEligible(
            false,
            &fireWeapon,
            &fireWeapon,
            &currentSoldier,
            &currentSoldier) &&
        !IsExactScopeFirePoseEligible(
            true,
            &fireWeapon,
            &otherWeapon,
            &currentSoldier,
            &currentSoldier) &&
        !IsExactScopeFirePoseEligible(
            true,
            &fireWeapon,
            &fireWeapon,
            &currentSoldier,
            &otherSoldier) &&
        !IsExactScopeFirePoseEligible(
            true,
            nullptr,
            nullptr,
            &currentSoldier,
            &currentSoldier);
}

bool TestTrackedScopeAimRetainsAuthoritativeLocalCorrection() noexcept
{
    const Matrix4 bodyAtCapture = Yaw(0.35F);
    Matrix4 trackedAtCapture = Multiply(Yaw(-0.20F), bodyAtCapture);
    trackedAtCapture.values[3][0] = 10.0F;
    trackedAtCapture.values[3][1] = 2.0F;
    trackedAtCapture.values[3][2] = -4.0F;
    const Matrix4 itemCorrection = Yaw(0.12F);
    const Matrix4 authoritativeAtCapture = Multiply(
        itemCorrection,
        trackedAtCapture);
    const auto correction = bfvr::stereo::MakeD3D8ScopeAimCorrection(
        authoritativeAtCapture,
        trackedAtCapture);
    if (!correction.has_value())
    {
        return false;
    }

    Matrix4 trackedNow = Multiply(Yaw(0.55F), Yaw(-0.25F));
    trackedNow.values[3][0] = 18.0F;
    trackedNow.values[3][1] = 1.5F;
    trackedNow.values[3][2] = 7.0F;
    const Matrix4 expected = Multiply(itemCorrection, trackedNow);
    const auto corrected = bfvr::stereo::ApplyD3D8ScopeAimCorrection(
        *correction,
        trackedNow);
    if (!corrected.has_value())
    {
        return false;
    }
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            if (!NearlyEqual(
                    corrected->values[row][column],
                    expected.values[row][column]))
            {
                return false;
            }
        }
    }
    return true;
}

bool TestScopedOffHandSupportRemainsHeldRegardlessOfDistance() noexcept
{
    if (!bfvr::stereo::IsD3D8ScopeOffHandSupportHeld(
            true, true, true, true, 0.8F))
    {
        return false;
    }
    return !bfvr::stereo::IsD3D8ScopeOffHandSupportHeld(
               false, true, true, true, 0.8F) &&
        !bfvr::stereo::IsD3D8ScopeOffHandSupportHeld(
            true, false, true, true, 0.8F) &&
        !bfvr::stereo::IsD3D8ScopeOffHandSupportHeld(
            true, true, false, true, 0.8F) &&
        !bfvr::stereo::IsD3D8ScopeOffHandSupportHeld(
            true, true, true, false, 0.8F) &&
        !bfvr::stereo::IsD3D8ScopeOffHandSupportHeld(
            true, true, true, true, 0.44F);
}

bool TestInvalidFovFailsClosed() noexcept
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    return !bfvr::stereo::ComputeD3D8ScopeProjectionScale(
                -1.0F,
                0.7F).has_value() &&
        !bfvr::stereo::ComputeD3D8ScopeProjectionScale(
                1.0F,
                0.0F).has_value() &&
        !bfvr::stereo::ComputeD3D8ScopeProjectionScale(
                nan,
                0.7F).has_value();
}

bool TestScopeCameraUsesGunRotationAndHeadPosition() noexcept
{
    Matrix4 headCamera = Yaw(-0.3F);
    headCamera.values[3][0] = 12.0F;
    headCamera.values[3][1] = 3.5F;
    headCamera.values[3][2] = -8.0F;
    Matrix4 gun = Yaw(0.85F);
    gun.values[3][0] = 90.0F;
    gun.values[3][1] = 80.0F;
    gun.values[3][2] = 70.0F;

    const auto scoped =
        bfvr::stereo::MakeD3D8WeaponDirectedScopeCamera(headCamera, gun);
    if (!scoped.has_value())
    {
        return false;
    }
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            if (!NearlyEqual(
                    scoped->values[row][column],
                    gun.values[row][column]))
            {
                return false;
            }
        }
    }
    return NearlyEqual(scoped->values[3][0], 12.0F) &&
        NearlyEqual(scoped->values[3][1], 3.5F) &&
        NearlyEqual(scoped->values[3][2], -8.0F);
}

bool TestProjectionScalePreservesCentreAndDepth() noexcept
{
    Matrix4 projection = {};
    projection.values[0][0] = 1.1F;
    projection.values[1][1] = 1.9F;
    projection.values[2][0] = 0.08F;
    projection.values[2][1] = -0.04F;
    projection.values[2][2] = 1.01F;
    projection.values[2][3] = 1.0F;
    projection.values[3][2] = -0.1F;
    const Matrix4 original = projection;

    if (!bfvr::stereo::ApplyD3D8ScopeProjectionScale(
            projection,
            1.5F))
    {
        return false;
    }
    return NearlyEqual(projection.values[0][0], 1.65F) &&
        NearlyEqual(projection.values[1][1], 2.85F) &&
        NearlyEqual(projection.values[2][0], original.values[2][0]) &&
        NearlyEqual(projection.values[2][1], original.values[2][1]) &&
        NearlyEqual(projection.values[2][2], original.values[2][2]) &&
        NearlyEqual(projection.values[3][2], original.values[3][2]);
}

bool TestInvalidCameraAndProjectionFailClosed() noexcept
{
    Matrix4 invalidCamera = Identity();
    invalidCamera.values[0][0] = 0.0F;
    Matrix4 projection = Identity();
    const Matrix4 original = projection;
    return !bfvr::stereo::MakeD3D8WeaponDirectedScopeCamera(
                Identity(),
                invalidCamera).has_value() &&
        !bfvr::stereo::ApplyD3D8ScopeProjectionScale(
            projection,
            std::numeric_limits<float>::infinity()) &&
        NearlyEqual(projection.values[0][0], original.values[0][0]) &&
        NearlyEqual(projection.values[1][1], original.values[1][1]);
}

bool TestEyeFillingScopeQuadCoversAsymmetricFov() noexcept
{
    const bfvr::stereo::ScopeOverlayFov fov = {
        -0.70F,
        0.85F,
        0.75F,
        -0.65F};
    const auto size =
        bfvr::stereo::ComputeEyeFillingScopeOverlayQuadSize(
            fov,
            1.0F,
            1.02F);
    return size.has_value() &&
        NearlyEqual(
            size->widthMeters,
            2.0F * std::tan(0.85F) * 1.02F) &&
        NearlyEqual(
            size->heightMeters,
            2.0F * std::tan(0.75F) * 1.02F);
}

bool TestInvalidEyeFillingScopeQuadFailsClosed() noexcept
{
    const bfvr::stereo::ScopeOverlayFov valid = {
        -0.70F,
        0.70F,
        0.70F,
        -0.70F};
    bfvr::stereo::ScopeOverlayFov invalid = valid;
    invalid.angleRight = 1.7F;
    return !bfvr::stereo::ComputeEyeFillingScopeOverlayQuadSize(
                valid,
                0.0F).has_value() &&
        !bfvr::stereo::ComputeEyeFillingScopeOverlayQuadSize(
                invalid,
                1.0F).has_value();
}

bool TestEyeFillingScopeQuadFollowsEyePose() noexcept
{
    const float halfYaw = 0.785398163F * 0.5F;
    const bfvr::stereo::Pose eye = {
        {2.0F, 3.0F, 4.0F},
        {0.0F, std::sin(halfYaw), 0.0F, std::cos(halfYaw)}};
    const auto quad = bfvr::stereo::MakeEyeFillingScopeOverlayQuad(
        eye,
        {-0.70F, 0.80F, 0.75F, -0.65F});
    return quad.has_value() &&
        NearlyEqual(quad->pose.position.x, 1.2928932F) &&
        NearlyEqual(quad->pose.position.y, 3.0F) &&
        NearlyEqual(quad->pose.position.z, 3.2928932F) &&
        NearlyEqual(quad->pose.orientation.y, eye.orientation.y) &&
        NearlyEqual(quad->pose.orientation.w, eye.orientation.w) &&
        quad->widthMeters > 0.0F && quad->heightMeters > 0.0F;
}
} // namespace

int main()
{
    if (!TestConfiguredFovBecomesExactRelativeScale() ||
        !TestDeathIsAHardScopeLifetimeBoundary() ||
        !TestScopeAimSourceKeepsModeIndependentFromTransientFreshness() ||
        !TestScopedFireRequiresExactWeaponAndSoldierLifetime() ||
        !TestTrackedScopeAimRetainsAuthoritativeLocalCorrection() ||
        !TestScopedOffHandSupportRemainsHeldRegardlessOfDistance() ||
        !TestInvalidFovFailsClosed() ||
        !TestScopeCameraUsesGunRotationAndHeadPosition() ||
        !TestProjectionScalePreservesCentreAndDepth() ||
        !TestInvalidCameraAndProjectionFailClosed() ||
        !TestEyeFillingScopeQuadCoversAsymmetricFov() ||
        !TestInvalidEyeFillingScopeQuadFailsClosed() ||
        !TestEyeFillingScopeQuadFollowsEyePose())
    {
        std::fprintf(stderr, "Scope-view math tests failed.\n");
        return 1;
    }
    std::printf("Scope-view math tests passed.\n");
    return 0;
}
