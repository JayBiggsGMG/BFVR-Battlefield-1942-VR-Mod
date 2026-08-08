#pragma once

#include <cstdint>

namespace bfvr
{

enum class OpenXRHapticEvent : std::uint32_t
{
    Hover = 0,
    Shot,
    Death
};

constexpr std::uint32_t kOpenXRHapticHandLeft = 0x1;
constexpr std::uint32_t kOpenXRHapticHandRight = 0x2;
constexpr std::uint32_t kOpenXRHapticHandBoth =
    kOpenXRHapticHandLeft | kOpenXRHapticHandRight;

struct OpenXRHapticPulse
{
    float amplitude = 0.0F;
    std::int64_t durationNanoseconds = 0;
};

[[nodiscard]] constexpr OpenXRHapticPulse OpenXRHapticPulseFor(
    OpenXRHapticEvent event) noexcept
{
    switch (event)
    {
    case OpenXRHapticEvent::Hover:
        return {0.12F, 15'000'000};
    case OpenXRHapticEvent::Shot:
        return {0.55F, 45'000'000};
    case OpenXRHapticEvent::Death:
        return {0.80F, 250'000'000};
    default:
        return {};
    }
}

// Zero means no target. A target pulses only when the pointer enters it or
// moves directly to a different target, never once per rendered frame.
[[nodiscard]] constexpr bool IsNewHapticHoverTarget(
    std::uint64_t previous,
    std::uint64_t current) noexcept
{
    return current != 0 && current != previous;
}

} // namespace bfvr
