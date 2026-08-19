#pragma once

#include "client/BFSoldierNativeArmMath.h"
#include "client/D3D8SharedPresentationBridge.h"

#include <array>
#include <optional>

namespace bfvr
{

struct BFSoldierVrArmTracking
{
    D3D8RuntimeControllerSample controllers = {};
    std::array<float, 3> headSkeletonPosition = {};
    LONG generation = 0;
};

struct BFSoldierVrPoseCameraTranslation
{
    std::array<float, 3> localDelta = {};
    LONG pose = 0;
};

// Safely reads the world transform exposed by BF1942's Object vtable.
[[nodiscard]] std::optional<native_arm_math::Matrix4>
ReadBf1942ObjectTransform(void* object) noexcept;

// Returns the current first-person pose-camera displacement relative to the
// standing pose. Function targets are verified by the owning hook before use.
[[nodiscard]] std::optional<BFSoldierVrPoseCameraTranslation>
ReadBFSoldierVrPoseCameraTranslation(
    void* soldier,
    void* getSoldierPoseTarget,
    void* getPoseCameraPositionTarget) noexcept;

// Reads the controller and matching centre-head from one accepted OpenXR
// generation, rebasing both into the callback's current infantry body frame.
[[nodiscard]] bool ReadFreshBFSoldierVrArmTracking(
    const native_arm_math::Matrix4& soldierTransform,
    DWORD maximumAgeMs,
    BFSoldierVrArmTracking& result) noexcept;

} // namespace bfvr
