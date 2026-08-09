#pragma once

#include "client/D3D8SharedPresentationBridge.h"

#include <windows.h>

namespace bfvr
{

// The x64 OpenXR presenter is the sole producer. It publishes only samples
// already accepted by the presentation bridge for the exact render request.
// Reads are lock-free because the native input thread cannot wait for a
// graphics/render thread without risking a game stall.
void PublishAcceptedControllerInput(
    const D3D8RuntimeControllerSample& sample,
    const D3D8RuntimeView& matchingHead,
    bool matchingHeadTracked) noexcept;
void ClearAcceptedControllerInput() noexcept;

[[nodiscard]] bool ReadFreshAcceptedControllerInput(
    D3D8RuntimeControllerSample& sample,
    LONG& generation,
    DWORD maximumAgeMs) noexcept;

// Returns the same focused controller sample together with the centre-head
// pose derived from the exact render request that accepted it. This keeps the
// grip/HMD relation at one OpenXR predicted display time for weapon-pose
// presentation math; it carries no game state or input write authority.
[[nodiscard]] bool ReadFreshAcceptedWeaponTracking(
    D3D8RuntimeControllerSample& sample,
    D3D8RuntimeView& matchingHead,
    LONG& generation,
    DWORD maximumAgeMs) noexcept;

// Publishes only the controller-authored infantry yaw that BFVR also submits
// through BF1942's native logical mouse-look path. The cumulative fixed-point
// value lets the render thread apply the same artificial turn to the shared
// headset/controller anchor without delaying or rewriting game/network state.
void PublishControllerInfantryTurnIntent(float degrees) noexcept;

[[nodiscard]] LONG ReadControllerInfantryTurnIntentMillidegrees() noexcept;

} // namespace bfvr
