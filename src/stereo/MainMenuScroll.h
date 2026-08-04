#pragma once

#include <cmath>
#include <cstdint>

namespace bfvr::stereo
{

constexpr float kMainMenuScrollPressThreshold = 0.65F;
constexpr float kMainMenuScrollReleaseThreshold = 0.40F;
constexpr std::uint32_t kMainMenuScrollInitialRepeatDelayMs = 320;
constexpr std::uint32_t kMainMenuScrollRepeatIntervalMs = 110;

struct MainMenuScrollRepeatState
{
    int heldDirection = 0;
    std::uint32_t nextRepeatTimeMs = 0;
};

// Returns +1 for one upward wheel detent, -1 for one downward detent, or zero.
// A new direction acts immediately, then repeats at a bounded menu-friendly
// cadence until the stick returns through the release threshold.
inline int UpdateMainMenuScrollRepeat(
    MainMenuScrollRepeatState& state,
    bool eligible,
    float verticalAxis,
    std::uint32_t nowMs) noexcept
{
    if (!eligible || !std::isfinite(verticalAxis))
    {
        state = {};
        return 0;
    }

    int requestedDirection = 0;
    if (verticalAxis >= kMainMenuScrollPressThreshold)
    {
        requestedDirection = 1;
    }
    else if (verticalAxis <= -kMainMenuScrollPressThreshold)
    {
        requestedDirection = -1;
    }
    else if (std::fabs(verticalAxis) <= kMainMenuScrollReleaseThreshold)
    {
        state = {};
        return 0;
    }
    else
    {
        requestedDirection = state.heldDirection;
    }

    if (requestedDirection == 0)
    {
        return 0;
    }
    if (requestedDirection != state.heldDirection)
    {
        state.heldDirection = requestedDirection;
        state.nextRepeatTimeMs =
            nowMs + kMainMenuScrollInitialRepeatDelayMs;
        return requestedDirection;
    }
    if (static_cast<std::int32_t>(
            nowMs - state.nextRepeatTimeMs) >= 0)
    {
        state.nextRepeatTimeMs =
            nowMs + kMainMenuScrollRepeatIntervalMs;
        return requestedDirection;
    }
    return 0;
}

} // namespace bfvr::stereo
