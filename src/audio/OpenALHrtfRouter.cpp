#include "audio/OpenALHrtfRouterDiagnostics.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstddef>

#define BFVR_OPENAL_FORWARD(name)                                         \
    static FARPROC g_forward_##name = nullptr;                           \
    extern "C" __declspec(naked) void __cdecl name()                    \
    {                                                                    \
        __asm { jmp dword ptr [g_forward_##name] }                       \
    }                                                                    \
    __pragma(comment(linker, "/export:" #name "=_" #name))
#include "audio/OpenALForwardedExports.inl"
#undef BFVR_OPENAL_FORWARD

namespace
{
using ALCboolean = char;
using ALCint = int;
struct ALCcontext;
struct ALCdevice;

using AlcCreateContextFunction = ALCcontext* (__cdecl*)(
    ALCdevice* device,
    const ALCint* attributes);
using AlcGetContextsDeviceFunction = ALCdevice* (__cdecl*)(
    ALCcontext* context);
using AlcGetIntegervFunction = void (__cdecl*)(
    ALCdevice* device,
    ALCint parameter,
    ALCint size,
    ALCint* values);

constexpr ALCint kAlcHrtfSoft = 0x1992;
constexpr ALCint kAlcHrtfStatusSoft = 0x1993;
constexpr ALCint kAlcTrue = 1;
constexpr std::size_t kMaximumAttributePairs = 64;

HMODULE g_routerModule = nullptr;
std::atomic<unsigned long> g_createContextCalls = 0;
std::atomic<unsigned long> g_forcedHrtfCalls = 0;
std::atomic<unsigned long> g_successfulContextCalls = 0;
std::atomic<long> g_lastHrtfStatus = 0;
std::atomic<unsigned long> g_malformedAttributeLists = 0;

void ResolveForwardedExports(HMODULE module) noexcept
{
    if (module == nullptr)
    {
        return;
    }
#define BFVR_OPENAL_FORWARD(name) \
    g_forward_##name = GetProcAddress(module, #name);
#include "audio/OpenALForwardedExports.inl"
#undef BFVR_OPENAL_FORWARD
}

HMODULE LoadActualOpenAL() noexcept
{
    HMODULE module = GetModuleHandleW(L"BFVROpenALSoft.dll");
    if (module != nullptr)
    {
        return module;
    }

    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(
        g_routerModule,
        path,
        static_cast<DWORD>(std::size(path)));
    if (length == 0 || length >= std::size(path))
    {
        return nullptr;
    }

    wchar_t* separator = wcsrchr(path, L'\\');
    if (separator == nullptr)
    {
        return nullptr;
    }
    *separator = L'\0';
    if (wcscat_s(path, L"\\BFVROpenALSoft.dll") != 0)
    {
        return nullptr;
    }
    module = LoadLibraryExW(
        path,
        nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
            LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    ResolveForwardedExports(module);
    return module;
}

template<typename Function>
Function Resolve(HMODULE module, const char* name) noexcept
{
    return module == nullptr
        ? nullptr
        : reinterpret_cast<Function>(GetProcAddress(module, name));
}

bool MakeHrtfAttributes(
    const ALCint* attributes,
    std::array<ALCint, kMaximumAttributePairs * 2 + 3>& merged) noexcept
{
    std::size_t output = 0;
    bool replaced = false;
    if (attributes != nullptr)
    {
        for (std::size_t pair = 0; pair < kMaximumAttributePairs; ++pair)
        {
            const ALCint key = attributes[pair * 2];
            if (key == 0)
            {
                if (!replaced)
                {
                    merged[output++] = kAlcHrtfSoft;
                    merged[output++] = kAlcTrue;
                }
                merged[output] = 0;
                return true;
            }

            merged[output++] = key;
            merged[output++] = key == kAlcHrtfSoft
                ? kAlcTrue
                : attributes[pair * 2 + 1];
            replaced = replaced || key == kAlcHrtfSoft;
        }
        return false;
    }

    merged[0] = kAlcHrtfSoft;
    merged[1] = kAlcTrue;
    merged[2] = 0;
    return true;
}
} // namespace

extern "C" ALCcontext* __cdecl BFVRAlcCreateContext(
    ALCdevice* device,
    const ALCint* attributes)
{
    ++g_createContextCalls;
    HMODULE module = LoadActualOpenAL();
    const auto createContext = Resolve<AlcCreateContextFunction>(
        module,
        "alcCreateContext");
    if (createContext == nullptr)
    {
        return nullptr;
    }

    std::array<ALCint, kMaximumAttributePairs * 2 + 3> merged = {};
    const bool valid = MakeHrtfAttributes(attributes, merged);
    if (!valid)
    {
        ++g_malformedAttributeLists;
        return createContext(device, attributes);
    }

    ++g_forcedHrtfCalls;
    ALCcontext* const context = createContext(device, merged.data());
    if (context == nullptr)
    {
        return nullptr;
    }
    ++g_successfulContextCalls;

    const auto getContextsDevice = Resolve<AlcGetContextsDeviceFunction>(
        module,
        "alcGetContextsDevice");
    const auto getIntegerv = Resolve<AlcGetIntegervFunction>(
        module,
        "alcGetIntegerv");
    if (getContextsDevice != nullptr && getIntegerv != nullptr)
    {
        ALCint hrtfStatus = 0;
        getIntegerv(
            getContextsDevice(context),
            kAlcHrtfStatusSoft,
            1,
            &hrtfStatus);
        g_lastHrtfStatus.store(hrtfStatus);
    }
    return context;
}

extern "C" int __cdecl BFVRGetOpenALHrtfRouterDiagnostics(
    bfvr::audio::OpenALHrtfRouterDiagnostics* diagnostics)
{
    if (diagnostics == nullptr ||
        diagnostics->version !=
            bfvr::audio::kOpenALHrtfRouterDiagnosticsVersion ||
        diagnostics->size <
            sizeof(bfvr::audio::OpenALHrtfRouterDiagnostics))
    {
        return 0;
    }

    diagnostics->createContextCalls = g_createContextCalls.load();
    diagnostics->forcedHrtfCalls = g_forcedHrtfCalls.load();
    diagnostics->successfulContextCalls =
        g_successfulContextCalls.load();
    diagnostics->lastHrtfStatus = g_lastHrtfStatus.load();
    diagnostics->malformedAttributeLists =
        g_malformedAttributeLists.load();
    return 1;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_routerModule = module;
        DisableThreadLibraryCalls(module);
        // BFVR preloads the renamed implementation by absolute path before
        // loading this router. GetModuleHandle/GetProcAddress are loader-lock
        // safe; loading a dependency here would not be.
        ResolveForwardedExports(
            GetModuleHandleW(L"BFVROpenALSoft.dll"));
    }
    return TRUE;
}
