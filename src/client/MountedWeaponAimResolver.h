#pragma once

#include "stereo/StereoMath.h"

namespace bfvr
{

struct MountedWeaponStationPose
{
    const void* controlObject = nullptr;
    stereo::Matrix4 stationWorld = {};
};

// Resolves the occupied PlayerControlObject's first native weapon and samples
// the same nominal FireArms transformation selected by the firing path.
[[nodiscard]] bool InitializeMountedWeaponAimResolver(
    void* gameImage,
    void (*appendLog)(const wchar_t* message)) noexcept;
void ShutdownMountedWeaponAimResolver() noexcept;

[[nodiscard]] bool ReadMountedWeaponFirePose(
    void* currentControlObject,
    stereo::Matrix4& firePose) noexcept;

// Reads the live local player's occupied non-soldier control object, proves
// that its primary FireArms transform is currently available, and samples the
// stable PlayerControlObject transform above any child turret bundles.
[[nodiscard]] bool ReadOccupiedMountedWeaponStationPose(
    MountedWeaponStationPose& stationPose) noexcept;

} // namespace bfvr
