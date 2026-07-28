#pragma once

#include "client/D3D8SharedPresentationBridge.h"

namespace bfvr
{

// A per-request policy only. It intentionally owns no sampled tracking origin:
// valid poses are expressed against OpenXR LOCAL identity, and invalid tracking
// uses the request's own centre pose for a no-delta fallback.
struct D3D8RuntimeFramePosePolicy
{
    D3D8RuntimeView currentHead = {};
    D3D8RuntimeView renderViewReference = {};
    bool headTracked = false;

    [[nodiscard]] D3D8RuntimeView EyeReference(
        bool renderViewApplied) const noexcept;
};

[[nodiscard]] D3D8RuntimeView MakeD3D8OpenXRLocalOrigin() noexcept;
[[nodiscard]] D3D8RuntimeFramePosePolicy MakeD3D8RuntimeFramePosePolicy(
    const D3D8RuntimeView& currentHead,
    bool headTracked) noexcept;

} // namespace bfvr
