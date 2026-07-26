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
    const D3D8RuntimeControllerSample& sample) noexcept;
void ClearAcceptedControllerInput() noexcept;

[[nodiscard]] bool ReadFreshAcceptedControllerInput(
    D3D8RuntimeControllerSample& sample,
    LONG& generation,
    DWORD maximumAgeMs) noexcept;

} // namespace bfvr
