#pragma once

#include "stereo/StereoMath.h"

#include <cstdint>
#include <optional>

namespace bfvr::stereo
{

struct HandWeaponRecoilAngles
{
    float pitchDegrees = 0.0F;
    float yawDegrees = 0.0F;
};

struct HandWeaponRecoilState
{
    HandWeaponRecoilAngles angles = {};
    std::uint64_t lastNativeSequence = 0;
};

enum class HandWeaponRecoilStepStatus
{
    Applied,
    Stale,
    Rejected
};

// Accumulates one exact paired output from BF1942's native pitch/yaw recoil
// accessors. The values are engine degree-angle deltas, not radians and not an
// absolute pose. Scaling changes amplitude only; sequence and timing remain
// native. The safety bound rejects a corrupt/unpaired stream instead of
// allowing it to rotate the authoritative controller weapon indefinitely.
[[nodiscard]] HandWeaponRecoilStepStatus AccumulateHandWeaponRecoilStep(
    HandWeaponRecoilState& state,
    std::uint64_t nativeSequence,
    float pitchStepDegrees,
    float yawStepDegrees,
    float pitchScale,
    float yawScale,
    float maximumAbsoluteAngleDegrees) noexcept;

// Frame-rate-independent exponential residual return. Native recoil samples
// must finish before the caller invokes this function, so the engine's own
// timed pattern is never filtered or replaced.
[[nodiscard]] bool RecoverHandWeaponRecoilToIdentity(
    HandWeaponRecoilState& state,
    float elapsedSeconds,
    float pitchHalfLifeSeconds,
    float yawHalfLifeSeconds) noexcept;

// Applies the current physical-gun recoil in the gun's local frame. BF1942's
// legacy camera View and a physical held gun rotate in opposite senses, so the
// engine degree angles are sign-inverted at this one matrix edge. Translation
// is copied exactly, keeping the right controller grip as the recoil pivot.
[[nodiscard]] std::optional<Matrix4> ApplyHandWeaponRecoilToGunPose(
    const Matrix4& gunWorld,
    const HandWeaponRecoilAngles& recoil) noexcept;

[[nodiscard]] bool IsHandWeaponRecoilAtIdentity(
    const HandWeaponRecoilState& state) noexcept;

} // namespace bfvr::stereo
