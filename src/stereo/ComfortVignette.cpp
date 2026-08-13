#include "stereo/ComfortVignette.h"

#include <algorithm>
#include <cmath>

namespace
{
bool IsFinite(const bfvr::stereo::Vec3& value) noexcept
{
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

} // namespace

namespace bfvr::stereo
{

float UpdateComfortVignetteMotionTarget(
    ComfortVignetteMotionState& state,
    const ComfortVignetteMotionSample& sample) noexcept
{
    if (!sample.valid || sample.contextToken == 0 ||
        sample.predictedDisplayTime <= 0 || !IsFinite(sample.worldPosition))
    {
        state = {};
        return 0.0F;
    }

    if (!state.valid || state.contextToken != sample.contextToken)
    {
        state.contextToken = sample.contextToken;
        state.predictedDisplayTime = sample.predictedDisplayTime;
        state.worldPosition = sample.worldPosition;
        state.valid = true;
        return 0.0F;
    }

    const std::int64_t elapsedNanoseconds =
        sample.predictedDisplayTime - state.predictedDisplayTime;
    const float elapsedSeconds =
        static_cast<float>(elapsedNanoseconds) * 1.0e-9F;
    const Vec3 delta = {
        sample.worldPosition.x - state.worldPosition.x,
        sample.worldPosition.y - state.worldPosition.y,
        sample.worldPosition.z - state.worldPosition.z};

    state.predictedDisplayTime = sample.predictedDisplayTime;
    state.worldPosition = sample.worldPosition;
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds <= 0.0F ||
        elapsedSeconds > kComfortVignetteMaximumSampleIntervalSeconds)
    {
        state.filteredSpeedMetersPerSecond = 0.0F;
        state.moving = false;
        return 0.0F;
    }

    const float distanceSquared =
        delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
    if (!std::isfinite(distanceSquared) || distanceSquared < 0.0F)
    {
        state = {};
        return 0.0F;
    }
    const float speed = std::sqrt(distanceSquared) / elapsedSeconds;
    if (!std::isfinite(speed) ||
        speed > kComfortVignetteMaximumPlausibleSpeedMetersPerSecond)
    {
        // Do not tunnel on spawn, teleport, map-load, or corrupt-object jumps.
        state = {};
        return 0.0F;
    }

    const float filterAlpha = 1.0F - std::exp(
        -elapsedSeconds / kComfortVignetteSpeedFilterSeconds);
    state.filteredSpeedMetersPerSecond +=
        (speed - state.filteredSpeedMetersPerSecond) * filterAlpha;
    if (!std::isfinite(state.filteredSpeedMetersPerSecond))
    {
        state = {};
        return 0.0F;
    }
    if (state.moving)
    {
        state.moving = state.filteredSpeedMetersPerSecond >
            kComfortVignetteMovementStopMetersPerSecond;
    }
    else
    {
        state.moving = state.filteredSpeedMetersPerSecond >=
            kComfortVignetteMovementStartMetersPerSecond;
    }
    return state.moving ? 1.0F : 0.0F;
}

float AdvanceComfortVignetteStrength(
    float currentStrength,
    float targetStrength,
    float deltaSeconds) noexcept
{
    const float current = std::isfinite(currentStrength)
        ? std::clamp(currentStrength, 0.0F, 1.0F)
        : 0.0F;
    const float target = std::isfinite(targetStrength)
        ? std::clamp(targetStrength, 0.0F, 1.0F)
        : 0.0F;
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F ||
        current == target)
    {
        return current;
    }
    const float transitionSeconds = target > current
        ? kComfortVignetteEaseInSeconds
        : kComfortVignetteEaseOutSeconds;
    const float maximumStep = std::clamp(
        deltaSeconds / transitionSeconds,
        0.0F,
        1.0F);
    if (target > current)
    {
        return std::min(current + maximumStep, target);
    }
    return std::max(current - maximumStep, target);
}

bool UpdateDeathComfortActive(
    DeathComfortState& state,
    bool enabled,
    std::int32_t deathSequence,
    bool localPlayerLifeKnown,
    bool localPlayerAlive,
    std::uint64_t nowMilliseconds) noexcept
{
    const bool newDeath = deathSequence != state.observedDeathSequence;
    state.observedDeathSequence = deathSequence;
    if (!enabled)
    {
        state.active = false;
        state.activeUntilMilliseconds = 0;
        return false;
    }
    if (newDeath)
    {
        state.active = true;
        state.activeUntilMilliseconds = nowMilliseconds +
            kDeathComfortDurationMilliseconds;
    }
    if (state.active && localPlayerLifeKnown && localPlayerAlive)
    {
        state.active = false;
    }
    if (state.active && nowMilliseconds >= state.activeUntilMilliseconds)
    {
        state.active = false;
    }
    return state.active;
}

} // namespace bfvr::stereo
