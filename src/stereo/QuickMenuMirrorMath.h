#pragma once

#include "stereo/StereoMath.h"

#include <array>

namespace bfvr::stereo
{

struct QuickMenuMirrorView
{
    Pose pose = {};
    float angleLeft = 0.0F;
    float angleRight = 0.0F;
    float angleUp = 0.0F;
    float angleDown = 0.0F;
};

struct QuickMenuMirrorCrop
{
    float sourceScaleX = 1.0F;
    float sourceScaleY = 1.0F;
    float sourceOffsetX = 0.0F;
    float sourceOffsetY = 0.0F;
};

struct QuickMenuMirrorVertex
{
    float clipX = 0.0F;
    float clipY = 0.0F;
    float clipZ = 0.0F;
    float clipW = 1.0F;
};

// Projects a LOCAL-space OpenXR quad into the centre-cropped desktop preview
// of one eye. Vertices are ordered as a D3D triangle strip: bottom-left,
// top-left, bottom-right, top-right. Clip W retains perspective interpolation.
[[nodiscard]] bool ProjectQuickMenuQuadToMirror(
    const Pose& quadPose,
    float widthMeters,
    float heightMeters,
    const QuickMenuMirrorView& eye,
    const QuickMenuMirrorCrop& crop,
    std::array<QuickMenuMirrorVertex, 4>& vertices) noexcept;

} // namespace bfvr::stereo
