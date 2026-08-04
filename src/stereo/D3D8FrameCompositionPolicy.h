#pragma once

#include "stereo/D3D8DrawPolicy.h"
#include "stereo/D3D8SemanticDrawPolicy.h"

namespace bfvr::stereo
{

enum class D3D8FrameCompositionLayer
{
    WorldEyes,
    Ref2Ui
};

struct D3D8FrameCompletionFacts
{
    bool hasMirroredDraws = false;
    bool stateRestorationExact = false;
    bool worldEyesHaveColor = false;
    bool eyeImagesDiffer = false;
    bool layerPartitionExact = false;
    bool uiLayerHasContent = false;
    bool frameTransferred = false;
    bool presentationMode = false;
};

[[nodiscard]] D3D8FrameCompositionLayer SelectD3D8FrameCompositionLayer(
    D3D8SemanticDrawClass semanticClass,
    D3D8DrawPolicy drawPolicy) noexcept;

// A standalone capture needs both stereo world eyes and the UI layer to prove
// its diagnostic result. A live compositor frame instead needs an exact,
// transferred visual layer: either world eyes or a valid Ref2-only transition
// layer. Death and menu transitions can legitimately omit world geometry.
[[nodiscard]] bool IsD3D8FrameCompositionComplete(
    const D3D8FrameCompletionFacts& facts) noexcept;

} // namespace bfvr::stereo
