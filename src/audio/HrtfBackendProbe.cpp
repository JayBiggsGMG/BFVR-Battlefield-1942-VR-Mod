#include "audio/OpenALHrtfRouterDiagnostics.h"

#include <windows.h>

#define DIRECTSOUND_VERSION 0x0800
#include <initguid.h>
#include <mmreg.h>
#include <dsound.h>

#include <array>
#include <cstdio>

namespace
{
using DirectSoundCreate8Function = HRESULT (WINAPI*)(
    const GUID* deviceGuid,
    IDirectSound8** directSound,
    IUnknown* outerUnknown);

HMODULE LoadRuntime(const wchar_t* name)
{
    std::array<wchar_t, MAX_PATH> path = {};
    const DWORD length = GetModuleFileNameW(
        nullptr,
        path.data(),
        static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
    {
        return nullptr;
    }
    wchar_t* separator = wcsrchr(path.data(), L'\\');
    if (separator == nullptr)
    {
        return nullptr;
    }
    *separator = L'\0';
    if (wcscat_s(
            path.data(),
            path.size(),
            L"\\runtime\\audio\\win32\\") != 0 ||
        wcscat_s(path.data(), path.size(), name) != 0)
    {
        return nullptr;
    }
    return LoadLibraryExW(
        path.data(),
        nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
            LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
}
} // namespace

int wmain()
{
    HMODULE actual = LoadRuntime(L"BFVROpenALSoft.dll");
    HMODULE router = actual == nullptr
        ? nullptr
        : LoadRuntime(L"dsoal-aldrv.dll");
    HMODULE dsoal = router == nullptr
        ? nullptr
        : LoadRuntime(L"BFVRDSoal.dll");
    if (actual == nullptr || router == nullptr || dsoal == nullptr)
    {
        std::fwprintf(
            stderr,
            L"HRTF backend probe could not load the private runtime (error %lu).\n",
            GetLastError());
        return 1;
    }

    const auto create = reinterpret_cast<DirectSoundCreate8Function>(
        GetProcAddress(dsoal, "DirectSoundCreate8"));
    const auto getDiagnostics = reinterpret_cast<
        bfvr::audio::GetOpenALHrtfRouterDiagnosticsFunction>(
            GetProcAddress(
                router,
                "BFVRGetOpenALHrtfRouterDiagnostics"));
    if (create == nullptr || getDiagnostics == nullptr)
    {
        std::fprintf(stderr, "HRTF backend probe exports are incomplete.\n");
        return 2;
    }

    IDirectSound8* directSound = nullptr;
    HRESULT result = create(nullptr, &directSound, nullptr);
    if (SUCCEEDED(result))
    {
        result = directSound->SetCooperativeLevel(
            GetDesktopWindow(),
            DSSCL_PRIORITY);
    }

    IDirectSoundBuffer* primary = nullptr;
    if (SUCCEEDED(result))
    {
        DSBUFFERDESC description = {};
        description.dwSize = sizeof(description);
        description.dwFlags =
            DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRL3D;
        result = directSound->CreateSoundBuffer(
            &description,
            &primary,
            nullptr);
    }

    IDirectSound3DListener* listener = nullptr;
    if (SUCCEEDED(result))
    {
        result = primary->QueryInterface(
            IID_IDirectSound3DListener,
            reinterpret_cast<void**>(&listener));
    }

    IDirectSoundBuffer* menuBuffer = nullptr;
    IDirectSound3DBuffer* menuSpatialBuffer = nullptr;
    DWORD menuMode = DS3DMODE_NORMAL;
    HRESULT menuModeResult = result;
    if (SUCCEEDED(menuModeResult))
    {
        WAVEFORMATEX format = {};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = 1;
        format.nSamplesPerSec = 44100;
        format.wBitsPerSample = 16;
        format.nBlockAlign = static_cast<WORD>(
            format.nChannels * format.wBitsPerSample / 8);
        format.nAvgBytesPerSec =
            format.nSamplesPerSec * format.nBlockAlign;

        DSBUFFERDESC description = {};
        description.dwSize = sizeof(description);
        description.dwFlags =
            DSBCAPS_CTRL3D | DSBCAPS_CTRLVOLUME | DSBCAPS_LOCSOFTWARE;
        description.dwBufferBytes = format.nAvgBytesPerSec / 10;
        description.lpwfxFormat = &format;
        menuModeResult = directSound->CreateSoundBuffer(
            &description,
            &menuBuffer,
            nullptr);
    }
    if (SUCCEEDED(menuModeResult))
    {
        menuModeResult = menuBuffer->QueryInterface(
            IID_IDirectSound3DBuffer,
            reinterpret_cast<void**>(&menuSpatialBuffer));
    }
    if (SUCCEEDED(menuModeResult))
    {
        menuModeResult = menuSpatialBuffer->SetMode(
            DS3DMODE_DISABLE,
            DS3D_IMMEDIATE);
    }
    if (SUCCEEDED(menuModeResult))
    {
        menuModeResult = menuSpatialBuffer->GetMode(&menuMode);
    }

    bfvr::audio::OpenALHrtfRouterDiagnostics diagnostics = {};
    const bool readDiagnostics = getDiagnostics(&diagnostics) != 0;
    std::printf(
        "HRESULT=0x%08lX contexts=%lu forced=%lu successful=%lu "
        "hrtfStatus=%ld malformed=%lu menuMode=%lu menuModeHRESULT=0x%08lX\n",
        static_cast<unsigned long>(result),
        diagnostics.createContextCalls,
        diagnostics.forcedHrtfCalls,
        diagnostics.successfulContextCalls,
        diagnostics.lastHrtfStatus,
        diagnostics.malformedAttributeLists,
        menuMode,
        static_cast<unsigned long>(menuModeResult));

    if (menuSpatialBuffer != nullptr)
    {
        menuSpatialBuffer->Release();
    }
    if (menuBuffer != nullptr)
    {
        menuBuffer->Release();
    }

    if (listener != nullptr)
    {
        listener->Release();
    }
    if (primary != nullptr)
    {
        primary->Release();
    }
    if (directSound != nullptr)
    {
        directSound->Release();
    }

    return SUCCEEDED(result) &&
        readDiagnostics &&
        diagnostics.forcedHrtfCalls != 0 &&
        diagnostics.successfulContextCalls != 0 &&
        diagnostics.lastHrtfStatus == 1 &&
        SUCCEEDED(menuModeResult) &&
        menuMode == DS3DMODE_DISABLE
        ? 0
        : 3;
}
