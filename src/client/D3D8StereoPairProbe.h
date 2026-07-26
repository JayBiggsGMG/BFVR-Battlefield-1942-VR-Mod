#pragma once

#include "client/D3D8ObserverBridge.h"

namespace bfvr
{

// Replays one eligible full-size indexed world candidate into two transient
// BFVR-owned color/depth target pairs. The game draw is always forwarded
// first, and every D3D8 state value changed by the replay is restored before
// the detour returns.
void StartD3D8StereoPairProbe(const D3D8ObserverCallbacks& callbacks);

// Accumulates all eligible full-size draws from all four D3D8 draw families
// over one bounded Present-to-Present window into frame-lived BFVR-owned eye
// targets. Perspective geometry receives stereo transforms; pretransformed
// and non-perspective work keeps the game's transforms in both eyes. RTT and
// depthless targets are excluded, and every draw verifies exact restoration.
void StartD3D8StereoFrameProbe(const D3D8ObserverCallbacks& callbacks);

// Runs a bounded continuous form of the same partition through the x64 OpenXR
// companion. Every acknowledged frame uses predicted runtime eye poses/FOV,
// renders configurable-resolution world eyes for upscale into runtime-sized
// swapchains, and submits the logical-screen Ref2 surface through the separate
// UI layer.
void StartD3D8StereoFramePresentationProbe(
    const D3D8ObserverCallbacks& callbacks);

// Runs the same runtime-paced frame partition through the pinned D3D9Ex
// translator and the x64 no-HMD consumer. This is the flat cross-process gate
// for the GPU-resident transport used by the OpenXR companion.
void StartD3D8StereoFrameSharedTransportProbe(
    const D3D8ObserverCallbacks& callbacks);

} // namespace bfvr
