#pragma once

#include "stereo/StereoMath.h"

#include <optional>

namespace bfvr::stereo
{

struct NativeArmFireAnchorDistances
{
    float nativeFireToHand = 0.0F;
    float solvedHandDisplacement = 0.0F;
};

// Unidentified/warm-up fire retains the original conservative neighborhood.
// Only an exact WeaponFire receiver/current-active-item match receives the
// slightly wider bound required by the observed valid first-spawn rifle call.
// The solved-hand and multi-metre cinematic protections remain common.
[[nodiscard]] float SelectD3D8NativeArmFireToHandLimit(
    bool exactCurrentActiveItemReceiver) noexcept;

[[nodiscard]] bool IsD3D8NativeArmFireAnchorWithinPolicy(
    const NativeArmFireAnchorDistances& distances,
    bool exactCurrentActiveItemReceiver) noexcept;

// Measures whether the native WeaponFire matrix belongs to the same local
// spatial neighborhood as the native/solved hand pair. This measurement is
// policy-free; invalid or non-rigid matrices fail closed.
[[nodiscard]] std::optional<NativeArmFireAnchorDistances>
MeasureD3D8NativeArmFireAnchorDistances(
    const Matrix4& nativeFireMatrix,
    const Matrix4& nativeHandWorld,
    const Matrix4& targetHandWorld) noexcept;

// Recovers the authored row-vector rotation that maps a native WeaponFire
// world basis back to BF1942's native right-hand bone basis. Because BF1942
// composes child/local transforms on the left, the returned correction must
// pre-multiply a desired controller gun world basis.
[[nodiscard]] std::optional<Matrix4>
MakeD3D8NativeHandFromFireRotation(
    const Matrix4& nativeFireMatrix,
    const Matrix4& nativeHandWorld) noexcept;

// Recovers the complete authored hand-from-functional rigid transform,
// including the local translation from a throwable/tool origin to the wrist.
[[nodiscard]] std::optional<Matrix4>
MakeD3D8NativeHandFromFunctionalTransform(
    const Matrix4& nativeFunctionalWorld,
    const Matrix4& nativeHandWorld) noexcept;

// Applies the recovered local hand-from-fire correction in BF1942's
// row-vector order: correction * controllerGunWorld. The controller position
// is retained so the rotational correction cannot create a world-space pivot.
[[nodiscard]] std::optional<Matrix4>
MakeD3D8ControllerDirectedNativeHandMatrix(
    const Matrix4& controllerGunWorld,
    const Matrix4& nativeHandFromFireRotation) noexcept;

// Inverts the complete authored hand-from-functional transform so a
// controller-owned anatomical wrist recovers both the item's direction and its
// origin offset without forcing the wrist to point like the item.
[[nodiscard]] std::optional<Matrix4>
MakeD3D8FunctionalFromNativeHandTransform(
    const Matrix4& nativeHandWorld,
    const Matrix4& nativeHandFromFunctionalTransform) noexcept;

// Applies the exact rigid world-space attachment already used by the
// grip-driven visual weapon to BF1942's native local-infantry fire matrix.
// Native-arm mode also moves the native fire origin with that attachment so
// projectile direction and origin follow the physically moved gun. The legacy
// visual-only route can retain the original native origin.
//
// WeaponFire_Core remains responsible for applying its weapon/barrel offsets,
// spread, and projectile construction to the resulting rigid matrix.
[[nodiscard]] std::optional<Matrix4> MakeD3D8WorldAttachedWeaponFireMatrix(
    const Matrix4& nativeFireMatrix,
    const Matrix4& visualWeaponWorldAttachment,
    bool moveNativeFireOrigin = false) noexcept;

// Replaces the ordinary-infantry fire parent basis with the exact rigid
// controller-driven held-gun pose. OpenXR aim orientation therefore owns the
// projectile direction directly instead of being inferred through BF1942's
// stock camera/hand orientation. Native WeaponFire_Core still applies its
// weapon/barrel offsets, spread, cadence, and projectile construction.
[[nodiscard]] std::optional<Matrix4>
MakeD3D8ControllerDirectedWeaponFireMatrix(
    const Matrix4& nativeFireMatrix,
    const Matrix4& controllerGunWorld,
    bool moveOriginToControllerGun = true) noexcept;

} // namespace bfvr::stereo
