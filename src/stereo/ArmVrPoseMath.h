#pragma once

#include "stereo/StereoMath.h"

#include <array>
#include <cstdint>
#include <optional>

namespace bfvr::stereo
{

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
    // Keep the production visual wrist at the tracked grip position. A
    // nonzero controller-local lever makes rotation translate the hand and
    // attached item around a remote pivot. The field remains injectable for
    // focused math coverage and any future controller-specific experiment.
    Vec3 gripLocalWristOffset = {};
};

// Returns only the rotation-dependent change of an explicitly supplied
// controller-local wrist lever. Production leaves the lever at zero so hand
// rotation cannot translate the wrist. The result is in D3D8/Skeleton
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
