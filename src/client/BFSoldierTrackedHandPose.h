#pragma once

#include "client/D3D8SharedPresentationBridge.h"
#include "stereo/StereoMath.h"

#include <array>
#include <optional>

namespace bfvr
{

struct BFSoldierTrackedHandPose
{
    stereo::Matrix4 local = {};
    stereo::Matrix4 world = {};
};

// Converts an accepted OpenXR grip to BF1942's calibrated soldier-local
// tracking frame, including the current stance camera translation, and then
// expresses the same pose in world space. It owns no binding or game state.
[[nodiscard]] std::optional<BFSoldierTrackedHandPose>
MakeBFSoldierTrackedHandPose(
    const D3D8RuntimeControllerHand& hand,
    const stereo::Matrix4& soldierWorld,
    const std::array<float, 3>& trackingOriginOffset,
    const std::array<float, 3>& stanceTranslation,
    float worldUnitsPerMetre) noexcept;

} // namespace bfvr
