#pragma once

#include "stereo/StereoMath.h"

#include <cstdint>

#include <windows.h>

namespace bfvr
{

enum class InfantryAuthoritativeAimTargetKind
{
    FunctionalWeapon,
    GadgetPointer,
    ScopedWeapon,
};

struct InfantryAuthoritativeAimRuntimeSample
{
    stereo::Vec3 targetForwardWorld = {};
    stereo::Vec3 nativeCameraForwardWorld = {};
    const void* soldier = nullptr;
    const void* item = nullptr;
    LONG targetControllerGeneration = 0;
    LONG nativeCameraRenderSequence = 0;
    DWORD nativeCameraAgeMs = 0;
    float bodyYawRadians = 0.0F;
    float nativePitchDegrees = 0.0F;
    float nativeYawOffsetDegrees = 0.0F;
    float currentYawRadians = 0.0F;
    float currentPitchRadians = 0.0F;
    InfantryAuthoritativeAimTargetKind targetKind =
        InfantryAuthoritativeAimTargetKind::FunctionalWeapon;
};

// Resolves one exact local-infantry lifetime. The target is the same final
// functional gun basis used by visual arms/scope presentation, including
// established two-hand steering and weapon recoil. Gadget slots retain the
// established raw aim-pointer policy because they have no functional barrel.
// Current authority comes from BF1942's untouched pre-VR source camera. Live
// accepted-shot correlation proves that forward is the local direction passed
// to WeaponFire_Core, while root plus replicated relative yaw is not a complete
// instantaneous firing direction. The raw soldier fields remain diagnostics;
// no game memory is written.
[[nodiscard]] bool ReadInfantryAuthoritativeAimRuntimeSample(
    const void* expectedSoldier,
    InfantryAuthoritativeAimRuntimeSample& sample) noexcept;

// The RenderView detour publishes the original source before any BFVR camera
// composition. Publication is exact-soldier scoped, short-lived, and grants no
// camera, input, fire, or network write authority.
void PublishInfantryNativeAimCamera(
    const void* soldier,
    const stereo::Matrix4& sourceCameraWorld,
    LONG renderSequence) noexcept;
void ClearInfantryNativeAimCamera() noexcept;

[[nodiscard]] const wchar_t* InfantryAuthoritativeAimTargetKindName(
    InfantryAuthoritativeAimTargetKind kind) noexcept;

} // namespace bfvr
