#pragma once

#include <windows.h>

struct IDirectSound8;
struct IUnknown;

namespace bfvr
{
using DirectSoundCreate8Function = HRESULT (WINAPI*)(
    const GUID* deviceGuid,
    IDirectSound8** directSound,
    IUnknown* outerUnknown);

struct DirectSoundImportRouteResult
{
    DirectSoundCreate8Function previous = nullptr;
    DWORD error = ERROR_SUCCESS;
    bool routed = false;
};

// Replaces only BF1942.exe's DirectSoundCreate8 IAT entry, accepting either
// the canonical name or DSOUND ordinal 11 used by the installed retail build.
// The root dsound proxy has already performed its process-attach patch by this
// point. This function must be called outside DllMain.
[[nodiscard]] DirectSoundImportRouteResult RouteExecutableDirectSoundCreate8(
    DirectSoundCreate8Function replacement) noexcept;
} // namespace bfvr
