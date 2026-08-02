#pragma once

#include "client/D3D8StereoProbeRecords.h"

namespace bfvr
{

struct D3D8WorldCrosshairRenderFrame
{
    void* device = nullptr;
    void* colorTargets[2] = {};
    void* depthTargets[2] = {};
    d3d8probe::D3DViewport viewport = {};
    d3d8probe::D3DMatrix eyeViews[2] = {};
    d3d8probe::D3DMatrix eyeProjections[2] = {};
};

void InitializeD3D8WorldCrosshairRenderer(
    void (*appendLog)(const wchar_t* message));
void ShutdownD3D8WorldCrosshairRenderer() noexcept;

// Draws the two aligned PNG layers into both owned world targets immediately
// before frame transport. It never changes BF1942's source backbuffer, depth,
// hit timer, weapon state, or physics.
[[nodiscard]] bool RenderD3D8WorldCrosshair(
    const D3D8WorldCrosshairRenderFrame& frame) noexcept;

} // namespace bfvr
