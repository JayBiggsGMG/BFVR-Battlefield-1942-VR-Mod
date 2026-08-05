#pragma once

#include "stereo/StereoMath.h"

namespace bfvr
{

struct MountedWeaponStationPose
{
    const void* controlObject = nullptr;
    stereo::Matrix4 stationWorld = {};
};

struct LocalPlayerControlContext
{
    const void* currentControlObject = nullptr;
    const void* defaultControlObject = nullptr;
    bool alive = false;
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

// Read-only local ownership query shared by the camera anchor. Comparing the
// current and default control objects is the established infantry/occupied
// boundary in both offline and multiplayer play; no object state is changed.
[[nodiscard]] bool ReadLocalPlayerControlContext(
    LocalPlayerControlContext& context) noexcept;

} // namespace bfvr
