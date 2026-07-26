#pragma once

#include "stereo/D3D8SemanticDrawPolicy.h"

namespace bfvr::stereo
{

enum class D3D8FrameCompositionLayer
{
    WorldEyes,
    Ref2Ui
};

[[nodiscard]] D3D8FrameCompositionLayer SelectD3D8FrameCompositionLayer(
    D3D8SemanticDrawClass semanticClass) noexcept;

} // namespace bfvr::stereo
