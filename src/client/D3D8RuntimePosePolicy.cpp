#include "client/D3D8RuntimePosePolicy.h"

namespace bfvr
{

D3D8RuntimeView MakeD3D8OpenXRLocalOrigin() noexcept
{
    // D3D8RuntimeView's defaults are exactly position=(0,0,0) and
    // orientation=(0,0,0,1), the identity transform of OpenXR LOCAL.
    return {};
}

D3D8RuntimeFramePosePolicy MakeD3D8RuntimeFramePosePolicy(
    const D3D8RuntimeView& currentHead,
    bool headTracked) noexcept
{
    D3D8RuntimeFramePosePolicy result = {};
    result.currentHead = currentHead;
    result.headTracked = headTracked;
    result.renderViewReference = headTracked
        ? MakeD3D8OpenXRLocalOrigin()
        : currentHead;
    return result;
}

D3D8RuntimeView D3D8RuntimeFramePosePolicy::EyeReference(
    bool renderViewApplied) const noexcept
{
    return renderViewApplied ? currentHead : renderViewReference;
}

} // namespace bfvr
