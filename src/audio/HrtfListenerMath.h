#pragma once

#include "stereo/StereoMath.h"

#include <optional>

namespace bfvr::audio
{
struct ListenerTransform
{
    stereo::Vec3 position = {};
    stereo::Vec3 front = {0.0F, 0.0F, 1.0F};
    stereo::Vec3 top = {0.0F, 1.0F, 0.0F};
};

// Composes OpenXR LOCAL head motion with the native DirectSound listener.
// The listener basis is treated as a D3D8 left-handed camera-to-world basis.
// Invalid or degenerate inputs fail closed so the caller can preserve the
// exact game-owned listener values.
[[nodiscard]] std::optional<ListenerTransform> ComposeHrtfListener(
    const ListenerTransform& nativeListener,
    const stereo::Pose& currentHead,
    float worldUnitsPerMeter) noexcept;
} // namespace bfvr::audio
