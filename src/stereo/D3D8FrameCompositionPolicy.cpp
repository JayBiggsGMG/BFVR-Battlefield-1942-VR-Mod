#include "stereo/D3D8FrameCompositionPolicy.h"

namespace bfvr::stereo
{

D3D8FrameCompositionLayer SelectD3D8FrameCompositionLayer(
    D3D8SemanticDrawClass semanticClass,
    D3D8DrawPolicy drawPolicy) noexcept
{
    // A full-size draw that does not consume stereo perspective is screen-space
    // presentation by construction. Replaying it into the differently shaped
    // world-eye targets changes its viewport mapping and gives each eye a
    // separate copy. Keep all such work on the single native-size UI layer;
    // exact semantic classes remain useful attribution, not a compatibility
    // requirement for mod-authored UI renderers.
    if (drawPolicy != D3D8DrawPolicy::StereoPerspective)
    {
        return D3D8FrameCompositionLayer::Ref2Ui;
    }

    switch (semanticClass)
    {
    case D3D8SemanticDrawClass::Ref2FontGlyphBatch:
    case D3D8SemanticDrawClass::Ref2MenuQuad:
        return D3D8FrameCompositionLayer::Ref2Ui;
    default:
        return D3D8FrameCompositionLayer::WorldEyes;
    }
}

bool IsD3D8FrameCompositionComplete(
    const D3D8FrameCompletionFacts& facts) noexcept
{
    if (!facts.hasMirroredDraws || !facts.stateRestorationExact ||
        !facts.layerPartitionExact)
    {
        return false;
    }
    if (facts.presentationMode)
    {
        return facts.frameTransferred &&
            (facts.worldEyesHaveColor || facts.uiLayerHasContent);
    }
    return facts.worldEyesHaveColor && facts.eyeImagesDiffer &&
        facts.uiLayerHasContent;
}

} // namespace bfvr::stereo
