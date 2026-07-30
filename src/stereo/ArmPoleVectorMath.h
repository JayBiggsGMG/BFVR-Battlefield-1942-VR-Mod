#pragma once

#include <array>
#include <optional>

namespace bfvr::stereo
{

struct ArmPoleVectorInput
{
    std::array<float, 3> shoulder = {};
    std::array<float, 3> handTarget = {};
    std::array<float, 3> previousPole = {};
    bool hasPreviousPole = false;
    bool leftArm = false;
    bool preserveNative = false;
};

struct ArmPoleVectorResult
{
    std::array<float, 3> pole = {};
    bool usedPreviousPole = false;
    bool usedFallbackAxis = false;
};

// Computes a direction-only rotate-plane pole in the same component frame as
// the shoulder and hand target. The primary policy is a fixed, mirrored
// outward/down/back human-arm direction. It is projected away from the
// shoulder-to-hand axis before being given to the native Maya solver.
[[nodiscard]] std::optional<ArmPoleVectorResult>
ComputeArmPoleVector(const ArmPoleVectorInput& input) noexcept;

} // namespace bfvr::stereo
