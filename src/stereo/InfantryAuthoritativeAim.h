#pragma once

#include "stereo/StereoMath.h"

#include <cstdint>

namespace bfvr::stereo
{

struct InfantryAuthoritativeAimConfiguration
{
    // Retains the original live-calibrated angular-error to BF1942 logical
    // mouse-look conversion. BF1942 applies its own simulation-time scaling.
    float inputPerRadian = 10.67F;
    float deadzoneRadians = 0.0015F;
    float maximumAxis = 1.0F;
    float minimumHorizontalForward = 0.001F;
};

struct InfantryAuthoritativeAimState
{
    std::uintptr_t infantryLifetime = 0;
    std::uintptr_t itemLifetime = 0;
    std::uint64_t controllerSequence = 0;
    bool valid = false;
};

struct InfantryAuthoritativeAimOutput
{
    float mouseLookX = 0.0F;
    float mouseLookY = 0.0F;
    float targetYawRadians = 0.0F;
    float targetPitchRadians = 0.0F;
    float currentYawRadians = 0.0F;
    float currentPitchRadians = 0.0F;
    float yawErrorRadians = 0.0F;
    float pitchErrorRadians = 0.0F;
    bool targetAccepted = false;
    bool lifetimeCaptured = false;
};

void ResetInfantryAuthoritativeAim(
    InfantryAuthoritativeAimState& state) noexcept;

// Closes the shortest angular error between BFVR's final functional gun
// forward and BF1942's current authoritative soldier aim. It owns no camera,
// body transform, fire matrix, network packet, or non-infantry state.
[[nodiscard]] InfantryAuthoritativeAimOutput UpdateInfantryAuthoritativeAim(
    InfantryAuthoritativeAimState& state,
    bool enabled,
    bool targetTracked,
    const Vec3& targetForwardWorld,
    float currentYawRadians,
    float currentPitchRadians,
    std::uintptr_t infantryLifetime,
    std::uintptr_t itemLifetime,
    std::uint64_t controllerSequence,
    const InfantryAuthoritativeAimConfiguration& configuration = {}) noexcept;

} // namespace bfvr::stereo
