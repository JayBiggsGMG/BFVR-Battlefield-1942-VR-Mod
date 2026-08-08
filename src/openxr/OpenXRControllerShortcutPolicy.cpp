#include "openxr/OpenXRControllerShortcutPolicy.h"

namespace bfvr
{
bool IsOpenXRMapActionPressed(
    bool leftHandPressed,
    bool rightHandPressed) noexcept
{
    return leftHandPressed || rightHandPressed;
}

OpenXRControllerShortcutOutput UpdateOpenXRControllerShortcuts(
    OpenXRControllerShortcutState& state,
    const OpenXRControllerShortcutInput& input) noexcept
{
    OpenXRControllerShortcutOutput output = {};
    if (!input.sessionFocused)
    {
        state = {};
        return output;
    }

    if (!state.focusedSampleInitialized)
    {
        state.focusedSampleInitialized = true;
        state.mapActionWasPressed = input.mapActionPressed;
        state.rightSecondaryWasPressed = input.rightSecondaryPressed;
        // A button already held while focus is acquired must be released
        // before it can produce either shortcut.
        state.recenterFiredForHold = input.rightSecondaryPressed;
        return output;
    }

    output.mapToggleRequested =
        input.mapActionPressed && !state.mapActionWasPressed;
    state.mapActionWasPressed = input.mapActionPressed;

    if (!input.rightSecondaryPressed)
    {
        state.recenterHoldStartedAt = 0;
        state.recenterFiredForHold = false;
    }
    else if (!state.rightSecondaryWasPressed)
    {
        state.recenterHoldStartedAt = input.sampleTime;
        state.recenterFiredForHold = false;
    }
    else if (!state.recenterFiredForHold &&
        state.recenterHoldStartedAt > 0 &&
        input.sampleTime >= state.recenterHoldStartedAt &&
        input.sampleTime - state.recenterHoldStartedAt >=
            kControllerRecenterHoldNanoseconds)
    {
        output.recenterRequested = true;
        state.recenterFiredForHold = true;
    }
    state.rightSecondaryWasPressed = input.rightSecondaryPressed;
    return output;
}
} // namespace bfvr
