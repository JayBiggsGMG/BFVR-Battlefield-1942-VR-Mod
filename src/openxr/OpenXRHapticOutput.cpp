#include "openxr/OpenXRHapticOutput.h"

namespace bfvr
{

bool ApplyOpenXRHapticOutput(
    const PFN_xrApplyHapticFeedback apply,
    const XrSession session,
    const XrAction action,
    const std::array<XrPath, 2>& userPaths,
    const bool available,
    const OpenXRHapticEvent event,
    const std::uint32_t handMask) noexcept
{
    if (!available || apply == nullptr || session == XR_NULL_HANDLE ||
        action == XR_NULL_HANDLE)
    {
        return false;
    }
    const OpenXRHapticPulse pulse = OpenXRHapticPulseFor(event);
    if (pulse.amplitude <= 0.0F || pulse.durationNanoseconds <= 0)
    {
        return false;
    }
    const auto applyToPath = [&](const XrPath path) {
        XrHapticActionInfo actionInfo{XR_TYPE_HAPTIC_ACTION_INFO};
        actionInfo.action = action;
        actionInfo.subactionPath = path;
        XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
        vibration.duration = static_cast<XrDuration>(
            pulse.durationNanoseconds);
        vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
        vibration.amplitude = pulse.amplitude;
        return XR_SUCCEEDED(apply(
            session,
            &actionInfo,
            reinterpret_cast<const XrHapticBaseHeader*>(&vibration)));
    };
    if ((handMask & kOpenXRHapticHandBoth) == kOpenXRHapticHandBoth)
    {
        // One all-subaction call is explicitly simultaneous and cannot let a
        // second same-action request replace the first hand's pulse.
        return applyToPath(XR_NULL_PATH);
    }
    bool applied = false;
    for (std::size_t hand = 0; hand < userPaths.size(); ++hand)
    {
        if ((handMask & (1U << hand)) == 0 ||
            userPaths[hand] == XR_NULL_PATH)
        {
            continue;
        }
        applied = applyToPath(userPaths[hand]) || applied;
    }
    return applied;
}

} // namespace bfvr
