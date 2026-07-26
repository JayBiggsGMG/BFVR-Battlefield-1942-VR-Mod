#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

namespace bfvr
{
struct D3DSystemRuntime
{
    using D3D11CreateDeviceFunction =
        decltype(&::D3D11CreateDevice);
    using CreateDXGIFactory1Function =
        decltype(&::CreateDXGIFactory1);

    HMODULE d3d11Module = nullptr;
    HMODULE dxgiModule = nullptr;
    D3D11CreateDeviceFunction createD3D11Device = nullptr;
    CreateDXGIFactory1Function createDXGIFactory1 = nullptr;
    DWORD error = ERROR_SUCCESS;

    [[nodiscard]] bool IsAvailable() const noexcept
    {
        return d3d11Module != nullptr &&
            dxgiModule != nullptr &&
            createD3D11Device != nullptr &&
            createDXGIFactory1 != nullptr;
    }
};

// Resolves only the Windows system copies and retains them for process life.
// This prevents BFVRClient's pre-device load from eagerly resolving a
// game-local DXGI proxy.
[[nodiscard]] const D3DSystemRuntime& GetD3DSystemRuntime() noexcept;
} // namespace bfvr
