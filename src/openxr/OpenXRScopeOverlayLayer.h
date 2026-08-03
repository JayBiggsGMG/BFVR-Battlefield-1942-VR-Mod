#pragma once

#ifndef XR_NO_PROTOTYPES
#define XR_NO_PROTOTYPES
#endif
#include <openxr/openxr.h>

#include <array>
#include <cstdint>

namespace bfvr
{

[[nodiscard]] XrPosef ComposeOpenXRPose(
    const XrPosef& parent,
    const XrPosef& local) noexcept;

// Builds two eye-exclusive VIEW-space quads from one monoscopic native scope
// raster. Each quad is centred on its eye's forward axis and sized to cover
// that eye's asymmetric runtime FOV.
[[nodiscard]] bool BuildOpenXREyeFillingScopeLayers(
    XrSpace viewSpace,
    XrSwapchain uiSwapchain,
    std::uint32_t uiWidth,
    std::uint32_t uiHeight,
    const XrPosef& headInLocalSpace,
    const std::array<XrView, 2>& viewsInLocalSpace,
    std::array<XrCompositionLayerQuad, 2>& layers) noexcept;

} // namespace bfvr
