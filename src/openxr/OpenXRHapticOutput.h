#pragma once

#include "openxr/OpenXRHaptics.h"

#include <windows.h>

#ifndef XR_NO_PROTOTYPES
#define XR_NO_PROTOTYPES
#endif
#include <openxr/openxr.h>

#include <array>
#include <cstdint>

namespace bfvr
{

[[nodiscard]] bool ApplyOpenXRHapticOutput(
    PFN_xrApplyHapticFeedback apply,
    XrSession session,
    XrAction action,
    const std::array<XrPath, 2>& userPaths,
    bool available,
    OpenXRHapticEvent event,
    std::uint32_t handMask) noexcept;

} // namespace bfvr
