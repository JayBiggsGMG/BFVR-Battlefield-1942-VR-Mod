#pragma once

#include "bfvr_shared_bridge.hpp"

#include <windows.h>

namespace bfvr
{

// Opens one newly created legacy D3D9 shared texture through a temporary
// in-process D3D11 device on the producer's exact adapter. Some graphics
// stacks need this one-time interop initialization before a separate x64
// D3D11 process can open the same allocation. This is best-effort startup
// work: it retains no device or texture, copies no pixels, and never runs per
// frame.
[[nodiscard]] bool PrimeD3D8To9D3D11SharedTextureInterop(
    const BFVRD3D8To9SharedDeviceDiagnostics& producerDiagnostics,
    HANDLE legacySharedHandle) noexcept;

} // namespace bfvr
