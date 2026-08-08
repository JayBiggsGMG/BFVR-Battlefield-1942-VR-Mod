bool ApplyHapticFeedback(
    const OpenXRHapticEvent event,
    const std::uint32_t handMask) noexcept
{
    return ApplyOpenXRHapticOutput(
        api.applyHapticFeedback, session, controllerHapticAction,
        controllerUserPaths,
        initialized && sessionRunning && controllerInputAttached &&
            sessionState == XR_SESSION_STATE_FOCUSED &&
            quickMenu.ControllerHapticsEnabled(),
        event, handMask);
}

void UpdateQuickMenuAndHoverHaptics(
    const OpenXRPresentationFrameState& frame) noexcept
{
    quickMenu.Update(frame);
    const std::uint64_t target = quickMenu.HapticHoverTarget();
    if (IsNewHapticHoverTarget(lastHapticHoverTarget, target))
    {
        (void)ApplyHapticFeedback(
            OpenXRHapticEvent::Hover,
            kOpenXRHapticHandRight);
    }
    lastHapticHoverTarget = target;
}
