#pragma once

#include <cstdint>

namespace bfvr::stereo
{

struct InfantryPresentationTurnConfiguration
{
    float thumbstickDeadzone = 0.20F;
    float responseExponent = 1.65F;
    float smoothDegreesPerSecondAt100Percent = 63.0F;
    float maximumFrameSeconds = 0.050F;
    float snapRearmThreshold = 0.35F;
    float snapActivationThreshold = 0.72F;
    float minimumSnapDegrees = 15.0F;
    float maximumSnapDegrees = 90.0F;
};

struct InfantryPresentationTurnState
{
    std::uintptr_t infantryLifetime = 0;
    std::int64_t lastPredictedDisplayTime = 0;
    bool smoothMode = true;
    bool snapArmed = true;
    bool valid = false;
};

struct InfantryPresentationTurnOutput
{
    float deltaDegrees = 0.0F;
    float response = 0.0F;
    float deltaSeconds = 0.0F;
    bool smoothApplied = false;
    bool snapApplied = false;
    bool lifetimeCaptured = false;
    bool timingDiscontinuity = false;
};

void ResetInfantryPresentationTurn(
    InfantryPresentationTurnState& state) noexcept;

// Integrates local-only artificial turn at the predicted-time cadence of new
// VR presentation requests. It owns no BF1942 input, body, fire, packet, or
// non-infantry state. Smooth is velocity/time based; Snap emits one complete
// configured edge after the stick rearms near centre.
[[nodiscard]] InfantryPresentationTurnOutput UpdateInfantryPresentationTurn(
    InfantryPresentationTurnState& state,
    bool enabled,
    bool smoothMode,
    bool thumbstickActive,
    bool quickMenuHeld,
    float thumbstickX,
    std::uint32_t smoothSpeedPercent,
    std::uint32_t snapAngleDegrees,
    std::int64_t predictedDisplayTime,
    std::uintptr_t infantryLifetime,
    const InfantryPresentationTurnConfiguration& configuration = {}) noexcept;

} // namespace bfvr::stereo
