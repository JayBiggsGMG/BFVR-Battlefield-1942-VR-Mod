#pragma once

#include "stereo/VehicleMotionAimMath.h"

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

struct LocalPlayerMotionPose
{
    const void* controlObject = nullptr;
    stereo::Vec3 worldPosition = {};
};

struct LocalInfantryBodyPose
{
    const void* controlObject = nullptr;
    stereo::Matrix4 world = {};
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

// Queries the same per-PlayerControlObject weapon vector used by BF1942's
// native vehicle HUD. Only a valid non-empty vector proves an armed station;
// malformed, unreadable, or unavailable state remains Unknown so controller
// motion can fail closed without disabling independent stick mouse-look.
[[nodiscard]] stereo::VehicleMotionAimWeaponStatus
ReadOccupiedVehicleWeaponStatus(
    const void* currentControlObject) noexcept;

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

// Samples only the world-space origin of the local infantry/occupied control
// object through the repeatedly established +0x3C transformation getter.
// Its orientation is intentionally discarded so look, aim, and rotation in
// place cannot drive the comfort vignette.
[[nodiscard]] bool ReadLocalPlayerMotionPose(
    LocalPlayerMotionPose& motionPose) noexcept;

// Samples the alive local infantry control object's complete world transform.
// The RenderView comfort path consumes only its horizontal facing direction;
// weapon, input, networking, and the native source camera remain unchanged.
[[nodiscard]] bool ReadLocalInfantryBodyPose(
    LocalInfantryBodyPose& bodyPose) noexcept;

// Reads the same authoritative/root soldier transform as the camera comfort
// path, but returns only its normalized horizontal heading. This is distinct
// from BF1942's faster first-person look/aim heading.
[[nodiscard]] bool ReadLocalInfantryBodyYaw(float& yawRadians) noexcept;

} // namespace bfvr
