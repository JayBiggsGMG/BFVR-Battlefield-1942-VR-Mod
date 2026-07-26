#include "client/D3D8ImportRoute.h"
#include "client/D3D8RuntimeRedirect.h"

#include <windows.h>

namespace
{
HMODULE g_module = nullptr;
HMODULE g_translatorModule = nullptr;
volatile LONG g_initializationStarted = 0;
}

// This deliberately tiny client has no D3D11 or DXGI dependency. Its only job
// is to establish whether BF1942 itself is compatible with the pinned
// translator before BFVR's renderer and OpenXR dependencies enter the process.
extern "C" __declspec(dllexport) DWORD WINAPI BFVRInitializeObserver(LPVOID)
{
    if (g_module == nullptr)
    {
        return 0;
    }
    if (InterlockedCompareExchange(
            &g_initializationStarted,
            1,
            0) != 0)
    {
        return 1;
    }

    const bfvr::D3D8RuntimeRedirectResult redirect =
        bfvr::LoadBundledD3D8To9(g_module);
    if (redirect.module == nullptr ||
        redirect.direct3DCreate8 == nullptr)
    {
        return 0;
    }

    const bfvr::D3D8ImportRouteResult route =
        bfvr::RouteExecutableDirect3DCreate8(
            reinterpret_cast<bfvr::D3D8CreateFunction>(
                redirect.direct3DCreate8));
    if (!route.routed)
    {
        FreeLibrary(redirect.module);
        return 0;
    }

    g_translatorModule = redirect.module;
    return 1;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
