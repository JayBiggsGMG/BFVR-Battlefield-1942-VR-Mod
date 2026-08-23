#pragma once

#include "stereo/StereoMath.h"

#include <cstdint>
#include <optional>

namespace bfvr::stereo
{

inline constexpr std::uint32_t kD3D8ShadowCameraSpacePosition = 0x00020000;
inline constexpr std::uint32_t kD3D8ShadowTextureTransformCount2 = 2;

[[nodiscard]] constexpr bool IsD3D8ProjectedShadowTextureState(
    std::uint32_t texCoordIndex,
    std::uint32_t transformFlags) noexcept
{
    return texCoordIndex == kD3D8ShadowCameraSpacePosition &&
        transformFlags == kD3D8ShadowTextureTransformCount2;
}

// BF1942 applies dynamic soldier and vehicle shadows as projected textures on
// terrain. The game builds the stage-0 texture transform for its native View,
// while BFVR replays the terrain with a different View for each eye. Under
// D3D8's row-vector convention, changing only View evaluates the projected
// texture in the wrong camera space.
//
// Build an eye-specific texture transform such that:
//
//   world * replayView * correctedTexture
//       == world * sourceView * originalTexture
//
// This preserves the game's authored world-space shadow projection while the
// terrain geometry retains ordinary per-eye stereo transforms.
[[nodiscard]] std::optional<Matrix4>
MakeD3D8ProjectedShadowTextureTransform(
    const Matrix4& sourceView,
    const Matrix4& replayView,
    const Matrix4& originalTextureTransform) noexcept;

} // namespace bfvr::stereo
