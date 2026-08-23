#pragma once

#ifndef XR_NO_PROTOTYPES
#define XR_NO_PROTOTYPES
#endif
#include <openxr/openxr.h>

namespace bfvr
{

// Returns the VIEW-space roll that keeps a head-centred HUD's +Y axis aligned
// with application-space gravity while position, yaw, and pitch follow the
// headset. Invalid and near-vertical poses fail closed.
[[nodiscard]] bool BuildOpenXRGravityUprightViewRoll(
    const XrQuaternionf& headInLocalSpace,
    XrQuaternionf& viewRoll) noexcept;

} // namespace bfvr
