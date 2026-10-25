#pragma once

#include "bfvr_shared_bridge.hpp"

#include <windows.h>
#include <d3d11.h>

namespace bfvr
{

struct D3D8To9InteropProbeResult
{
    HRESULT prerequisiteResult = E_PENDING;
    HRESULT factoryResult = E_PENDING;
    HRESULT adapterEnumerationResult = E_PENDING;
    HRESULT adapterDescriptionResult = E_PENDING;
    HRESULT createDeviceResult = E_PENDING;
    HRESULT openSharedResourceResult = E_PENDING;
    LUID producerAdapterLuid = {};
    LUID probeAdapterLuid = {};
    D3D_FEATURE_LEVEL featureLevel = static_cast<D3D_FEATURE_LEVEL>(0);
    wchar_t adapterDescription[128] = {};
    D3D11_TEXTURE2D_DESC openedTextureDescription = {};
    bool openedTexture = false;
};

// Opens one newly created legacy D3D9 shared texture through a temporary
// in-process D3D11 device on the producer's exact adapter. Some graphics
// stacks need this one-time interop initialization before a separate x64
// D3D11 process can open the same allocation. This is best-effort startup
// work: it retains no device or texture, copies no pixels, and never runs per
// frame.
[[nodiscard]] bool PrimeD3D8To9D3D11SharedTextureInterop(
    const BFVRD3D8To9SharedDeviceDiagnostics& producerDiagnostics,
    HANDLE legacySharedHandle,
    D3D8To9InteropProbeResult* probeResult = nullptr) noexcept;

} // namespace bfvr
