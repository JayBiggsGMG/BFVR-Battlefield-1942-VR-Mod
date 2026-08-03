#include "client/DirectSoundImportRoute.h"

#define DIRECTSOUND_VERSION 0x0800
#include <mmreg.h>
#include <dsound.h>

#include <cstdio>

namespace
{
constexpr HRESULT kReplacementResult = static_cast<HRESULT>(0x81234567L);
unsigned long g_calls = 0;

HRESULT WINAPI ReplacementDirectSoundCreate8(
    const GUID*,
    IDirectSound8** directSound,
    IUnknown*)
{
    ++g_calls;
    if (directSound != nullptr)
    {
        *directSound = nullptr;
    }
    return kReplacementResult;
}
} // namespace

int main()
{
    const bfvr::DirectSoundImportRouteResult route =
        bfvr::RouteExecutableDirectSoundCreate8(
            &ReplacementDirectSoundCreate8);
    if (!route.routed || route.previous == nullptr)
    {
        std::fprintf(
            stderr,
            "DirectSound import route failed (error %lu).\n",
            route.error);
        return 1;
    }

    IDirectSound8* directSound = reinterpret_cast<IDirectSound8*>(1);
    const HRESULT result = DirectSoundCreate8(
        nullptr,
        &directSound,
        nullptr);
    if (result != kReplacementResult ||
        directSound != nullptr ||
        g_calls != 1)
    {
        std::fprintf(
            stderr,
            "Routed DirectSound call did not reach the replacement.\n");
        return 2;
    }
    return 0;
}
