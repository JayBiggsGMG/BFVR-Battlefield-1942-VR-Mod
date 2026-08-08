#pragma once

#include <cstdint>

namespace bfvr
{
constexpr std::int64_t kControllerRecenterHoldNanoseconds =
    2'500'000'000LL;

struct OpenXRControllerShortcutState
{
    std::int64_t recenterHoldStartedAt = 0;
    bool focusedSampleInitialized = false;
    bool mapActionWasPressed = false;
    bool rightSecondaryWasPressed = false;
    bool recenterFiredForHold = false;
};

struct OpenXRControllerShortcutInput
{
    std::int64_t sampleTime = 0;
    bool sessionFocused = false;
    bool mapActionPressed = false;
    bool rightSecondaryPressed = false;
};

struct OpenXRControllerShortcutOutput
{
    bool recenterRequested = false;
    bool mapToggleRequested = false;
};

[[nodiscard]] bool IsOpenXRMapActionPressed(
    bool leftHandPressed,
    bool rightHandPressed) noexcept;

[[nodiscard]] OpenXRControllerShortcutOutput
UpdateOpenXRControllerShortcuts(
    OpenXRControllerShortcutState& state,
    const OpenXRControllerShortcutInput& input) noexcept;
} // namespace bfvr
