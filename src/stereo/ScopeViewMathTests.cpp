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

bool TestScopeAimSmoothingAttenuatesMicroMotionAndTranslation() noexcept
{
    bfvr::stereo::ScopeAimSmoothingState state = {};
    const void* const weapon = reinterpret_cast<const void*>(0x1000);
    const void* const soldier = reinterpret_cast<const void*>(0x2000);
    constexpr std::int64_t firstTime = 1'000'000'000;
    constexpr std::int64_t frameTime = 11'111'111;
    Matrix4 initial = Yaw(0.0F);
    initial.values[3][0] = 1.0F;
    const auto first = bfvr::stereo::UpdateD3D8ScopeAimSmoothing(
        state, initial, weapon, soldier, 10, firstTime, true);
    Matrix4 smallMove = Yaw(0.001F);
    smallMove.values[3][0] = 4.0F;
    smallMove.values[3][1] = 5.0F;
    smallMove.values[3][2] = 6.0F;
    const auto stabilized = bfvr::stereo::UpdateD3D8ScopeAimSmoothing(
        state, smallMove, weapon, soldier, 11, firstTime + frameTime, true);
    const auto duplicate = bfvr::stereo::UpdateD3D8ScopeAimSmoothing(
        state,
        Yaw(0.018F),
        weapon,
        soldier,
        11,
        firstTime + frameTime,
        true);
    if (!first.has_value() || !stabilized.has_value() ||
        !duplicate.has_value())
    {
        return false;
    }
    const float stabilizedYaw = std::atan2(
        stabilized->values[2][0],
        stabilized->values[2][2]);
    return NearlyEqual(first->values[2][2], 1.0F) &&
        stabilizedYaw > 0.00013F && stabilizedYaw < 0.00016F &&
        NearlyEqual(stabilized->values[3][0], 4.0F) &&
        NearlyEqual(stabilized->values[3][1], 5.0F) &&
        NearlyEqual(stabilized->values[3][2], 6.0F) &&
        NearlyEqual(
            duplicate->values[2][0], stabilized->values[2][0]) &&
        NearlyEqual(
            duplicate->values[2][2], stabilized->values[2][2]);
}

bool TestScopeAimSmoothingHandlesSustainedMotionAndBypassesAtBound() noexcept
{
    bfvr::stereo::ScopeAimSmoothingState state = {};
    const void* const weapon = reinterpret_cast<const void*>(0x1000);
    const void* const soldier = reinterpret_cast<const void*>(0x2000);
    constexpr std::int64_t firstTime = 1'000'000'000;
    constexpr std::int64_t frameTime = 11'111'111;
    const auto first = bfvr::stereo::UpdateD3D8ScopeAimSmoothing(
        state, Yaw(0.0F), weapon, soldier, 1, firstTime, true);
    const auto firstMove = bfvr::stereo::UpdateD3D8ScopeAimSmoothing(
        state,
        Yaw(0.001F),
        weapon,
        soldier,
        2,
        firstTime + frameTime,
        true);
    const auto secondMove = bfvr::stereo::UpdateD3D8ScopeAimSmoothing(
        state,
        Yaw(0.002F),
        weapon,
        soldier,
        3,
        firstTime + 2 * frameTime,
        true);
    const auto thirdMove = bfvr::stereo::UpdateD3D8ScopeAimSmoothing(
        state,
        Yaw(0.003F),
        weapon,
        soldier,
        4,
        firstTime + 3 * frameTime,
        true);
    const Matrix4 deliberateMove = Yaw(0.010F);
    const auto bypassed = bfvr::stereo::UpdateD3D8ScopeAimSmoothing(
        state,
        deliberateMove,
        weapon,
        soldier,
        5,
        firstTime + 4 * frameTime,
        true);
    if (!first.has_value() || !firstMove.has_value() ||
        !secondMove.has_value() || !thirdMove.has_value() ||
        !bypassed.has_value())
    {
        return false;
    }
    const float firstYaw = std::atan2(
        firstMove->values[2][0], firstMove->values[2][2]);
    const float secondYaw = std::atan2(
        secondMove->values[2][0], secondMove->values[2][2]);
    const float thirdYaw = std::atan2(
        thirdMove->values[2][0], thirdMove->values[2][2]);
    return firstYaw > 0.0F && firstYaw < 0.001F &&
        secondYaw > firstYaw && secondYaw < 0.002F &&
        thirdYaw > secondYaw && thirdYaw < 0.003F &&
        0.003F - thirdYaw <
            bfvr::stereo::kScopeAimSmoothingMaximumErrorRadians &&
        NearlyEqual(
            bypassed->values[2][0], deliberateMove.values[2][0]) &&
        NearlyEqual(
            bypassed->values[2][2], deliberateMove.values[2][2]);
}

bool TestScopeAimSmoothingResetsAtEveryDiscontinuity() noexcept
{
    bfvr::stereo::ScopeAimSmoothingState state = {};
    const void* const weapon = reinterpret_cast<const void*>(0x1000);
    const void* const soldier = reinterpret_cast<const void*>(0x2000);
    const void* const otherWeapon = reinterpret_cast<const void*>(0x3000);
    constexpr std::int64_t firstTime = 1'000'000'000;
    constexpr std::int64_t frameTime = 11'111'111;
    const auto first = bfvr::stereo::UpdateD3D8ScopeAimSmoothing(
        state, Yaw(0.0F), weapon, soldier, 1, firstTime, true);
    const auto filtered = bfvr::stereo::UpdateD3D8ScopeAimSmoothing(
        state,
        Yaw(0.001F),
        weapon,
        soldier,
        2,
        firstTime + frameTime,
        true);
    const Matrix4 disabledMove = Yaw(0.002F);
    const auto disabled = bfvr::stereo::UpdateD3D8ScopeAimSmoothing(
        state,
        disabledMove,
        weapon,
        soldier,
        3,
        firstTime + 2 * frameTime,
        false);
    const Matrix4 reenabledMove = Yaw(0.003F);
    const auto reenabled = bfvr::stereo::UpdateD3D8ScopeAimSmoothing(
        state,
        reenabledMove,
        weapon,
        soldier,
        4,
        firstTime + 3 * frameTime,
        true);
    const Matrix4 reversedTimeMove = Yaw(0.004F);
    const auto reversedTime = bfvr::stereo::UpdateD3D8ScopeAimSmoothing(
        state,
        reversedTimeMove,
        weapon,
        soldier,
        5,
        firstTime + 2 * frameTime,
        true);
    const Matrix4 lifetimeMove = Yaw(0.005F);
    const auto lifetimeChanged =
        bfvr::stereo::UpdateD3D8ScopeAimSmoothing(
            state,
            lifetimeMove,
            otherWeapon,
            soldier,
            6,
            firstTime + 4 * frameTime,
            true);
    const Matrix4 gapMove = Yaw(0.006F);
    const auto gapReset = bfvr::stereo::UpdateD3D8ScopeAimSmoothing(
        state,
        gapMove,
        otherWeapon,
        soldier,
        7,
        firstTime + 100'000'000,
        true);
    return first.has_value() && filtered.has_value() &&
        disabled.has_value() && reenabled.has_value() &&
        reversedTime.has_value() && lifetimeChanged.has_value() &&
        gapReset.has_value() &&
        NearlyEqual(disabled->values[2][0], disabledMove.values[2][0]) &&
        NearlyEqual(reenabled->values[2][0], reenabledMove.values[2][0]) &&
        NearlyEqual(
            reversedTime->values[2][0], reversedTimeMove.values[2][0]) &&
        NearlyEqual(
            lifetimeChanged->values[2][0], lifetimeMove.values[2][0]) &&
        NearlyEqual(gapReset->values[2][0], gapMove.values[2][0]);
}

bool TestScopeAimSmoothingDiagnosticsClassifyRawBoundaries() noexcept
{
    using bfvr::stereo::ScopeAimSmoothingDiagnostics;
    using bfvr::stereo::ScopeAimSmoothingOutcome;
    bfvr::stereo::ScopeAimSmoothingState state = {};
    ScopeAimSmoothingDiagnostics diagnostics = {};
    const void* const weapon = reinterpret_cast<const void*>(0x1000);
    const void* const soldier = reinterpret_cast<const void*>(0x2000);
    constexpr std::int64_t firstTime = 1'000'000'000;
    constexpr std::int64_t frameTime = 11'111'111;
    const auto first = bfvr::stereo::UpdateD3D8ScopeAimSmoothing(
        state,
        Yaw(0.0F),
        weapon,
        soldier,
        1,
        firstTime,
        true,
        &diagnostics);
    if (!first.has_value() ||
        diagnostics.outcome != ScopeAimSmoothingOutcome::FirstSample)
    {
        return false;
    }
    const auto smoothed = bfvr::stereo::UpdateD3D8ScopeAimSmoothing(
        state,
        Yaw(0.001F),
        weapon,
        soldier,
        2,
        firstTime + frameTime,
        true,
        &diagnostics);
    if (!smoothed.has_value() ||
        diagnostics.outcome != ScopeAimSmoothingOutcome::Smoothed ||
        diagnostics.elapsedNanoseconds != frameTime ||
        diagnostics.angularErrorRadians <= 0.0F)
    {
        return false;
    }
    const auto duplicate = bfvr::stereo::UpdateD3D8ScopeAimSmoothing(
        state,
        Yaw(0.002F),
        weapon,
        soldier,
        2,
        firstTime + frameTime,
        true,
        &diagnostics);
    if (!duplicate.has_value() ||
        diagnostics.outcome !=
            ScopeAimSmoothingOutcome::DuplicateGeneration)
    {
        return false;
    }
    const auto gap = bfvr::stereo::UpdateD3D8ScopeAimSmoothing(
        state,
        Yaw(0.003F),
        weapon,
        soldier,
        3,
        firstTime + 100'000'000,
        true,
        &diagnostics);
    if (!gap.has_value() ||
        diagnostics.outcome != ScopeAimSmoothingOutcome::NonContinuousTime ||
        diagnostics.elapsedNanoseconds <=
            bfvr::stereo::kScopeAimSmoothingMaximumSampleIntervalNanoseconds)
    {
        return false;
    }
    const auto invalidTime = bfvr::stereo::UpdateD3D8ScopeAimSmoothing(
        state,
        Yaw(0.004F),
        weapon,
        soldier,
        4,
        0,
        true,
        &diagnostics);
    return invalidTime.has_value() &&
        diagnostics.outcome ==
            ScopeAimSmoothingOutcome::InvalidPredictedDisplayTime;
}

bool TestScopeAimSmoothingIsFrameRateAware() noexcept
{
    bfvr::stereo::ScopeAimSmoothingState ninetyHertz = {};
    bfvr::stereo::ScopeAimSmoothingState oneEightyHertz = {};
    const void* const weapon = reinterpret_cast<const void*>(0x1000);
    const void* const soldier = reinterpret_cast<const void*>(0x2000);
    constexpr std::int64_t firstTime = 1'000'000'000;
    const Matrix4 target = Yaw(0.001F);
    const auto initialNinety = bfvr::stereo::UpdateD3D8ScopeAimSmoothing(
        ninetyHertz, Yaw(0.0F), weapon, soldier, 1, firstTime, true);
    const auto atNinety = bfvr::stereo::UpdateD3D8ScopeAimSmoothing(
        ninetyHertz,
        target,
        weapon,
        soldier,
        2,
        firstTime + 11'111'111,
        true);
    const auto initialOneEighty =
        bfvr::stereo::UpdateD3D8ScopeAimSmoothing(
        oneEightyHertz, Yaw(0.0F), weapon, soldier, 1, firstTime, true);
    const auto halfwayOneEighty =
        bfvr::stereo::UpdateD3D8ScopeAimSmoothing(
        oneEightyHertz,
        target,
        weapon,
        soldier,
        2,
        firstTime + 5'555'556,
        true);
    const auto atOneEighty = bfvr::stereo::UpdateD3D8ScopeAimSmoothing(
        oneEightyHertz,
        target,
        weapon,
        soldier,
        3,
        firstTime + 11'111'111,
        true);
    if (!initialNinety.has_value() || !initialOneEighty.has_value() ||
        !halfwayOneEighty.has_value() || !atNinety.has_value() ||
        !atOneEighty.has_value())
    {
        return false;
    }
    const float ninetyYaw = std::atan2(
        atNinety->values[2][0], atNinety->values[2][2]);
    const float oneEightyYaw = std::atan2(
        atOneEighty->values[2][0], atOneEighty->values[2][2]);
    return std::fabs(ninetyYaw - oneEightyYaw) < 0.00005F;
}

bool TestAcceptedShotAwaitsOnlyExactOwnedScopeDecision() noexcept
{
    const void* const weapon = reinterpret_cast<const void*>(0x1000);
    const void* const soldier = reinterpret_cast<const void*>(0x2000);
    const void* const otherWeapon = reinterpret_cast<const void*>(0x3000);
    const void* const otherSoldier = reinterpret_cast<const void*>(0x4000);
    using bfvr::stereo::ShouldAwaitD3D8NativeScopeDecisionAfterAcceptedShot;
    return ShouldAwaitD3D8NativeScopeDecisionAfterAcceptedShot(
               weapon, weapon, soldier, true, weapon, soldier) &&
        !ShouldAwaitD3D8NativeScopeDecisionAfterAcceptedShot(
            weapon, weapon, soldier, false, weapon, soldier) &&
        !ShouldAwaitD3D8NativeScopeDecisionAfterAcceptedShot(
            weapon, weapon, soldier, true, otherWeapon, soldier) &&
        !ShouldAwaitD3D8NativeScopeDecisionAfterAcceptedShot(
            weapon, weapon, soldier, true, weapon, otherSoldier) &&
        !ShouldAwaitD3D8NativeScopeDecisionAfterAcceptedShot(
            nullptr, weapon, soldier, true, weapon, soldier);
}

bool TestPostShotScopeDecisionPreservesOrReleasesNativePolicy() noexcept
{
    using bfvr::stereo::
        ShouldReleaseD3D8OwnedScopeForNativePostShotState;
    return ShouldReleaseD3D8OwnedScopeForNativePostShotState(
               true, true, false) &&
        !ShouldReleaseD3D8OwnedScopeForNativePostShotState(
            true, true, true) &&
        !ShouldReleaseD3D8OwnedScopeForNativePostShotState(
            false, true, false) &&
        !ShouldReleaseD3D8OwnedScopeForNativePostShotState(
            true, false, false);
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
        !TestScopeAimSmoothingAttenuatesMicroMotionAndTranslation() ||
        !TestScopeAimSmoothingHandlesSustainedMotionAndBypassesAtBound() ||
        !TestScopeAimSmoothingResetsAtEveryDiscontinuity() ||
        !TestScopeAimSmoothingDiagnosticsClassifyRawBoundaries() ||
        !TestScopeAimSmoothingIsFrameRateAware() ||
        !TestAcceptedShotAwaitsOnlyExactOwnedScopeDecision() ||
        !TestPostShotScopeDecisionPreservesOrReleasesNativePolicy() ||
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
