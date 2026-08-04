#pragma once

#include "stereo/StereoMath.h"

#include <optional>

namespace bfvr::stereo
{

// BF1942's fixed-function water highlight generates a reflection vector in
// the active replay camera space, then applies the stage-0 texture matrix that
// the game prepared from its logical camera. This builds a rotation-only
// change of basis from replay camera space back to that logical camera space
// and pre-multiplies it into the game's original texture transform.
//
// logicalCameraToWorld is the unmodified camera transform observed before BFVR
// composes HMD motion. replayView is the exact per-eye D3D8 View transform.
// Translation is intentionally ignored because reflection coordinates are
// directions and must remain independent of eye position.
[[nodiscard]] std::optional<Matrix4>
MakeD3D8WaterReflectionTextureTransform(
    const Matrix4& logicalCameraToWorld,
    const Matrix4& replayView,
    const Matrix4& originalTextureTransform) noexcept;

} // namespace bfvr::stereo
