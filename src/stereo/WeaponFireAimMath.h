#pragma once

#include "stereo/StereoMath.h"

#include <optional>

namespace bfvr::stereo
{

// Reorients BF1942's native local-infantry fire matrix with the exact rigid
// world-space attachment already used by the grip-driven visual weapon,
// preserving native translation. This makes gameplay direction follow the
// rendered weapon geometry instead of reconstructing a parallel runtime aim
// ray from a view-space offset.
//
// This changes no projectile origin by itself. WeaponFire_Core remains
// responsible for applying its native weapon/barrel offsets, spread, and
// projectile construction to the resulting rigid matrix.
[[nodiscard]] std::optional<Matrix4> MakeD3D8WorldAttachedWeaponFireMatrix(
    const Matrix4& nativeFireMatrix,
    const Matrix4& visualWeaponWorldAttachment) noexcept;

} // namespace bfvr::stereo
