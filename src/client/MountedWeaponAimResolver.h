#pragma once

#include "stereo/StereoMath.h"

namespace bfvr
{

// Resolves the occupied PlayerControlObject's first native weapon and samples
// the same nominal FireArms transformation selected by the firing path.
[[nodiscard]] bool InitializeMountedWeaponAimResolver(
    void* gameImage,
    void (*appendLog)(const wchar_t* message)) noexcept;
void ShutdownMountedWeaponAimResolver() noexcept;

[[nodiscard]] bool ReadMountedWeaponFirePose(
    void* currentControlObject,
    stereo::Matrix4& firePose) noexcept;

} // namespace bfvr
