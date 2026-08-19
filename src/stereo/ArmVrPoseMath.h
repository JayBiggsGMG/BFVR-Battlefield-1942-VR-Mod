#pragma once

#include "stereo/StereoMath.h"

#include <array>
#include <cstdint>
#include <optional>

namespace bfvr::stereo
{

// Owner-accepted body-local visible-wrist calibration. These values were
// measured in-headset after the VR-owned shoulder/elbow solve was accepted.
constexpr std::array<std::int32_t, 3>
    kRightHandPositionCalibrationCentimeters = {-5, 4, 2};
constexpr std::array<std::int32_t, 3>
    kLeftHandPositionCalibrationCentimeters = {-1, 8, -8};

struct ArmVrShoulderAnchorInput
{
    // All positions are already in BF1942's local Skeleton component frame.
    std::array<float, 3> trackedHead = {};
    std::array<float, 3> trackingToSkeleton = {};
    std::array<float, 3> stanceTranslation = {};
    float halfShoulderWidth = 0.18F;
    float headToShoulderDrop = 0.20F;
};

struct ArmVrShoulderAnchors
{
    std::array<float, 3> right = {};
    std::array<float, 3> left = {};
};

// Establishes a stable body-frame shoulder pair from the tracked head. It
// intentionally consumes no BF1942 weapon-animation shoulder/root transform.
[[nodiscard]] std::optional<ArmVrShoulderAnchors>
ComputeArmVrShoulderAnchors(
    const ArmVrShoulderAnchorInput& input) noexcept;

struct ArmVrWristOffsetInput
{
    Quaternion referenceGripOrientation = {};
    Quaternion currentGripOrientation = {};
    // OpenXR grip-local coordinates. +Z is wristward for the recorded
    // palm-to-wrist calibration experiment.
    Vec3 gripLocalWristOffset = {0.0F, 0.0F, 0.08F};
};

// Returns only the rotation-dependent change of a controller-local wrist
// lever arm. The reference pose therefore produces zero displacement and
// cannot introduce a spawn/item-switch jump. The result is in D3D8/Skeleton
// coordinates and is visual-arm-only.
[[nodiscard]] std::optional<std::array<float, 3>>
ComputeArmVrWristOffsetDelta(
    const ArmVrWristOffsetInput& input) noexcept;

// Adds one body-local hand-position calibration to an already
// solved Skeleton target. X is right, Y is up, and Z is forward.
[[nodiscard]] std::optional<std::array<float, 3>>
ApplyArmVrHandPositionCalibration(
    const std::array<float, 3>& target,
    const std::array<std::int32_t, 3>& offsetCentimeters) noexcept;

} // namespace bfvr::stereo
