#pragma once

#include <openxr/openxr.h>

namespace bfvr
{
// BFVR uses OpenXR 1.0 core functionality plus separately advertised
// extensions. Requesting 1.0 keeps the same feature set compatible with
// runtimes which have not promoted their core API support to OpenXR 1.1.
inline constexpr XrVersion kRequestedOpenXRApiVersion =
    XR_MAKE_VERSION(1, 0, 0);

static_assert(XR_VERSION_MAJOR(kRequestedOpenXRApiVersion) == 1);
static_assert(XR_VERSION_MINOR(kRequestedOpenXRApiVersion) == 0);
} // namespace bfvr
