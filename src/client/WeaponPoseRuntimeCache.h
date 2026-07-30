#pragma once

#include "stereo/StereoMath.h"

#include <windows.h>

namespace bfvr
{

struct NativeArmWeaponVisualPose
{
    stereo::Matrix4 worldAttachment = {};
    stereo::Matrix4 nativeHandWorld = {};
    stereo::Matrix4 targetHandWorld = {};
    stereo::Matrix4 controllerGunWorld = {};
    const void* soldier = nullptr;
    LONG controllerGeneration = 0;
};

// Publishes the exact fresh view-space offset and its world-space conjugated
// attachment used by the visual weapon draw. The fire path consumes the latter
// so rendered-barrel and gameplay direction use one transform rather than
// reconstructing a parallel HMD/controller basis. This cache carries derived
// pose data only and grants no write authority.
void PublishWeaponVisualPose(
    const stereo::Matrix4& viewOffset,
    const stereo::Matrix4& worldAttachment,
    LONG controllerGeneration) noexcept;

// Native-arm publication additionally carries the source/solved hand anchors,
// the unmodified controller gun pose, and their BFSoldier lifetime. WeaponFire
// can reject an unrelated cinematic/death-camera matrix and still consume the
// gun basis rather than mistaking the anatomical hand-bone basis for a barrel.
void PublishNativeArmWeaponVisualPose(
    const stereo::Matrix4& viewOffset,
    const stereo::Matrix4& worldAttachment,
    const stereo::Matrix4& nativeHandWorld,
    const stereo::Matrix4& targetHandWorld,
    const stereo::Matrix4& controllerGunWorld,
    const void* soldier,
    LONG controllerGeneration) noexcept;

// Legacy view-offset-only publication remains available while callers migrate
// to PublishWeaponVisualPose. It deliberately does not manufacture a world
// attachment; consumers requiring exact visual/fire alignment must fail closed
// until the paired publication arrives.
void PublishWeaponViewOffset(
    const stereo::Matrix4& viewOffset,
    LONG controllerGeneration) noexcept;
void ClearWeaponViewOffset() noexcept;
[[nodiscard]] bool ReadFreshWeaponViewOffset(
    stereo::Matrix4& viewOffset,
    LONG& controllerGeneration,
    DWORD maximumAgeMs) noexcept;

// Reads the exact world-space attachment used by the most recent visual draw.
// The generation comes from the same accepted controller sample as the visual
// pose, allowing consumers to reject a cross-frame pairing.
[[nodiscard]] bool ReadFreshWeaponWorldAttachment(
    stereo::Matrix4& worldAttachment,
    LONG& controllerGeneration,
    DWORD maximumAgeMs) noexcept;

[[nodiscard]] bool ReadFreshNativeArmWeaponVisualPose(
    NativeArmWeaponVisualPose& pose,
    DWORD maximumAgeMs) noexcept;

} // namespace bfvr
