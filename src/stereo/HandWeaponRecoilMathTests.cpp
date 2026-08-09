#include "stereo/HandWeaponRecoilMath.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

namespace
{

constexpr float kTolerance = 0.0001F;

bool NearlyEqual(const float left, const float right) noexcept
{
    return std::fabs(left - right) <= kTolerance;
}

bfvr::stereo::Matrix4 Identity(const float x = 0.0F,
                               const float y = 0.0F,
                               const float z = 0.0F) noexcept
{
    bfvr::stereo::Matrix4 result = {};
    result.values[0][0] = 1.0F;
    result.values[1][1] = 1.0F;
    result.values[2][2] = 1.0F;
    result.values[3][0] = x;
    result.values[3][1] = y;
    result.values[3][2] = z;
    result.values[3][3] = 1.0F;
    return result;
}

bool TestLongNativePatternReturnsExactly() noexcept
{
    // Installed BF1942.exe 0x009581E8, consumed from index 20 down to 1.
    constexpr std::array<float, 21> table = {
        0.0F, 0.02F, 0.03F, 0.05F, 0.07F, 0.09F, 0.12F,
        0.12F, 0.09F, 0.07F, 0.05F, 0.03F, 0.01F, -0.01F,
        -0.03F, -0.05F, -0.07F, -0.10F, -0.13F, -0.16F, -0.20F};
    bfvr::stereo::HandWeaponRecoilState state = {};
    std::uint64_t sequence = 0;
    for (std::size_t index = 20; index > 0; --index)
    {
        if (bfvr::stereo::AccumulateHandWeaponRecoilStep(
                state,
                ++sequence,
                table[index] * 1.25F,
                table[index] * -0.50F,
                1.0F,
                1.0F,
                45.0F) !=
            bfvr::stereo::HandWeaponRecoilStepStatus::Applied)
        {
            return false;
        }
    }
    return bfvr::stereo::IsHandWeaponRecoilAtIdentity(state) &&
        NearlyEqual(state.angles.pitchDegrees, 0.0F) &&
        NearlyEqual(state.angles.yawDegrees, 0.0F);
}

bool TestShortNativePatternRetainsThenRecovers() noexcept
{
    // Installed BF1942.exe 0x0095823C, consumed from index 8 down to 1.
    constexpr std::array<float, 9> table = {
        0.0F, -0.01F, -0.03F, -0.05F, -0.07F,
        -0.10F, -0.13F, -0.16F, -0.20F};
    bfvr::stereo::HandWeaponRecoilState state = {};
    std::uint64_t sequence = 0;
    for (std::size_t index = 8; index > 0; --index)
    {
        (void)bfvr::stereo::AccumulateHandWeaponRecoilStep(
            state,
            ++sequence,
            table[index],
            table[index] * 0.4F,
            1.0F,
            1.0F,
            45.0F);
    }
    if (!NearlyEqual(state.angles.pitchDegrees, -0.75F) ||
        !NearlyEqual(state.angles.yawDegrees, -0.30F))
    {
        return false;
    }
    if (!bfvr::stereo::RecoverHandWeaponRecoilToIdentity(
            state, 0.10F, 0.10F, 0.10F) ||
        !NearlyEqual(state.angles.pitchDegrees, -0.375F) ||
        !NearlyEqual(state.angles.yawDegrees, -0.15F))
    {
        return false;
    }
    return bfvr::stereo::RecoverHandWeaponRecoilToIdentity(
               state, 2.0F, 0.10F, 0.10F) &&
        bfvr::stereo::IsHandWeaponRecoilAtIdentity(state);
}

bool TestOneHandScalingAndContinuousRecovery() noexcept
{
    bfvr::stereo::HandWeaponRecoilState twoHand = {};
    bfvr::stereo::HandWeaponRecoilState oneHand = {};
    (void)bfvr::stereo::AccumulateHandWeaponRecoilStep(
        twoHand, 1, -0.5F, 0.2F, 2.0F, 2.0F, 45.0F);
    (void)bfvr::stereo::AccumulateHandWeaponRecoilStep(
        oneHand, 1, -0.5F, 0.2F, 4.0F, 4.0F, 45.0F);
    if (!NearlyEqual(
            oneHand.angles.pitchDegrees,
            twoHand.angles.pitchDegrees * 2.0F) ||
        !NearlyEqual(
            oneHand.angles.yawDegrees,
            twoHand.angles.yawDegrees * 2.0F))
    {
        return false;
    }
    (void)bfvr::stereo::RecoverHandWeaponRecoilToIdentity(
        oneHand, 0.10F, 0.10F, 0.10F);
    const float recoveredPitch = oneHand.angles.pitchDegrees;
    (void)bfvr::stereo::AccumulateHandWeaponRecoilStep(
        oneHand, 2, -0.25F, 0.0F, 4.0F, 4.0F, 45.0F);
    return NearlyEqual(
        oneHand.angles.pitchDegrees,
        recoveredPitch - 1.00F);
}

bool TestSequenceAndSafetyBounds() noexcept
{
    bfvr::stereo::HandWeaponRecoilState state = {};
    if (bfvr::stereo::AccumulateHandWeaponRecoilStep(
            state, 1, -0.5F, 0.25F, 1.0F, 1.0F, 45.0F) !=
        bfvr::stereo::HandWeaponRecoilStepStatus::Applied)
    {
        return false;
    }
    const auto before = state;
    if (bfvr::stereo::AccumulateHandWeaponRecoilStep(
            state, 1, 10.0F, 10.0F, 1.0F, 1.0F, 45.0F) !=
            bfvr::stereo::HandWeaponRecoilStepStatus::Stale ||
        !NearlyEqual(state.angles.pitchDegrees, before.angles.pitchDegrees))
    {
        return false;
    }
    return bfvr::stereo::AccumulateHandWeaponRecoilStep(
               state, 2, 100.0F, 0.0F, 1.0F, 1.0F, 45.0F) ==
            bfvr::stereo::HandWeaponRecoilStepStatus::Rejected &&
        state.lastNativeSequence == 1;
}

bool TestGunPoseMovesAroundControllerPivot() noexcept
{
    const auto raw = Identity(1.25F, -0.50F, 3.0F);
    const auto recoiled = bfvr::stereo::ApplyHandWeaponRecoilToGunPose(
        raw,
        {-2.0F, 1.0F});
    if (!recoiled.has_value() ||
        !NearlyEqual(recoiled->values[3][0], raw.values[3][0]) ||
        !NearlyEqual(recoiled->values[3][1], raw.values[3][1]) ||
        !NearlyEqual(recoiled->values[3][2], raw.values[3][2]))
    {
        return false;
    }
    // Negative native pitch is the flat camera's upward kick. Moving it to a
    // physical gun sign-inverts at the matrix edge, producing barrel rise.
    return recoiled->values[2][1] > 0.0F &&
        !NearlyEqual(recoiled->values[2][0], 0.0F);
}

bool TestInvalidPoseRejected() noexcept
{
    auto invalid = Identity();
    invalid.values[0][0] = std::numeric_limits<float>::quiet_NaN();
    return !bfvr::stereo::ApplyHandWeaponRecoilToGunPose(
                invalid,
                {-1.0F, 0.0F})
                .has_value();
}

} // namespace

int main()
{
    if (!TestLongNativePatternReturnsExactly() ||
        !TestShortNativePatternRetainsThenRecovers() ||
        !TestOneHandScalingAndContinuousRecovery() ||
        !TestSequenceAndSafetyBounds() ||
        !TestGunPoseMovesAroundControllerPivot() ||
        !TestInvalidPoseRejected())
    {
        std::fprintf(stderr, "BFVR handweapon recoil math tests failed.\n");
        return 1;
    }
    std::printf("BFVR handweapon recoil math tests passed.\n");
    return 0;
}
