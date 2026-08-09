#pragma once

#include "stereo/StereoMath.h"

#include <windows.h>

#include <optional>

namespace bfvr
{

using HandWeaponRecoilLogCallback = void (*)(const wchar_t* message);

// Compile-time tuning boundary. Native BF1942 pitch/yaw values, sample order,
// randomization, and table timing remain unchanged. Only the requested support
// amplitude policy and post-native residual return are BFVR-owned.
inline constexpr float kTwoHandPitchRecoilScale = 2.0F;
inline constexpr float kTwoHandYawRecoilScale = 2.0F;
inline constexpr float kOneHandPitchRecoilScale = 4.0F;
inline constexpr float kOneHandYawRecoilScale = 4.0F;
inline constexpr float kHandWeaponRecoilReturnHalfLifeSeconds = 0.10F;

void StartHandWeaponRecoilRuntime(
    HandWeaponRecoilLogCallback appendLog) noexcept;
void StopHandWeaponRecoilRuntime() noexcept;
void LogHandWeaponRecoilSummary() noexcept;

// WeaponFire_Core is the accepted shot boundary. Support is the established
// BFVR off-hand binding, not raw squeeze/proximity. Its scale is latched for
// the native recoil sequence begun by this shot.
void NotifyHandWeaponRecoilShot(
    const void* soldier,
    const void* weapon,
    bool bothHands) noexcept;

// ControllerInputOverlay publishes its hysteresis-filtered logical Fire state
// so residual recovery starts only after release, never between automatic
// rounds. This state has no camera or BF1942 input write authority.
void PublishHandWeaponRecoilFireHeld(bool held) noexcept;

// Called only after the profiled yaw->pitch accessor pair has been observed for
// the exact current local camera soldier. Recoil index is the native table
// index before BF1942's caller decrements it.
void PublishPairedNativeHandWeaponRecoil(
    const void* soldier,
    float pitchDegrees,
    float yawDegrees,
    int recoilIndex) noexcept;

// Returns a weapon-only pose for the same soldier/item lifetime. The raw input
// pose is never modified and no view/camera service consumes this API.
[[nodiscard]] std::optional<stereo::Matrix4>
MakeCurrentHandWeaponRecoilPose(
    const stereo::Matrix4& rawGunWorld,
    const void* soldier,
    const void* weapon) noexcept;

} // namespace bfvr
