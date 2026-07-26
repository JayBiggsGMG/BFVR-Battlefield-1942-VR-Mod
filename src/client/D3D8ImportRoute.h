#pragma once

#include <windows.h>

namespace bfvr
{
using D3D8CreateFunction = void* (WINAPI*)(UINT sdkVersion);

struct D3D8ImportRouteResult
{
    D3D8CreateFunction previous = nullptr;
    DWORD error = ERROR_SUCCESS;
    bool routed = false;
};

// Replaces only the main executable's named Direct3DCreate8 IAT entry.
// This function must be called outside DllMain.
[[nodiscard]] D3D8ImportRouteResult RouteExecutableDirect3DCreate8(
    D3D8CreateFunction replacement) noexcept;
} // namespace bfvr
