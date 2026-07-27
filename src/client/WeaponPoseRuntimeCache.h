#pragma once

#include "stereo/StereoMath.h"

#include <windows.h>

namespace bfvr
{

// Publishes the exact fresh view-space orientation used by the visual weapon
// attachment. The fire path consumes this same transform so the rendered
// barrel and gameplay direction cannot establish parallel calibration bases.
// This cache carries derived pose data only and grants no write authority.
void PublishWeaponViewOffset(
    const stereo::Matrix4& viewOffset,
    LONG controllerGeneration) noexcept;
void ClearWeaponViewOffset() noexcept;
[[nodiscard]] bool ReadFreshWeaponViewOffset(
    stereo::Matrix4& viewOffset,
    LONG& controllerGeneration,
    DWORD maximumAgeMs) noexcept;

} // namespace bfvr
