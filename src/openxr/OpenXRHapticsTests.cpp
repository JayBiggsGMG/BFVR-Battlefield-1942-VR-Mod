#include "openxr/OpenXRHaptics.h"
#include "openxr/OpenXRHapticOutput.h"

#include <array>
#include <cstdio>

namespace
{
std::array<XrPath, 4> g_paths = {};
std::size_t g_callCount = 0;

XrResult XRAPI_PTR CaptureHaptic(
    XrSession,
    const XrHapticActionInfo* actionInfo,
    const XrHapticBaseHeader* haptic)
{
    if (actionInfo == nullptr || haptic == nullptr ||
        haptic->type != XR_TYPE_HAPTIC_VIBRATION ||
        g_callCount >= g_paths.size())
    {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    g_paths[g_callCount++] = actionInfo->subactionPath;
    return XR_SUCCESS;
}
} // namespace

int main()
{
    const auto hover = bfvr::OpenXRHapticPulseFor(
        bfvr::OpenXRHapticEvent::Hover);
    const auto shot = bfvr::OpenXRHapticPulseFor(
        bfvr::OpenXRHapticEvent::Shot);
    const auto death = bfvr::OpenXRHapticPulseFor(
        bfvr::OpenXRHapticEvent::Death);
    const std::array<XrPath, 2> userPaths = {11, 22};
    const bool rightApplied = bfvr::ApplyOpenXRHapticOutput(
        CaptureHaptic,
        (XrSession)1,
        (XrAction)2,
        userPaths,
        true,
        bfvr::OpenXRHapticEvent::Shot,
        bfvr::kOpenXRHapticHandRight);
    const bool rightMapped = rightApplied && g_callCount == 1 &&
        g_paths[0] == userPaths[1];
    g_callCount = 0;
    const bool bothApplied = bfvr::ApplyOpenXRHapticOutput(
        CaptureHaptic,
        (XrSession)1,
        (XrAction)2,
        userPaths,
        true,
        bfvr::OpenXRHapticEvent::Death,
        bfvr::kOpenXRHapticHandBoth);
    const bool bothMapped = bothApplied && g_callCount == 1 &&
        g_paths[0] == XR_NULL_PATH;
    g_callCount = 0;
    const bool disabled = !bfvr::ApplyOpenXRHapticOutput(
        CaptureHaptic,
        (XrSession)1,
        (XrAction)2,
        userPaths,
        false,
        bfvr::OpenXRHapticEvent::Hover,
        bfvr::kOpenXRHapticHandRight) && g_callCount == 0;
    const bool passed = rightMapped && bothMapped && disabled &&
        hover.amplitude > 0.0F &&
        hover.amplitude < shot.amplitude &&
        shot.amplitude < death.amplitude &&
        hover.durationNanoseconds < shot.durationNanoseconds &&
        shot.durationNanoseconds < death.durationNanoseconds &&
        bfvr::IsNewHapticHoverTarget(0, 1) &&
        !bfvr::IsNewHapticHoverTarget(1, 1) &&
        bfvr::IsNewHapticHoverTarget(1, 2) &&
        !bfvr::IsNewHapticHoverTarget(2, 0) &&
        bfvr::kOpenXRHapticHandBoth ==
            (bfvr::kOpenXRHapticHandLeft |
             bfvr::kOpenXRHapticHandRight);
    if (!passed)
    {
        fwprintf(stderr, L"[FAIL] OpenXR haptic policy mismatch.\n");
        return 1;
    }
    wprintf(
        L"[PASS] OpenXR hover edges and hover/shot/death pulse profiles are deterministic.\n");
    return 0;
}
