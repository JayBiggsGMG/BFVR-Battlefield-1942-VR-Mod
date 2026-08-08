#pragma once

#include "client/D3D8SharedPresentationBridge.h"
#include "client/D3D8StereoProbeRecords.h"
#include "client/D3D8StereoReadback.h"

namespace bfvr::d3d8probe
{

bool TransferStereoFrameToSharedPresentation(
    const D3D8StereoReadbackApi& readbackApi,
    void* device,
    StereoFrameRecord& frame,
    DWORD worldClearColor,
    DWORD uiClearColor,
    D3D8SharedPresentationBridge& presentationBridge,
    const D3D8RuntimeRenderRequest& renderRequest,
    bool analyzePixels,
    const D3D8RuntimeUiPlacement& uiPlacement,
    const D3D8RuntimeMovementFrame& movementFrame,
    const D3D8RuntimeDepthFrame& depthFrame);

} // namespace bfvr::d3d8probe
