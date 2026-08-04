#pragma once

#include "bfvr_shared_bridge.hpp"
#include "d3d8.hpp"

namespace bfvr::shared
{

bool RunD3D8To9CrossProcessProbe(
    IDirect3DDevice8* device,
    BFVRD3D8To9CreateSharedRenderTargetFn createSharedTarget,
    BFVRD3D8To9CreateTextureBackedDepthStencilFn createDepthTarget,
    BFVRD3D8To9ResolveDepthToSharedTargetFn resolveDepthTarget,
    BFVRD3D8To9WaitForGpuFn waitForGpu,
    const wchar_t* consumerPath,
    const wchar_t* consumerLogPath,
    bool enableAmbientOcclusion,
    bool enableScreenSpaceGlobalIllumination,
    bool enableWaterReflections);

} // namespace bfvr::shared
