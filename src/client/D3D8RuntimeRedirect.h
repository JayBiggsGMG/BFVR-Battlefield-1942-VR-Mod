#pragma once

#include <windows.h>

namespace bfvr
{
struct D3D8RuntimeRedirectResult
{
    HMODULE module = nullptr;
    FARPROC direct3DCreate8 = nullptr;
    DWORD error = ERROR_SUCCESS;
    wchar_t path[MAX_PATH] = {};
};

// Loads BFVR's pinned, renamed d3d8to9 DLL from the BFVRClient directory.
// The caller retains the returned module for the lifetime of the redirected
// Direct3D8 interfaces.
[[nodiscard]] D3D8RuntimeRedirectResult LoadBundledD3D8To9(
    HMODULE clientModule) noexcept;
} // namespace bfvr
