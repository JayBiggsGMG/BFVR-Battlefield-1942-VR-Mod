#include "stereo/D3D8FrameCompositionPolicy.h"

namespace bfvr::stereo
{

D3D8FrameCompositionLayer SelectD3D8FrameCompositionLayer(
    D3D8SemanticDrawClass semanticClass) noexcept
{
    switch (semanticClass)
    {
    case D3D8SemanticDrawClass::Ref2FontGlyphBatch:
    case D3D8SemanticDrawClass::Ref2MenuQuad:
        return D3D8FrameCompositionLayer::Ref2Ui;
    default:
        return D3D8FrameCompositionLayer::WorldEyes;
    }
}

} // namespace bfvr::stereo
