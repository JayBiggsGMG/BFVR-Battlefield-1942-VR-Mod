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
    const D3D8RuntimeControllerSample& nativeAimSample,
    const D3D8RuntimeView& matchingHead,
    bool matchingHeadTracked,
    float presentationBodyYawRadians,
    bool presentationBodyYawValid) noexcept;
void ClearAcceptedControllerInput() noexcept;

[[nodiscard]] bool ReadFreshAcceptedControllerInput(
    D3D8RuntimeControllerSample& sample,
    LONG& generation,
    DWORD maximumAgeMs) noexcept;

// Returns the presentation-rebased sample together with the exact infantry
// body yaw used to construct its tracking reference. The native arm callback
// can then compensate to its same-callback soldier transform instead of
// combining controller and body bases from different PlayerAction instants.
[[nodiscard]] bool ReadFreshAcceptedInfantryPresentationInput(
    D3D8RuntimeControllerSample& sample,
    float& presentationBodyYawRadians,
    bool& presentationBodyYawValid,
    LONG& generation,
    DWORD maximumAgeMs) noexcept;

// Returns the same accepted OpenXR sample before infantry presentation/body
// compensation. Native infantry aim consumes this source so body motion can
// never feed back into the hand delta submitted to BF1942.
[[nodiscard]] bool ReadFreshAcceptedNativeInfantryAimInput(
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

} // namespace bfvr
