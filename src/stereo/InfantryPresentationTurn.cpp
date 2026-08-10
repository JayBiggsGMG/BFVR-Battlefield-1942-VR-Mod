#include "stereo/InfantryPresentationTurn.h"

#include <algorithm>
#include <cmath>

namespace
{

constexpr double kNanosecondsToSeconds = 1.0e-9;

bool IsConfigurationValid(
    const bfvr::stereo::InfantryPresentationTurnConfiguration& value) noexcept
{
    return std::isfinite(value.thumbstickDeadzone) &&
        value.thumbstickDeadzone >= 0.0F &&
        value.thumbstickDeadzone < 1.0F &&
        std::isfinite(value.responseExponent) &&
        value.responseExponent > 0.0F &&
        std::isfinite(value.smoothDegreesPerSecondAt100Percent) &&
        value.smoothDegreesPerSecondAt100Percent > 0.0F &&
        std::isfinite(value.maximumFrameSeconds) &&
        value.maximumFrameSeconds > 0.0F &&
        std::isfinite(value.snapRearmThreshold) &&
        std::isfinite(value.snapActivationThreshold) &&
        value.snapRearmThreshold >= 0.0F &&
        value.snapRearmThreshold < value.snapActivationThreshold &&
        value.snapActivationThreshold <= 1.0F &&
        std::isfinite(value.minimumSnapDegrees) &&
        std::isfinite(value.maximumSnapDegrees) &&
        value.minimumSnapDegrees > 0.0F &&
        value.minimumSnapDegrees <= value.maximumSnapDegrees;
}

float ApplyResponse(
    const float axis,
    const bfvr::stereo::InfantryPresentationTurnConfiguration&
        configuration) noexcept
{
    if (!std::isfinite(axis))
    {
        return 0.0F;
    }
    const float bounded = std::clamp(axis, -1.0F, 1.0F);
    const float magnitude = std::fabs(bounded);
    if (magnitude <= configuration.thumbstickDeadzone)
    {
        return 0.0F;
    }
    const float normalized =
        (magnitude - configuration.thumbstickDeadzone) /
        (1.0F - configuration.thumbstickDeadzone);
    return std::copysign(
        std::pow(normalized, configuration.responseExponent),
        bounded);
}

} // namespace

namespace bfvr::stereo
{

void ResetInfantryPresentationTurn(
    InfantryPresentationTurnState& state) noexcept
{
    state = {};
}

InfantryPresentationTurnOutput UpdateInfantryPresentationTurn(
    InfantryPresentationTurnState& state,
    const bool enabled,
    const bool smoothMode,
    const bool thumbstickActive,
    const bool quickMenuHeld,
    const float thumbstickX,
    const std::uint32_t smoothSpeedPercent,
    const std::uint32_t snapAngleDegrees,
    const std::int64_t predictedDisplayTime,
    const std::uintptr_t infantryLifetime,
    const InfantryPresentationTurnConfiguration& configuration) noexcept
{
    InfantryPresentationTurnOutput output = {};
    if (!enabled || infantryLifetime == 0 || predictedDisplayTime <= 0 ||
        !IsConfigurationValid(configuration))
    {
        ResetInfantryPresentationTurn(state);
        return output;
    }

    if (!state.valid || state.infantryLifetime != infantryLifetime ||
        state.smoothMode != smoothMode)
    {
        state.infantryLifetime = infantryLifetime;
        state.lastPredictedDisplayTime = predictedDisplayTime;
        state.smoothMode = smoothMode;
        state.snapArmed = true;
        state.valid = true;
        output.lifetimeCaptured = true;
        return output;
    }

    const std::int64_t elapsedNanoseconds =
        predictedDisplayTime - state.lastPredictedDisplayTime;
    state.lastPredictedDisplayTime = predictedDisplayTime;

    if (smoothMode)
    {
        if (elapsedNanoseconds <= 0)
        {
            return output;
        }
        output.deltaSeconds = static_cast<float>(
            static_cast<double>(elapsedNanoseconds) *
            kNanosecondsToSeconds);
        if (!std::isfinite(output.deltaSeconds) ||
            output.deltaSeconds > configuration.maximumFrameSeconds)
        {
            output.deltaSeconds = 0.0F;
            output.timingDiscontinuity = true;
            return output;
        }
        if (!thumbstickActive || quickMenuHeld)
        {
            return output;
        }
        output.response = ApplyResponse(thumbstickX, configuration);
        if (output.response == 0.0F)
        {
            return output;
        }
        const float boundedSpeedPercent = static_cast<float>(
            std::clamp(smoothSpeedPercent, 1U, 500U));
        output.deltaDegrees = output.response *
            configuration.smoothDegreesPerSecondAt100Percent *
            (boundedSpeedPercent / 100.0F) * output.deltaSeconds;
        output.smoothApplied = std::isfinite(output.deltaDegrees) &&
            output.deltaDegrees != 0.0F;
        if (!output.smoothApplied)
        {
            output.deltaDegrees = 0.0F;
        }
        return output;
    }

    if (!thumbstickActive)
    {
        state.snapArmed = true;
        return output;
    }
    if (!std::isfinite(thumbstickX))
    {
        return output;
    }
    const float axis = std::clamp(thumbstickX, -1.0F, 1.0F);
    const float magnitude = std::fabs(axis);
    if (magnitude < configuration.snapRearmThreshold)
    {
        state.snapArmed = true;
        return output;
    }
    if (!quickMenuHeld && state.snapArmed &&
        magnitude >= configuration.snapActivationThreshold)
    {
        const float boundedDegrees = std::clamp(
            static_cast<float>(snapAngleDegrees),
            configuration.minimumSnapDegrees,
            configuration.maximumSnapDegrees);
        output.deltaDegrees = std::copysign(boundedDegrees, axis);
        output.snapApplied = true;
        state.snapArmed = false;
    }
    return output;
}

} // namespace bfvr::stereo
