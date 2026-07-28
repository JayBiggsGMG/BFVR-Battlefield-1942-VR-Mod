#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cwchar>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr DWORD kObserverInitializationParametersMagic = 0x52564642;

struct ObserverInitializationParameters
{
    DWORD magic = kObserverInitializationParametersMagic;
    DWORD size = sizeof(ObserverInitializationParameters);
    ULONG_PTR request = 0;
    DWORD primaryThreadId = 0;
};

struct Options
{
    std::wstring gameRoot;
    std::wstring clientPath;
    bool dryRun = false;
    bool showHelp = false;
    bool presentBridgeProbe = false;
    bool surfaceDescriptorProbe = false;
    bool surfaceCopyProbe = false;
    bool surfaceStreamProbe = false;
    bool surfaceResetProbe = false;
    bool surfaceReadbackProbe = false;
    bool surfaceSceneReadbackProbe = false;
    bool surfaceD3D11UploadProbe = false;
    bool renderViewTransformProbe = false;
    bool renderViewSetterBaselineProbe = false;
    bool renderViewSingleEyeProbe = false;
    bool configuredViewListProbe = false;
    bool configuredViewListWriterProbe = false;
    bool sceneBatchProbe = false;
    bool d3d8CallInventoryProbe = false;
    bool d3d8StateCensusProbe = false;
    bool d3d8StereoPairProbe = false;
    bool d3d8StereoFrameProbe = false;
    bool d3d8StereoFramePresentationProbe = false;
    bool d3d8To9FlatProbe = false;
    bool d3d8To9ObserverProbe = false;
    bool playerInputProbe = false;
    bool weaponViewModelProbe = false;
    bool weaponTransformOwnershipProbe = false;
    bool weaponFireProbe = false;
    bool weaponMotionProbe = false;
    bool runUntilStopped = false;
    DWORD diagnosticTimeoutMs = 0;
    std::vector<std::wstring> gameArguments;
};

std::wstring GetModuleDirectory()
{
    wchar_t path[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path))) == 0)
    {
        return {};
    }

    std::wstring directory(path);
    const std::size_t separator = directory.find_last_of(L"\\/");
    return separator == std::wstring::npos ? std::wstring{} : directory.substr(0, separator);
}

std::wstring ParentDirectory(const std::wstring& path)
{
    const std::size_t separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? std::wstring{} : path.substr(0, separator);
}

std::wstring Combine(const std::wstring& directory, const std::wstring& fileName)
{
    if (directory.empty())
    {
        return fileName;
    }
    return directory + L"\\" + fileName;
}

std::wstring LoaderLogPath()
{
    return Combine(GetModuleDirectory(), L"loader.log");
}

std::wstring D3D8ProbeCompletionEventName(DWORD processId)
{
    wchar_t eventName[96] = {};
    if (swprintf_s(eventName, std::size(eventName), L"Local\\BFVRD3D8ProbeComplete-%lu", processId) < 0)
    {
        return {};
    }
    return eventName;
}

void ResetLoaderLog()
{
    FILE* file = nullptr;
    if (_wfopen_s(&file, LoaderLogPath().c_str(), L"wt, ccs=UTF-8") == 0 && file != nullptr)
    {
        fclose(file);
    }
}

void AppendLoaderLog(const std::wstring& message)
{
    FILE* file = nullptr;
    if (_wfopen_s(&file, LoaderLogPath().c_str(), L"at, ccs=UTF-8") != 0 || file == nullptr)
    {
        return;
    }
    fputws(message.c_str(), file);
    fputws(L"\n", file);
    fclose(file);
}

void AppendLoaderError(const wchar_t* operation, DWORD error)
{
    wchar_t message[512] = {};
    swprintf_s(message, std::size(message), L"%ls failed: %lu", operation, error);
    AppendLoaderLog(message);
}

std::wstring QuoteArgument(const std::wstring& argument)
{
    if (argument.find_first_of(L" \t\"") == std::wstring::npos)
    {
        return argument;
    }

    std::wstring quoted = L"\"";
    std::size_t slashCount = 0;
    for (wchar_t character : argument)
    {
        if (character == L'\\')
        {
            ++slashCount;
            continue;
        }

        if (character == L'\"')
        {
            quoted.append(slashCount * 2 + 1, L'\\');
            quoted.push_back(L'\"');
            slashCount = 0;
            continue;
        }

        quoted.append(slashCount, L'\\');
        slashCount = 0;
        quoted.push_back(character);
    }

    quoted.append(slashCount * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

std::wstring RequireValue(int argc, wchar_t* argv[], int& index, const wchar_t* option)
{
    if (index + 1 >= argc)
    {
        fwprintf(stderr, L"[FAIL] Missing value for %ls.\n", option);
        return {};
    }
    return argv[++index];
}

bool ParseOptions(int argc, wchar_t* argv[], Options& options)
{
    const std::wstring bfvrDirectory = GetModuleDirectory();
    options.gameRoot = ParentDirectory(bfvrDirectory);
    options.clientPath = Combine(bfvrDirectory, L"BFVRClient.dll");

    for (int index = 1; index < argc; ++index)
    {
        const std::wstring argument = argv[index];
        if (argument == L"--")
        {
            for (++index; index < argc; ++index)
            {
                options.gameArguments.emplace_back(argv[index]);
            }
            break;
        }

        if (argument == L"--game-root")
        {
            options.gameRoot = RequireValue(argc, argv, index, L"--game-root");
        }
        else if (argument == L"--client")
        {
            options.clientPath = RequireValue(argc, argv, index, L"--client");
        }
        else if (argument == L"--dry-run")
        {
            options.dryRun = true;
        }
        else if (argument == L"--present-bridge-probe")
        {
            options.presentBridgeProbe = true;
        }
        else if (argument == L"--surface-descriptor-probe")
        {
            options.presentBridgeProbe = true;
            options.surfaceDescriptorProbe = true;
        }
        else if (argument == L"--surface-copy-probe")
        {
            options.presentBridgeProbe = true;
            options.surfaceDescriptorProbe = true;
            options.surfaceCopyProbe = true;
        }
        else if (argument == L"--surface-stream-probe")
        {
            options.presentBridgeProbe = true;
            options.surfaceDescriptorProbe = true;
            options.surfaceCopyProbe = true;
            options.surfaceStreamProbe = true;
        }
        else if (argument == L"--surface-reset-probe")
        {
            options.presentBridgeProbe = true;
            options.surfaceDescriptorProbe = true;
            options.surfaceCopyProbe = true;
            options.surfaceStreamProbe = true;
            options.surfaceResetProbe = true;
        }
        else if (argument == L"--surface-readback-probe")
        {
            options.presentBridgeProbe = true;
            options.surfaceDescriptorProbe = true;
            options.surfaceReadbackProbe = true;
        }
        else if (argument == L"--surface-scene-readback-probe")
        {
            options.presentBridgeProbe = true;
            options.surfaceDescriptorProbe = true;
            options.surfaceSceneReadbackProbe = true;
        }
        else if (argument == L"--surface-d3d11-upload-probe")
        {
            options.presentBridgeProbe = true;
            options.surfaceDescriptorProbe = true;
            options.surfaceD3D11UploadProbe = true;
        }
        else if (argument == L"--render-view-transform-probe")
        {
            options.renderViewTransformProbe = true;
        }
        else if (argument == L"--render-view-setter-baseline-probe")
        {
            options.renderViewSetterBaselineProbe = true;
        }
        else if (argument == L"--render-view-single-eye-probe")
        {
            options.renderViewSingleEyeProbe = true;
        }
        else if (argument == L"--configured-view-list-probe")
        {
            options.configuredViewListProbe = true;
        }
        else if (argument == L"--configured-view-list-writer-probe")
        {
            options.configuredViewListWriterProbe = true;
        }
        else if (argument == L"--scene-batch-probe")
        {
            options.sceneBatchProbe = true;
        }
        else if (argument == L"--d3d8-call-inventory-probe")
        {
            options.d3d8CallInventoryProbe = true;
        }
        else if (argument == L"--d3d8-state-census-probe")
        {
            options.d3d8StateCensusProbe = true;
        }
        else if (argument == L"--d3d8-stereo-pair-probe")
        {
            options.d3d8StereoPairProbe = true;
        }
        else if (argument == L"--d3d8-stereo-frame-probe")
        {
            options.d3d8StereoFrameProbe = true;
        }
        else if (argument == L"--d3d8-openxr-presentation-probe")
        {
            options.d3d8StereoFramePresentationProbe = true;
        }
        else if (argument == L"--d3d8to9-flat-probe")
        {
            options.d3d8To9FlatProbe = true;
        }
        else if (argument == L"--d3d8to9-observer-probe")
        {
            options.d3d8To9ObserverProbe = true;
        }
        else if (argument == L"--player-input-probe")
        {
            options.playerInputProbe = true;
        }
        else if (argument == L"--weapon-viewmodel-probe")
        {
            options.weaponViewModelProbe = true;
        }
        else if (argument == L"--weapon-transform-ownership-probe")
        {
            options.weaponTransformOwnershipProbe = true;
        }
        else if (argument == L"--weapon-fire-probe")
        {
            options.weaponFireProbe = true;
        }
        else if (argument == L"--weapon-motion-probe")
        {
            options.weaponMotionProbe = true;
        }
        else if (argument == L"--run-until-stopped")
        {
            options.runUntilStopped = true;
        }
        else if (argument == L"--diagnostic-timeout-ms")
        {
            const std::wstring timeoutText = RequireValue(argc, argv, index, L"--diagnostic-timeout-ms");
            wchar_t* parseEnd = nullptr;
            const unsigned long timeout = std::wcstoul(timeoutText.c_str(), &parseEnd, 10);
            if (timeoutText.empty() || parseEnd == timeoutText.c_str() || *parseEnd != L'\0' || timeout < 1000 || timeout > 300000)
            {
                fwprintf(stderr, L"[FAIL] --diagnostic-timeout-ms must be a whole number from 1000 to 300000.\n");
                return false;
            }
            options.diagnosticTimeoutMs = static_cast<DWORD>(timeout);
        }
        else if (argument == L"--help" || argument == L"-h" || argument == L"/?")
        {
            options.showHelp = true;
        }
        else
        {
            fwprintf(stderr, L"[FAIL] Unknown argument: %ls\n", argument.c_str());
            return false;
        }

        if (options.gameRoot.empty() || options.clientPath.empty())
        {
            return false;
        }
    }

    if (options.renderViewSingleEyeProbe && options.diagnosticTimeoutMs == 0)
    {
        fwprintf(stderr, L"[FAIL] --render-view-single-eye-probe requires --diagnostic-timeout-ms so its directly launched game process is bounded.\n");
        return false;
    }
    if (options.sceneBatchProbe && options.diagnosticTimeoutMs == 0)
    {
        fwprintf(stderr, L"[FAIL] --scene-batch-probe requires --diagnostic-timeout-ms so its directly launched game process is bounded.\n");
        return false;
    }
    if (options.d3d8CallInventoryProbe && options.diagnosticTimeoutMs == 0)
    {
        fwprintf(stderr, L"[FAIL] --d3d8-call-inventory-probe requires --diagnostic-timeout-ms so its directly launched game process is bounded.\n");
        return false;
    }
    if (options.d3d8StateCensusProbe && options.diagnosticTimeoutMs == 0)
    {
        fwprintf(stderr, L"[FAIL] --d3d8-state-census-probe requires --diagnostic-timeout-ms so its directly launched game process is bounded.\n");
        return false;
    }
    if (options.d3d8StereoPairProbe && options.diagnosticTimeoutMs == 0)
    {
        fwprintf(stderr, L"[FAIL] --d3d8-stereo-pair-probe requires --diagnostic-timeout-ms so its directly launched game process is bounded.\n");
        return false;
    }
    if (options.d3d8StereoFrameProbe && options.diagnosticTimeoutMs == 0)
    {
        fwprintf(stderr, L"[FAIL] --d3d8-stereo-frame-probe requires --diagnostic-timeout-ms so its directly launched game process is bounded.\n");
        return false;
    }
    if (options.d3d8StereoFramePresentationProbe &&
        !options.runUntilStopped &&
        options.diagnosticTimeoutMs == 0)
    {
        fwprintf(stderr, L"[FAIL] --d3d8-openxr-presentation-probe requires --diagnostic-timeout-ms so its directly launched game and owned companion are bounded.\n");
        return false;
    }
    if (options.d3d8To9FlatProbe && options.diagnosticTimeoutMs == 0)
    {
        fwprintf(stderr, L"[FAIL] --d3d8to9-flat-probe requires --diagnostic-timeout-ms so its directly launched game process is bounded.\n");
        return false;
    }
    if (options.d3d8To9ObserverProbe &&
        !options.runUntilStopped &&
        options.diagnosticTimeoutMs == 0)
    {
        fwprintf(stderr, L"[FAIL] --d3d8to9-observer-probe requires --diagnostic-timeout-ms so its directly launched game process is bounded.\n");
        return false;
    }
    if (options.playerInputProbe && options.diagnosticTimeoutMs < 125000)
    {
        fwprintf(stderr, L"[FAIL] --player-input-probe requires --diagnostic-timeout-ms of at least 125000 so its 120-second forwarding-only observation can report and remove both hooks.\n");
        return false;
    }
    if ((options.weaponViewModelProbe || options.weaponTransformOwnershipProbe) &&
        options.diagnosticTimeoutMs < 160000)
    {
        fwprintf(stderr, L"[FAIL] Weapon D3D8 probes require --diagnostic-timeout-ms of at least 160000 so they can wait for the direct-D3D8 lifecycle and a manually spawned local-infantry capture.\n");
        return false;
    }
    if (options.weaponFireProbe && options.diagnosticTimeoutMs < 185000)
    {
        fwprintf(stderr, L"[FAIL] --weapon-fire-probe requires --diagnostic-timeout-ms of at least 185000 so its 180-second forwarding-only observation can remove its hook.\n");
        return false;
    }
    if ((options.weaponViewModelProbe ? 1 : 0) +
            (options.weaponTransformOwnershipProbe ? 1 : 0) +
            (options.weaponFireProbe ? 1 : 0) > 1)
    {
        fwprintf(stderr, L"[FAIL] Select only one weapon probe mode.\n");
        return false;
    }
    if (options.weaponMotionProbe &&
        (!options.d3d8To9ObserverProbe ||
         !options.d3d8StereoFramePresentationProbe ||
         !options.runUntilStopped))
    {
        fwprintf(stderr, L"[FAIL] --weapon-motion-probe requires the continuous --d3d8to9-observer-probe, --d3d8-openxr-presentation-probe, and --run-until-stopped path.\n");
        return false;
    }
    if (options.playerInputProbe &&
        (options.presentBridgeProbe || options.surfaceDescriptorProbe ||
         options.surfaceCopyProbe || options.surfaceStreamProbe ||
         options.surfaceResetProbe || options.surfaceReadbackProbe ||
         options.surfaceSceneReadbackProbe || options.surfaceD3D11UploadProbe ||
         options.renderViewTransformProbe || options.renderViewSetterBaselineProbe ||
         options.renderViewSingleEyeProbe || options.configuredViewListProbe ||
         options.configuredViewListWriterProbe || options.sceneBatchProbe ||
         options.d3d8CallInventoryProbe || options.d3d8StateCensusProbe ||
         options.d3d8StereoPairProbe || options.d3d8StereoFrameProbe ||
         options.d3d8StereoFramePresentationProbe || options.d3d8To9FlatProbe ||
         options.d3d8To9ObserverProbe || options.weaponFireProbe ||
         options.runUntilStopped))
    {
        fwprintf(stderr, L"[FAIL] --player-input-probe is intentionally isolated from every graphics/OpenXR probe.\n");
        return false;
    }
    if (options.d3d8To9FlatProbe && options.d3d8To9ObserverProbe)
    {
        fwprintf(stderr, L"[FAIL] Select only one d3d8to9 probe mode.\n");
        return false;
    }
    if ((options.weaponViewModelProbe || options.weaponTransformOwnershipProbe ||
         options.weaponFireProbe) &&
        (options.presentBridgeProbe || options.surfaceDescriptorProbe ||
         options.surfaceCopyProbe || options.surfaceStreamProbe ||
         options.surfaceResetProbe || options.surfaceReadbackProbe ||
         options.surfaceSceneReadbackProbe || options.surfaceD3D11UploadProbe ||
         options.renderViewTransformProbe || options.renderViewSetterBaselineProbe ||
         options.renderViewSingleEyeProbe || options.configuredViewListProbe ||
         options.configuredViewListWriterProbe || options.sceneBatchProbe ||
         options.d3d8CallInventoryProbe || options.d3d8StateCensusProbe ||
         options.d3d8StereoPairProbe || options.d3d8StereoFrameProbe ||
         options.d3d8StereoFramePresentationProbe || options.d3d8To9FlatProbe ||
         options.d3d8To9ObserverProbe || options.playerInputProbe ||
         options.runUntilStopped))
    {
        fwprintf(stderr, L"[FAIL] Weapon probes are intentionally isolated from every other graphics, input, and OpenXR probe.\n");
        return false;
    }
    if (options.runUntilStopped &&
        (!options.d3d8StereoFramePresentationProbe ||
         !options.d3d8To9ObserverProbe))
    {
        fwprintf(
            stderr,
            L"[FAIL] --run-until-stopped currently requires the combined --d3d8to9-observer-probe and --d3d8-openxr-presentation-probe path.\n");
        return false;
    }
    if (options.runUntilStopped &&
        options.diagnosticTimeoutMs != 0)
    {
        fwprintf(
            stderr,
            L"[FAIL] Select either --run-until-stopped or --diagnostic-timeout-ms.\n");
        return false;
    }

    return true;
}

void PrintUsage()
{
    wprintf(L"BFVRLoader (D3D8 observer prototype)\n");
    wprintf(L"A diagnostic timeout closes only the game process started by this loader after the requested observation window. --run-until-stopped keeps the combined translated OpenXR test active for BF1942's process lifetime.\n");
    wprintf(L"Usage: BFVRLoader [--dry-run] [--present-bridge-probe] [--surface-descriptor-probe] [--surface-copy-probe] [--surface-stream-probe] [--surface-reset-probe] [--surface-readback-probe] [--surface-scene-readback-probe] [--surface-d3d11-upload-probe] [--render-view-transform-probe] [--render-view-setter-baseline-probe] [--render-view-single-eye-probe] [--configured-view-list-probe] [--configured-view-list-writer-probe] [--scene-batch-probe] [--d3d8-call-inventory-probe] [--d3d8-state-census-probe] [--d3d8-stereo-pair-probe] [--d3d8-stereo-frame-probe] [--d3d8-openxr-presentation-probe] [--d3d8to9-flat-probe] [--d3d8to9-observer-probe] [--player-input-probe] [--weapon-viewmodel-probe] [--weapon-transform-ownership-probe] [--weapon-fire-probe] [--weapon-motion-probe] [--diagnostic-timeout-ms <1000-300000> | --run-until-stopped] [--game-root <path>] [--client <path>] [-- <game arguments>]\n");
    wprintf(L"The loader starts BF1942.exe suspended, loads BFVRClient.dll from the BFVR folder, then resumes it.\n");
    wprintf(L"--present-bridge-probe is an explicit active test: it installs a no-op in-process D3D8 Present detour only after the verified D3D8 lifecycle trace completes.\n");
    wprintf(L"--surface-descriptor-probe is a separate explicit one-shot test: at the passively confirmed ordinary-world Projection submission it calls GetRenderTarget/GetDesc/Release on the same D3D8 thread.\n");
    wprintf(L"--surface-copy-probe extends that one-shot boundary with one BFVR-owned matching render target, one full-surface CopyRects call, and balanced release of both temporary references before returning.\n");
    wprintf(L"--surface-stream-probe runs at most 60 same-thread copies into a BFVR-owned target, releases it before Reset, and recreates it only after a later verified Projection boundary.\n");
    wprintf(L"--surface-reset-probe holds one BFVR-owned target until an observed Reset, proves the pre-Reset release, then requires one post-Reset recreation/copy before completion.\n");
    wprintf(L"--surface-readback-probe is a one-shot content probe: it copies the ordinary-world target into a transient system-memory image, locks five pixels, then releases every temporary surface before returning.\n");
    wprintf(L"--surface-scene-readback-probe arms at the exact Projection setup but performs that same transient system-memory readback only after the following EndScene returns on the same device thread.\n");
    wprintf(L"--surface-d3d11-upload-probe performs that post-EndScene readback, uploads it once into a transient BFVR-owned D3D11 texture, verifies five pixels through a BFVR-owned staging texture, then releases all D3D8/D3D11 resources without creating an OpenXR session.\n");
    wprintf(L"--render-view-transform-probe is a one-shot read-only CPU debug-register trace of the ordinary per-view transformation handoff. It copies the input and derived matrices plus renderer caches; it does not invoke or write game or D3D state.\n");
    wprintf(L"--render-view-setter-baseline-probe installs one temporary pass-through hook on the verified RenderView setter, forwards the game matrix unchanged once, validates the stored transform, then removes the hook.\n");
    wprintf(L"--render-view-single-eye-probe is an opt-in one-shot transform experiment: only the confirmed active ordinary-world RenderView receives one copied local-right offset, then the next matching game setter must prove normal restoration before the hook is removed. It is not stereo or head tracking.\n");
    wprintf(L"--configured-view-list-probe is a one-shot read-only CPU debug-register trace of the normal per-view dispatch. It copies the configured-view list metadata, resolved owner, RenderView, viewport, and clone target without invoking or writing game code/data.\n");
    wprintf(L"--configured-view-list-writer-probe is a one-shot read-only CPU data-breakpoint trace of the configured-view vector end pointer. It records the native writer instruction and resulting vector state, then restores the prior debug-register context without invoking or writing game code/data.\n");
    wprintf(L"--scene-batch-probe is a one-shot read-only CPU trace below the per-view renderer. It copies the three SkinningShader batch owners, their submitted-list metadata, and the first subsequent item router without invoking game or D3D code.\n");
    wprintf(L"--d3d8-call-inventory-probe is a bounded no-HMD Present-to-Present trace. It forwards Clear, render-target, World/View/Projection, draw, scene, and Present calls unchanged, records one spawned-frame, and creates no D3D resource or OpenXR object.\n");
    wprintf(L"--d3d8-state-census-probe is a separate bounded no-HMD Present-to-Present trace. It forwards D3D8 setter calls unchanged and records only original HRESULTs, state values, opaque pointers, and counts; it performs no state/resource read or write beyond the original game calls.\n");
    wprintf(L"--d3d8-stereo-pair-probe is an active one-draw proof. It forwards the game draw, renders one eligible full-size indexed draw into transient BFVR-owned left/right color+depth targets with diagnostic View/Projection offsets, restores exact target/depth/viewport/transform state, reads back the owned colors, and releases every reference.\n");
    wprintf(L"--d3d8-stereo-frame-probe accumulates one bounded Present-to-Present full-size stream from all four D3D8 draw families into frame-lived BFVR-owned left/right world targets plus a transparent Ref2 menu target. Perspective non-XYZRHW work receives stereo transforms except for the exact profiled skybox cube-face signature; pretransformed world particles and skybox faces keep exact game transforms, while exact Ref2 menu quads are omitted from the world eyes and replayed once into the menu layer. It excludes RTT-sized/depthless targets, verifies state after every draw, releases before Reset, and never presents any owned target to BF1942.\n");
    wprintf(L"--d3d8-openxr-presentation-probe runs a continuous partition through the x64 BFVRPresenter beside BFVRClient.dll. It is bounded by default; add --run-until-stopped on the combined translated path to keep it active for BF1942's process lifetime. The GPU-resident path renders world eyes at 100%% of the OpenXR-recommended dimensions by default (override BFVR_OPENXR_WORLD_RENDER_SCALE from 0.50 through 1.25), then copies them into runtime-sized swapchains with runtime asymmetric FOV, eye poses, and head orientation/translation composed onto the game camera; the logical-screen transparent Ref2 surface is aspect-fit into the configurable composition layer.\n");
    wprintf(L"--d3d8to9-flat-probe injects the DXGI-free BFVRD3D8To9FlatClient.dll beside the selected client, loads BFVRD3D8To9.dll by absolute path, and redirects only BF1942's Direct3DCreate8 import through the pinned translator. It creates no OpenXR session and writes no proxy DLL into the game directory.\n");
    wprintf(L"--d3d8to9-observer-probe keeps the full BFVR observer in front of the pinned translator, validating translated CreateDevice, Present, Reset, and teardown routing without creating an OpenXR session or writing a proxy DLL into the game directory.\n");
    wprintf(L"--player-input-probe is an isolated 120-second read-only input trace. It requires --diagnostic-timeout-ms >= 125000, temporarily forwards the verified PlayerInputMap setter and normalizer unchanged, logs only non-normalizer setter callers, bounded input-thread non-axis button-mask press/release edges, and normalized-frame slots, axes, receiver addresses, threads, and caller RVAs before removing its hooks. It does not synthesize input or write BF1942 state.\n");
    wprintf(L"--weapon-viewmodel-probe is an isolated forwarding-only direct-D3D8 capture. It requires --diagnostic-timeout-ms >= 160000, waits for a sustained local-infantry alive gate, then gives an 8-second preparation window before recording 240 frames of original setter arguments and draw signatures. It creates no resource or changes no input, game, camera, D3D state, or rendering output. Manually stay on foot, select a contrasting ordinary infantry weapon, and begin any desired fire/reload transition before capture.\n");
    wprintf(L"--weapon-transform-ownership-probe is an isolated, bounded active D3D8 test. After the same local-infantry gate and an 8-second preparation window, it offsets only the evidence-classified generic fixed-function weapon candidates +0.25 right in view space for 180 frames and restores the original World transform before each draw returns. It creates no resource and changes no input, game, camera, weapon, projectile, or network state. Use it only to observe whether the first-person weapon shifts.\n");
    wprintf(L"--weapon-fire-probe is an isolated 180-second forwarding-only native fire capture. It records the profiled fire-core's caller-supplied 4x4 matrix, barrel index, caller, and a raw actor/local-player comparison while forwarding every call unchanged. Load a local infantry map, then fire several shots with two ordinary weapons during the window. It does not alter input, camera, weapon, projectile, ray, hit registration, or network state.\n");
    wprintf(L"--weapon-motion-probe enables the right-hand 6DOF weapon development slice only on the continuous translated OpenXR path: combine it with --d3d8to9-observer-probe --d3d8-openxr-presentation-probe --run-until-stopped. The tracked grip drives only the proven shared fixed-function presentation candidate. At the independently proven ordinary local-infantry WeaponFire_Core boundary, the same fresh valid/tracked grip-derived visual orientation reorients the native fire matrix while preserving its position and BF1942's weapon/barrel offsets, spread, cadence, projectile creation, and networking path. Every failed tracking, ownership, caller, pose, or matrix gate forwards the original draw or shot.\n");
    wprintf(L"Combine --d3d8to9-observer-probe with --d3d8-stereo-frame-probe for the live no-HMD D3D9Ex shared eye/UI target gate.\n");
    wprintf(L"Combine --d3d8to9-observer-probe with --d3d8-openxr-presentation-probe for the same GPU-resident target path through the x64 OpenXR presenter.\n");
}

bool Exists(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool InjectLibrary(HANDLE process, const std::wstring& clientPath, DWORD& remoteModuleBase)
{
    remoteModuleBase = 0;
    const SIZE_T byteCount = (clientPath.size() + 1) * sizeof(wchar_t);
    void* remotePath = VirtualAllocEx(process, nullptr, byteCount, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (remotePath == nullptr)
    {
        const DWORD error = GetLastError();
        fwprintf(stderr, L"[FAIL] VirtualAllocEx failed: %lu\n", error);
        return false;
    }

    SIZE_T written = 0;
    const bool wrotePath = WriteProcessMemory(process, remotePath, clientPath.c_str(), byteCount, &written) != FALSE && written == byteCount;
    if (!wrotePath)
    {
        const DWORD error = GetLastError();
        fwprintf(stderr, L"[FAIL] WriteProcessMemory failed: %lu\n", error);
        AppendLoaderError(L"WriteProcessMemory", error);
        VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
        return false;
    }

    const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    const auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(kernel32, "LoadLibraryW"));
    if (loadLibrary == nullptr)
    {
        fwprintf(stderr, L"[FAIL] Unable to find LoadLibraryW.\n");
        AppendLoaderLog(L"GetProcAddress(LoadLibraryW) failed.");
        VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
        return false;
    }

    HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary, remotePath, 0, nullptr);
    if (thread == nullptr)
    {
        const DWORD error = GetLastError();
        fwprintf(stderr, L"[FAIL] CreateRemoteThread failed: %lu\n", error);
        AppendLoaderError(L"CreateRemoteThread", error);
        VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
        return false;
    }

    const DWORD wait = WaitForSingleObject(thread, 15000);
    DWORD moduleHandle = 0;
    const bool loaded = wait == WAIT_OBJECT_0 && GetExitCodeThread(thread, &moduleHandle) != FALSE && moduleHandle != 0;
    if (!loaded)
    {
        const DWORD error = GetLastError();
        fwprintf(stderr, L"[FAIL] Remote LoadLibraryW did not complete successfully (wait=%lu, error=%lu).\n", wait, error);
        AppendLoaderError(L"Remote LoadLibraryW", error);
    }

    if (loaded)
    {
        remoteModuleBase = moduleHandle;
    }

    CloseHandle(thread);
    VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
    return loaded;
}

bool InitializeObserver(HANDLE process, DWORD primaryThreadId, const std::wstring& clientPath, DWORD remoteModuleBase, bool presentBridgeProbe, bool surfaceDescriptorProbe, bool surfaceCopyProbe, bool surfaceStreamProbe, bool surfaceResetProbe, bool surfaceReadbackProbe, bool surfaceSceneReadbackProbe, bool surfaceD3D11UploadProbe, bool renderViewTransformProbe, bool renderViewSetterBaselineProbe, bool renderViewSingleEyeProbe, bool configuredViewListProbe, bool configuredViewListWriterProbe, bool sceneBatchProbe, bool d3d8CallInventoryProbe, bool d3d8StateCensusProbe, bool d3d8StereoPairProbe, bool d3d8StereoFrameProbe, bool d3d8StereoFramePresentationProbe, bool d3d8To9FlatProbe, bool d3d8To9ObserverProbe, bool playerInputProbe, bool weaponViewModelProbe, bool weaponTransformOwnershipProbe, bool weaponFireProbe)
{
    HMODULE localModule = LoadLibraryW(clientPath.c_str());
    if (localModule == nullptr)
    {
        const DWORD error = GetLastError();
        fwprintf(stderr, L"[FAIL] Unable to load BFVRClient.dll locally to locate its initializer (error=%lu).\n", error);
        AppendLoaderError(L"LoadLibraryW(local BFVRClient)", error);
        return false;
    }

    FARPROC localInitializer = GetProcAddress(localModule, "BFVRInitializeObserver");
    if (localInitializer == nullptr)
    {
        localInitializer = GetProcAddress(localModule, "BFVRInitializeObserver@4");
    }
    if (localInitializer == nullptr)
    {
        localInitializer = GetProcAddress(localModule, "_BFVRInitializeObserver@4");
    }
    if (localInitializer == nullptr)
    {
        const DWORD error = GetLastError();
        fwprintf(stderr, L"[FAIL] BFVRClient.dll does not export BFVRInitializeObserver (error=%lu).\n", error);
        AppendLoaderError(L"GetProcAddress(BFVRInitializeObserver)", error);
        FreeLibrary(localModule);
        return false;
    }

    const DWORD_PTR initializerRva = reinterpret_cast<DWORD_PTR>(localInitializer) - reinterpret_cast<DWORD_PTR>(localModule);
    FreeLibrary(localModule);

    const auto remoteInitializer = reinterpret_cast<LPTHREAD_START_ROUTINE>(static_cast<DWORD_PTR>(remoteModuleBase) + initializerRva);
    ULONG_PTR initializationRequest = 0;
    initializationRequest = presentBridgeProbe ? 1 : initializationRequest;
    initializationRequest = surfaceDescriptorProbe ? 2 : initializationRequest;
    initializationRequest = surfaceCopyProbe ? 3 : initializationRequest;
    initializationRequest = surfaceStreamProbe ? 4 : initializationRequest;
    initializationRequest = surfaceResetProbe ? 5 : initializationRequest;
    initializationRequest = surfaceReadbackProbe ? 6 : initializationRequest;
    initializationRequest = surfaceSceneReadbackProbe ? 7 : initializationRequest;
    initializationRequest = surfaceD3D11UploadProbe ? 8 : initializationRequest;
    initializationRequest = renderViewTransformProbe ? 9 : initializationRequest;
    initializationRequest = renderViewSetterBaselineProbe ? 10 : initializationRequest;
    initializationRequest = configuredViewListProbe ? 11 : initializationRequest;
    initializationRequest = configuredViewListWriterProbe ? 12 : initializationRequest;
    initializationRequest = renderViewSingleEyeProbe ? 13 : initializationRequest;
    initializationRequest = sceneBatchProbe ? 14 : initializationRequest;
    initializationRequest = d3d8CallInventoryProbe ? 15 : initializationRequest;
    initializationRequest = d3d8StateCensusProbe ? 16 : initializationRequest;
    initializationRequest = d3d8StereoPairProbe ? 17 : initializationRequest;
    initializationRequest = d3d8StereoFrameProbe ? 18 : initializationRequest;
    initializationRequest =
        d3d8StereoFramePresentationProbe ? 19 : initializationRequest;
    initializationRequest = d3d8To9FlatProbe ? 20 : initializationRequest;
    initializationRequest = d3d8To9ObserverProbe ? 21 : initializationRequest;
    initializationRequest =
        d3d8To9ObserverProbe && d3d8StereoFrameProbe
        ? 22
        : initializationRequest;
    initializationRequest =
        d3d8To9ObserverProbe && d3d8StereoFramePresentationProbe
        ? 23
        : initializationRequest;
    initializationRequest = playerInputProbe ? 24 : initializationRequest;
    initializationRequest = weaponViewModelProbe ? 25 : initializationRequest;
    initializationRequest = weaponTransformOwnershipProbe ? 26 : initializationRequest;
    initializationRequest = weaponFireProbe ? 27 : initializationRequest;
    const ObserverInitializationParameters initializationParameters = {
        kObserverInitializationParametersMagic,
        sizeof(ObserverInitializationParameters),
        initializationRequest,
        primaryThreadId
    };
    void* remoteParameter = VirtualAllocEx(
        process,
        nullptr,
        sizeof(initializationParameters),
        MEM_RESERVE | MEM_COMMIT,
        PAGE_READWRITE);
    if (remoteParameter == nullptr)
    {
        const DWORD error = GetLastError();
        fwprintf(stderr, L"[FAIL] VirtualAllocEx(observer parameters) failed: %lu\n", error);
        AppendLoaderError(L"VirtualAllocEx(observer parameters)", error);
        return false;
    }
    SIZE_T parameterBytesWritten = 0;
    if (WriteProcessMemory(
            process,
            remoteParameter,
            &initializationParameters,
            sizeof(initializationParameters),
            &parameterBytesWritten) == FALSE ||
        parameterBytesWritten != sizeof(initializationParameters))
    {
        const DWORD error = GetLastError();
        fwprintf(stderr, L"[FAIL] WriteProcessMemory(observer parameters) failed: %lu\n", error);
        AppendLoaderError(L"WriteProcessMemory(observer parameters)", error);
        VirtualFreeEx(process, remoteParameter, 0, MEM_RELEASE);
        return false;
    }
    HANDLE thread = CreateRemoteThread(process, nullptr, 0, remoteInitializer, remoteParameter, 0, nullptr);
    if (thread == nullptr)
    {
        const DWORD error = GetLastError();
        fwprintf(stderr, L"[FAIL] CreateRemoteThread(BFVRInitializeObserver) failed: %lu\n", error);
        AppendLoaderError(L"CreateRemoteThread(BFVRInitializeObserver)", error);
        VirtualFreeEx(process, remoteParameter, 0, MEM_RELEASE);
        return false;
    }

    const DWORD wait = WaitForSingleObject(thread, 15000);
    DWORD initializerResult = 0;
    const bool initialized = wait == WAIT_OBJECT_0 &&
        GetExitCodeThread(thread, &initializerResult) != FALSE &&
        initializerResult != 0;
    if (!initialized)
    {
        const DWORD error = GetLastError();
        fwprintf(stderr, L"[FAIL] BFVRInitializeObserver did not complete successfully (wait=%lu, result=%lu, error=%lu).\n", wait, initializerResult, error);
        AppendLoaderError(L"BFVRInitializeObserver", error);
    }

    CloseHandle(thread);
    VirtualFreeEx(process, remoteParameter, 0, MEM_RELEASE);
    return initialized;
}

bool IsKnownProcessId(
    const std::vector<DWORD>& processIds,
    DWORD processId)
{
    for (const DWORD knownProcessId : processIds)
    {
        if (knownProcessId == processId)
        {
            return true;
        }
    }
    return false;
}

DWORD FindAnyThreadForProcess(DWORD processId)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    THREADENTRY32 entry = {};
    entry.dwSize = sizeof(entry);
    DWORD threadId = 0;
    if (Thread32First(snapshot, &entry))
    {
        do
        {
            if (entry.th32OwnerProcessID == processId)
            {
                threadId = entry.th32ThreadID;
                break;
            }
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return threadId;
}

struct ReplacementProcess
{
    HANDLE handle = nullptr;
    DWORD processId = 0;
    DWORD threadId = 0;
};

ReplacementProcess FindBf1942Replacement(
    const std::wstring& executablePath,
    const std::vector<DWORD>& knownProcessIds)
{
    ReplacementProcess replacement = {};
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return replacement;
    }

    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (_wcsicmp(entry.szExeFile, L"BF1942.exe") != 0 ||
                IsKnownProcessId(knownProcessIds, entry.th32ProcessID))
            {
                continue;
            }

            HANDLE process = OpenProcess(
                PROCESS_CREATE_THREAD |
                    PROCESS_QUERY_INFORMATION |
                    PROCESS_VM_OPERATION |
                    PROCESS_VM_READ |
                    PROCESS_VM_WRITE |
                    SYNCHRONIZE,
                FALSE,
                entry.th32ProcessID);
            if (process == nullptr)
            {
                continue;
            }

            wchar_t imagePath[MAX_PATH] = {};
            DWORD imagePathLength = static_cast<DWORD>(std::size(imagePath));
            const bool isExpectedImage =
                QueryFullProcessImageNameW(
                    process,
                    0,
                    imagePath,
                    &imagePathLength) != FALSE &&
                _wcsicmp(imagePath, executablePath.c_str()) == 0;
            if (!isExpectedImage)
            {
                CloseHandle(process);
                continue;
            }

            const DWORD threadId = FindAnyThreadForProcess(entry.th32ProcessID);
            if (threadId == 0)
            {
                CloseHandle(process);
                continue;
            }

            replacement.handle = process;
            replacement.processId = entry.th32ProcessID;
            replacement.threadId = threadId;
            break;
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return replacement;
}
} // namespace

int wmain(int argc, wchar_t* argv[])
{
    Options options;
    if (!ParseOptions(argc, argv, options))
    {
        return 2;
    }
    if (options.showHelp)
    {
        PrintUsage();
        return 0;
    }
    const bool d3d8To9SharedFrameProbe =
        options.d3d8To9ObserverProbe && options.d3d8StereoFrameProbe;
    const bool d3d8To9OpenXRPresentationProbe =
        options.d3d8To9ObserverProbe &&
        options.d3d8StereoFramePresentationProbe;

    const std::wstring executablePath = Combine(options.gameRoot, L"BF1942.exe");
    const std::wstring activeClientPath = options.d3d8To9FlatProbe
        ? Combine(
            ParentDirectory(options.clientPath),
            L"BFVRD3D8To9FlatClient.dll")
        : options.clientPath;
    if (!Exists(executablePath) || !Exists(activeClientPath))
    {
        fwprintf(stderr, L"[FAIL] Expected game executable or selected BFVR client DLL was not found.\n");
        fwprintf(stderr, L"[INFO] game=%ls\n[INFO] client=%ls\n", executablePath.c_str(), activeClientPath.c_str());
        return 2;
    }
    const std::wstring d3d8To9Path =
        Combine(ParentDirectory(activeClientPath), L"BFVRD3D8To9.dll");
    if ((options.d3d8To9FlatProbe || options.d3d8To9ObserverProbe) &&
        !Exists(d3d8To9Path))
    {
        fwprintf(
            stderr,
            L"[FAIL] The isolated d3d8to9 translator was not found beside BFVRClient.dll.\n[INFO] translator=%ls\n",
            d3d8To9Path.c_str());
        return 2;
    }

    if (options.dryRun)
    {
        wprintf(
            L"[PASS] Dry run only.\n[INFO] game=%ls\n[INFO] client=%ls\n",
            executablePath.c_str(),
            activeClientPath.c_str());
        if (options.d3d8To9FlatProbe || options.d3d8To9ObserverProbe)
        {
            wprintf(L"[INFO] translator=%ls\n", d3d8To9Path.c_str());
        }
        return 0;
    }

    std::wstring commandLine = QuoteArgument(executablePath);
    for (const auto& argument : options.gameArguments)
    {
        commandLine += L" ";
        commandLine += QuoteArgument(argument);
    }
    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    ResetLoaderLog();
    AppendLoaderLog(options.weaponMotionProbe
            ? L"Starting the opt-in OpenXR right-hand 6DOF weapon presentation and rendered-weapon-directed local-infantry fire overlays."
            : options.runUntilStopped
            ? L"Starting a run-until-stopped live-game D3D9Ex-to-x64 OpenXR GPU shared-target proof."
            : options.weaponTransformOwnershipProbe
            ? L"Starting a bounded, isolated weapon transform-ownership test."
            : options.weaponViewModelProbe
            ? L"Starting a bounded, isolated forwarding-only weapon view-model draw capture."
            : options.weaponFireProbe
            ? L"Starting a bounded, isolated forwarding-only native weapon-fire capture."
            : options.playerInputProbe
            ? L"Starting a bounded, isolated 120-second forwarding-only PlayerInputMap observation run."
            : d3d8To9SharedFrameProbe
            ? L"Starting a bounded live-game D3D9Ex-to-x64 no-HMD GPU shared-target proof."
            : d3d8To9OpenXRPresentationProbe
            ? L"Starting a bounded live-game D3D9Ex-to-x64 OpenXR GPU shared-target proof."
            : options.d3d8To9FlatProbe
            ? L"Starting a bounded flat compatibility run through the pinned, isolated d3d8to9 translator."
            : options.d3d8StereoFrameProbe
            ? L"Starting a bounded observer run with the one-frame no-HMD D3D8 full draw-family stereo-stream proof enabled."
            : options.d3d8StereoFramePresentationProbe
            ? L"Starting a bounded observer run with the D3D8-to-x64 OpenXR world/UI presentation proof enabled."
            : options.d3d8StereoPairProbe
            ? L"Starting a bounded observer run with the explicit one-draw no-HMD D3D8 stereo-pair proof enabled."
            : options.d3d8StateCensusProbe
            ? L"Starting a bounded observer run with the explicit one-frame no-HMD D3D8 state census enabled."
            : options.d3d8CallInventoryProbe
            ? L"Starting a bounded observer run with the explicit one-frame no-HMD D3D8 call inventory enabled."
            : options.sceneBatchProbe
            ? L"Starting a bounded observer run with the explicit one-shot read-only scene-batch probe enabled."
            : options.configuredViewListWriterProbe
            ? L"Starting a bounded observer run with the explicit one-shot read-only configured-view list writer probe enabled."
            : options.configuredViewListProbe
            ? L"Starting a bounded observer run with the explicit one-shot read-only configured-view list probe enabled."
            : options.renderViewSingleEyeProbe
            ? L"Starting a bounded observer run with the explicit one-shot RenderView single-eye transform experiment enabled."
            : options.renderViewSetterBaselineProbe
            ? L"Starting a bounded observer run with the explicit one-shot RenderView setter pass-through baseline probe enabled."
            : options.renderViewTransformProbe
            ? L"Starting a bounded observer run with the explicit one-shot read-only RenderView transformation probe enabled."
            : options.surfaceD3D11UploadProbe
            ? L"Starting a bounded observer run with the explicit post-EndScene D3D11 upload probe enabled; it creates no OpenXR session."
            : options.surfaceSceneReadbackProbe
            ? L"Starting a bounded observer run with the explicit post-EndScene system-memory readback probe enabled."
            : options.surfaceResetProbe
            ? L"Starting a bounded observer run with the explicit reset-lifecycle owned-surface probe enabled."
            : options.surfaceStreamProbe
                ? L"Starting a bounded observer run with the explicit reset-aware owned-surface stream probe enabled."
            : options.surfaceCopyProbe
                ? L"Starting a bounded observer run with the explicit same-thread owned-surface copy probe enabled."
            : options.surfaceDescriptorProbe
                ? L"Starting a bounded observer run with the explicit same-thread surface-descriptor probe enabled."
            : options.presentBridgeProbe
                ? L"Starting a bounded observer run with the explicit no-op Present bridge probe enabled."
                : L"Starting a bounded passive observer run.");
    mutableCommandLine.push_back(L'\0');

    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_SHOWNORMAL;
    PROCESS_INFORMATION processInfo = {};
    constexpr wchar_t kForceWindowedEnvironment[] =
        L"BFVR_D3D8TO9_FORCE_WINDOWED";
    constexpr wchar_t kWorldRenderScaleEnvironment[] =
        L"BFVR_OPENXR_WORLD_RENDER_SCALE";
    constexpr wchar_t kRunUntilStoppedEnvironment[] =
        L"BFVR_PRESENTATION_RUN_UNTIL_STOPPED";
    constexpr wchar_t kWeaponMotionEnvironment[] =
        L"BFVR_ENABLE_WEAPON_MOTION";
    std::wstring priorForceWindowed;
    std::wstring priorRunUntilStopped;
    std::wstring priorWeaponMotion;
    bool hadPriorForceWindowed = false;
    bool hadPriorRunUntilStopped = false;
    bool hadPriorWeaponMotion = false;
    bool injectedNativeWorldRenderScale = false;
    if (d3d8To9SharedFrameProbe || d3d8To9OpenXRPresentationProbe)
    {
        const DWORD priorLength = GetEnvironmentVariableW(
            kForceWindowedEnvironment,
            nullptr,
            0);
        if (priorLength > 0)
        {
            std::vector<wchar_t> priorValue(priorLength);
            if (GetEnvironmentVariableW(
                    kForceWindowedEnvironment,
                    priorValue.data(),
                    priorLength) > 0)
            {
                priorForceWindowed = priorValue.data();
                hadPriorForceWindowed = true;
            }
        }
        if (!SetEnvironmentVariableW(
                kForceWindowedEnvironment,
                L"1"))
        {
            const DWORD error = GetLastError();
            fwprintf(
                stderr,
                L"[FAIL] Could not enable the translator windowed-interoperability mode (error=%lu).\n",
                error);
            AppendLoaderError(
                L"SetEnvironmentVariableW(BFVR_D3D8TO9_FORCE_WINDOWED)",
                error);
            return 2;
        }
        const DWORD existingWorldRenderScaleLength =
            GetEnvironmentVariableW(
                kWorldRenderScaleEnvironment,
                nullptr,
                0);
        if (existingWorldRenderScaleLength == 0)
        {
            if (!SetEnvironmentVariableW(
                    kWorldRenderScaleEnvironment,
                    L"1.0"))
            {
                const DWORD error = GetLastError();
                SetEnvironmentVariableW(
                    kForceWindowedEnvironment,
                    hadPriorForceWindowed
                        ? priorForceWindowed.c_str()
                        : nullptr);
                fwprintf(
                    stderr,
                    L"[FAIL] Could not select native source-eye resolution for the GPU-resident path (error=%lu).\n",
                    error);
                AppendLoaderError(
                    L"SetEnvironmentVariableW(BFVR_OPENXR_WORLD_RENDER_SCALE)",
                    error);
                return 2;
            }
            injectedNativeWorldRenderScale = true;
        }
        if (options.runUntilStopped)
        {
            const DWORD priorRunLength =
                GetEnvironmentVariableW(
                    kRunUntilStoppedEnvironment,
                    nullptr,
                    0);
            if (priorRunLength > 0)
            {
                std::vector<wchar_t> priorValue(priorRunLength);
                if (GetEnvironmentVariableW(
                        kRunUntilStoppedEnvironment,
                        priorValue.data(),
                        priorRunLength) > 0)
                {
                    priorRunUntilStopped = priorValue.data();
                    hadPriorRunUntilStopped = true;
                }
            }
            if (!SetEnvironmentVariableW(
                    kRunUntilStoppedEnvironment,
                    L"1"))
            {
                const DWORD error = GetLastError();
                SetEnvironmentVariableW(
                    kForceWindowedEnvironment,
                    hadPriorForceWindowed
                        ? priorForceWindowed.c_str()
                        : nullptr);
                if (injectedNativeWorldRenderScale)
                {
                    SetEnvironmentVariableW(
                        kWorldRenderScaleEnvironment,
                        nullptr);
                }
                fwprintf(
                    stderr,
                    L"[FAIL] Could not enable run-until-stopped presentation mode (error=%lu).\n",
                    error);
                AppendLoaderError(
                    L"SetEnvironmentVariableW(BFVR_PRESENTATION_RUN_UNTIL_STOPPED)",
                    error);
                return 2;
            }
        }
        if (options.weaponMotionProbe)
        {
            const DWORD priorMotionLength = GetEnvironmentVariableW(
                kWeaponMotionEnvironment,
                nullptr,
                0);
            if (priorMotionLength > 0)
            {
                std::vector<wchar_t> priorValue(priorMotionLength);
                if (GetEnvironmentVariableW(
                        kWeaponMotionEnvironment,
                        priorValue.data(),
                        priorMotionLength) > 0)
                {
                    priorWeaponMotion = priorValue.data();
                    hadPriorWeaponMotion = true;
                }
            }
            if (!SetEnvironmentVariableW(kWeaponMotionEnvironment, L"1"))
            {
                const DWORD error = GetLastError();
                SetEnvironmentVariableW(
                    kForceWindowedEnvironment,
                    hadPriorForceWindowed
                        ? priorForceWindowed.c_str()
                        : nullptr);
                if (injectedNativeWorldRenderScale)
                {
                    SetEnvironmentVariableW(
                        kWorldRenderScaleEnvironment,
                        nullptr);
                }
                if (options.runUntilStopped)
                {
                    SetEnvironmentVariableW(
                        kRunUntilStoppedEnvironment,
                        hadPriorRunUntilStopped
                            ? priorRunUntilStopped.c_str()
                            : nullptr);
                }
                fwprintf(stderr, L"[FAIL] Could not enable weapon-motion presentation mode (error=%lu).\n", error);
                AppendLoaderError(
                    L"SetEnvironmentVariableW(BFVR_ENABLE_WEAPON_MOTION)",
                    error);
                return 2;
            }
        }
    }
    const BOOL processCreated = CreateProcessW(
            executablePath.c_str(),
            mutableCommandLine.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_SUSPENDED,
            nullptr,
            options.gameRoot.c_str(),
            &startupInfo,
            &processInfo);
    if (d3d8To9SharedFrameProbe || d3d8To9OpenXRPresentationProbe)
    {
        SetEnvironmentVariableW(
            kForceWindowedEnvironment,
            hadPriorForceWindowed
                ? priorForceWindowed.c_str()
                : nullptr);
        if (injectedNativeWorldRenderScale)
        {
            SetEnvironmentVariableW(
                kWorldRenderScaleEnvironment,
                nullptr);
        }
        if (options.runUntilStopped)
        {
            SetEnvironmentVariableW(
                kRunUntilStoppedEnvironment,
                hadPriorRunUntilStopped
                    ? priorRunUntilStopped.c_str()
                    : nullptr);
        }
        if (options.weaponMotionProbe)
        {
            SetEnvironmentVariableW(
                kWeaponMotionEnvironment,
                hadPriorWeaponMotion ? priorWeaponMotion.c_str() : nullptr);
        }
    }
    if (!processCreated)
    {
        const DWORD error = GetLastError();
        fwprintf(stderr, L"[FAIL] CreateProcessW failed: %lu\n", error);
        AppendLoaderError(L"CreateProcessW", error);
        if (error == ERROR_ELEVATION_REQUIRED)
        {
            fwprintf(stderr, L"[BLOCKED] BF1942.exe requires elevation. Start BFVRLoader as administrator, then retry the offline observer run.\n");
        }
        return 2;
    }

    // Keep a loader-private duplicate solely for process-lifetime decisions.
    // Injection/initialization use processInfo.hProcess, so a mistaken close or
    // reuse of that working handle must never decide that a still-running game
    // has ended the continuous OpenXR session.
    HANDLE processLifetimeHandle = nullptr;
    if (!DuplicateHandle(
            GetCurrentProcess(),
            processInfo.hProcess,
            GetCurrentProcess(),
            &processLifetimeHandle,
            0,
            FALSE,
            DUPLICATE_SAME_ACCESS))
    {
        const DWORD error = GetLastError();
        fwprintf(
            stderr,
            L"[FAIL] Could not duplicate the BF1942 process lifetime handle: %lu\n",
            error);
        AppendLoaderError(L"DuplicateHandle(BF1942 process lifetime)", error);
        TerminateProcess(processInfo.hProcess, 1);
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
        return 2;
    }
    AppendLoaderLog(
        L"Created a loader-private duplicate of the launched BF1942 process handle for lifetime waits and exit-code queries.");

    HANDLE d3d8ProbeCompletionEvent = nullptr;
    if (options.d3d8CallInventoryProbe ||
        options.d3d8StateCensusProbe ||
        options.weaponViewModelProbe ||
        options.weaponTransformOwnershipProbe ||
        options.d3d8StereoPairProbe ||
        options.d3d8StereoFrameProbe ||
        options.d3d8StereoFramePresentationProbe)
    {
        const std::wstring eventName = D3D8ProbeCompletionEventName(processInfo.dwProcessId);
        d3d8ProbeCompletionEvent = eventName.empty()
            ? nullptr
            : CreateEventW(nullptr, TRUE, FALSE, eventName.c_str());
        if (d3d8ProbeCompletionEvent == nullptr)
        {
            const DWORD error = GetLastError();
            AppendLoaderError(
                L"CreateEventW(D3D8 probe completion)",
                error);
            if (options.runUntilStopped)
            {
                fwprintf(
                    stderr,
                    L"[FAIL] Could not create the continuous presentation completion event (error=%lu).\n",
                    error);
                TerminateProcess(processInfo.hProcess, 1);
                CloseHandle(processInfo.hThread);
                CloseHandle(processInfo.hProcess);
                CloseHandle(processLifetimeHandle);
                return 2;
            }
        }
    }
    DWORD remoteModuleBase = 0;
    const bool injected = InjectLibrary(processInfo.hProcess, activeClientPath, remoteModuleBase);
    if (!injected)
    {
        TerminateProcess(processInfo.hProcess, 1);
        CloseHandle(processInfo.hThread);
        CloseHandle(d3d8ProbeCompletionEvent);
        AppendLoaderLog(L"InjectLibrary returned failure; terminating the suspended child process.");
        CloseHandle(processInfo.hProcess);
        CloseHandle(processLifetimeHandle);
        return 2;
    }
    AppendLoaderLog(options.d3d8To9FlatProbe
        ? L"Injected the DXGI-free BFVRD3D8To9FlatClient.dll into the suspended BF1942.exe process."
        : L"Injected BFVRClient.dll into the suspended BF1942.exe process.");

    if (!InitializeObserver(processInfo.hProcess, processInfo.dwThreadId, activeClientPath, remoteModuleBase, options.presentBridgeProbe, options.surfaceDescriptorProbe, options.surfaceCopyProbe, options.surfaceStreamProbe, options.surfaceResetProbe, options.surfaceReadbackProbe, options.surfaceSceneReadbackProbe, options.surfaceD3D11UploadProbe, options.renderViewTransformProbe, options.renderViewSetterBaselineProbe, options.renderViewSingleEyeProbe, options.configuredViewListProbe, options.configuredViewListWriterProbe, options.sceneBatchProbe, options.d3d8CallInventoryProbe, options.d3d8StateCensusProbe, options.d3d8StereoPairProbe, options.d3d8StereoFrameProbe, options.d3d8StereoFramePresentationProbe, options.d3d8To9FlatProbe, options.d3d8To9ObserverProbe, options.playerInputProbe, options.weaponViewModelProbe, options.weaponTransformOwnershipProbe, options.weaponFireProbe))
    {
        TerminateProcess(processInfo.hProcess, 1);
        CloseHandle(processInfo.hThread);
        CloseHandle(d3d8ProbeCompletionEvent);
        AppendLoaderLog(L"BFVRInitializeObserver returned failure; terminating the suspended child process.");
        CloseHandle(processInfo.hProcess);
        CloseHandle(processLifetimeHandle);
        return 2;
    }
    AppendLoaderLog(d3d8To9SharedFrameProbe
             ? L"Initialized the D3D8 observer, pinned d3d8to9 translator, and x64 no-HMD GPU shared-target proof outside DllMain while BF1942.exe remained suspended."
             : d3d8To9OpenXRPresentationProbe
             ? L"Initialized the D3D8 observer, pinned d3d8to9 translator, and x64 OpenXR GPU shared-target proof outside DllMain while BF1942.exe remained suspended."
             : options.d3d8To9FlatProbe
             ? L"Initialized the D3D8 observer with the pinned, isolated d3d8to9 flat redirect outside DllMain while BF1942.exe remained suspended."
             : options.d3d8To9ObserverProbe
             ? L"Initialized the full D3D8 observer with the pinned d3d8to9 translator downstream outside DllMain while BF1942.exe remained suspended."
            : options.weaponTransformOwnershipProbe
            ? L"Initialized the BFVR observer and requested the isolated bounded weapon transform-ownership test outside DllMain while BF1942.exe remained suspended."
            : options.weaponViewModelProbe
            ? L"Initialized the BFVR observer and requested the isolated forwarding-only weapon view-model draw capture outside DllMain while BF1942.exe remained suspended."
            : options.playerInputProbe
            ? L"Initialized the BFVR observer and requested the isolated PlayerInputMap forwarding-only trace outside DllMain while BF1942.exe remained suspended."
            : options.d3d8StereoFramePresentationProbe
            ? L"Initialized the D3D8 observer and requested the D3D8-to-x64 OpenXR world/UI presentation proof outside DllMain while BF1942.exe remained suspended."
            : options.d3d8StereoFrameProbe
            ? L"Initialized the D3D8 observer and requested the one-frame no-HMD D3D8 full draw-family stereo-stream proof outside DllMain while BF1942.exe remained suspended."
            : options.d3d8StereoPairProbe
            ? L"Initialized the D3D8 observer and requested the one-draw no-HMD D3D8 stereo-pair proof outside DllMain while BF1942.exe remained suspended."
            : options.d3d8StateCensusProbe
            ? L"Initialized the D3D8 observer and requested the one-frame no-HMD D3D8 state census outside DllMain while BF1942.exe remained suspended."
            : options.d3d8CallInventoryProbe
            ? L"Initialized the D3D8 observer and requested the one-frame no-HMD D3D8 call inventory outside DllMain while BF1942.exe remained suspended."
            : options.sceneBatchProbe
            ? L"Initialized the D3D8 observer and requested the one-shot read-only scene-batch probe outside DllMain while BF1942.exe remained suspended."
            : options.configuredViewListWriterProbe
            ? L"Initialized the D3D8 observer and requested the one-shot read-only configured-view list writer probe outside DllMain while BF1942.exe remained suspended."
            : options.configuredViewListProbe
            ? L"Initialized the D3D8 observer and requested the one-shot read-only configured-view list probe outside DllMain while BF1942.exe remained suspended."
            : options.renderViewSingleEyeProbe
            ? L"Initialized the D3D8 observer and requested the one-shot RenderView single-eye transform experiment outside DllMain while BF1942.exe remained suspended."
            : options.renderViewSetterBaselineProbe
            ? L"Initialized the D3D8 observer and requested the one-shot RenderView setter pass-through baseline probe outside DllMain while BF1942.exe remained suspended."
            : options.renderViewTransformProbe
            ? L"Initialized the D3D8 observer and requested the one-shot read-only RenderView transformation probe outside DllMain while BF1942.exe remained suspended."
            : options.surfaceD3D11UploadProbe
            ? L"Initialized the D3D8 observer and requested the one-shot ordinary-world post-EndScene D3D11 upload probe outside DllMain while BF1942.exe remained suspended; no OpenXR session was requested."
            : options.surfaceSceneReadbackProbe
            ? L"Initialized the D3D8 observer and requested the one-shot ordinary-world post-EndScene system-memory readback probe outside DllMain while BF1942.exe remained suspended."
            : options.surfaceReadbackProbe
            ? L"Initialized the D3D8 observer and requested the one-shot ordinary-world system-memory readback probe outside DllMain while BF1942.exe remained suspended."
            : options.surfaceResetProbe
            ? L"Initialized the D3D8 observer and requested the reset-lifecycle owned-surface probe outside DllMain while BF1942.exe remained suspended."
            : options.surfaceStreamProbe
                ? L"Initialized the D3D8 observer and requested the reset-aware owned-surface stream probe outside DllMain while BF1942.exe remained suspended."
            : options.surfaceCopyProbe
                ? L"Initialized the D3D8 observer and requested the same-thread owned-surface copy probe outside DllMain while BF1942.exe remained suspended."
            : options.surfaceDescriptorProbe
                ? L"Initialized the D3D8 observer and requested the same-thread surface-descriptor probe outside DllMain while BF1942.exe remained suspended."
            : options.presentBridgeProbe
                ? L"Initialized the D3D8 observer and requested the no-op Present bridge probe outside DllMain while BF1942.exe remained suspended."
                : L"Initialized the D3D8 observer outside DllMain while BF1942.exe remained suspended.");

    if (ResumeThread(processInfo.hThread) == static_cast<DWORD>(-1))
    {
        fwprintf(stderr, L"[FAIL] ResumeThread failed: %lu\n", GetLastError());
        TerminateProcess(processInfo.hProcess, 1);
        CloseHandle(processInfo.hThread);
        CloseHandle(d3d8ProbeCompletionEvent);
        CloseHandle(processInfo.hProcess);
        CloseHandle(processLifetimeHandle);
        return 2;
    }
    AppendLoaderLog(d3d8To9SharedFrameProbe
             ? L"Resumed BF1942.exe with runtime-paced D3D9Ex shared eye/UI targets and the x64 no-HMD consumer requested."
             : d3d8To9OpenXRPresentationProbe
             ? L"Resumed BF1942.exe with runtime-paced D3D9Ex shared eye/UI targets and the x64 OpenXR presenter requested."
             : options.d3d8To9FlatProbe
             ? L"Resumed BF1942.exe after redirecting Direct3DCreate8 through the pinned, isolated d3d8to9 translator."
             : options.d3d8To9ObserverProbe
             ? L"Resumed BF1942.exe with the full D3D8 observer in front of the pinned d3d8to9 translator."
            : options.weaponTransformOwnershipProbe
            ? L"Resumed BF1942.exe after observer injection with the isolated bounded weapon transform-ownership test requested."
            : options.weaponViewModelProbe
            ? L"Resumed BF1942.exe after observer injection with the isolated forwarding-only weapon view-model draw capture requested."
            : options.playerInputProbe
            ? L"Resumed BF1942.exe after observer injection with the isolated PlayerInputMap forwarding-only trace requested."
            : options.d3d8StereoFramePresentationProbe
            ? L"Resumed BF1942.exe after observer injection with the D3D8-to-x64 OpenXR world/UI presentation proof requested."
            : options.d3d8StereoFrameProbe
            ? L"Resumed BF1942.exe after observer injection with the one-frame no-HMD D3D8 full draw-family stereo-stream proof requested."
            : options.d3d8StereoPairProbe
            ? L"Resumed BF1942.exe after observer injection with the one-draw no-HMD D3D8 stereo-pair proof requested."
            : options.d3d8StateCensusProbe
            ? L"Resumed BF1942.exe after observer injection with the one-frame no-HMD D3D8 state census requested."
            : options.d3d8CallInventoryProbe
            ? L"Resumed BF1942.exe after observer injection with the one-frame no-HMD D3D8 call inventory requested."
            : options.sceneBatchProbe
            ? L"Resumed BF1942.exe after observer injection with the one-shot read-only scene-batch probe requested."
            : options.configuredViewListWriterProbe
            ? L"Resumed BF1942.exe after observer injection with the one-shot read-only configured-view list writer probe requested."
            : options.configuredViewListProbe
            ? L"Resumed BF1942.exe after observer injection with the one-shot read-only configured-view list probe requested."
            : options.renderViewSingleEyeProbe
            ? L"Resumed BF1942.exe after observer injection with the one-shot RenderView single-eye transform experiment requested."
            : options.renderViewSetterBaselineProbe
            ? L"Resumed BF1942.exe after observer injection with the one-shot RenderView setter pass-through baseline probe requested."
            : options.renderViewTransformProbe
            ? L"Resumed BF1942.exe after observer injection with the one-shot read-only RenderView transformation probe requested."
            : options.surfaceD3D11UploadProbe
            ? L"Resumed BF1942.exe after observer injection with the one-shot ordinary-world post-EndScene D3D11 upload probe requested; no OpenXR session was requested."
            : options.surfaceSceneReadbackProbe
            ? L"Resumed BF1942.exe after observer injection with the one-shot ordinary-world post-EndScene system-memory readback probe requested."
            : options.surfaceReadbackProbe
            ? L"Resumed BF1942.exe after observer injection with the one-shot ordinary-world system-memory readback probe requested."
            : options.surfaceResetProbe
            ? L"Resumed BF1942.exe after observer injection with the reset-lifecycle owned-surface probe requested."
            : options.surfaceStreamProbe
                ? L"Resumed BF1942.exe after observer injection with the reset-aware owned-surface stream probe requested."
            : options.surfaceCopyProbe
                ? L"Resumed BF1942.exe after observer injection with the same-thread owned-surface copy probe requested."
            : options.surfaceDescriptorProbe
                ? L"Resumed BF1942.exe after observer injection with the same-thread surface-descriptor probe requested."
            : options.presentBridgeProbe
                ? L"Resumed BF1942.exe after observer injection with the no-op Present bridge probe requested."
                : L"Resumed BF1942.exe after passive observer injection.");
    wprintf(d3d8To9SharedFrameProbe
             ? L"[PASS] Started BF1942.exe with the live D3D9Ex-to-x64 no-HMD GPU shared-target proof (pid=%lu).\n"
             : d3d8To9OpenXRPresentationProbe
             ? L"[PASS] Started BF1942.exe with the live D3D9Ex-to-x64 OpenXR GPU shared-target proof (pid=%lu).\n"
             : options.d3d8To9FlatProbe
             ? L"[PASS] Started BF1942.exe with the pinned, isolated d3d8to9 flat compatibility path (pid=%lu).\n"
             : options.d3d8To9ObserverProbe
             ? L"[PASS] Started BF1942.exe with the full BFVR D3D8 observer in front of the pinned d3d8to9 translator (pid=%lu).\n"
            : options.weaponTransformOwnershipProbe
            ? L"[PASS] Started BF1942.exe with the isolated bounded weapon transform-ownership test (pid=%lu).\n"
            : options.weaponViewModelProbe
            ? L"[PASS] Started BF1942.exe with the isolated forwarding-only weapon view-model draw capture (pid=%lu).\n"
            : options.playerInputProbe
            ? L"[PASS] Started BF1942.exe with the isolated PlayerInputMap forwarding-only trace (pid=%lu).\n"
            : options.d3d8StereoFramePresentationProbe
            ? L"[PASS] Started BF1942.exe with the BFVR D3D8 observer and opt-in D3D8-to-x64 OpenXR world/UI presentation proof (pid=%lu).\n"
            : options.d3d8StereoFrameProbe
            ? L"[PASS] Started BF1942.exe with the BFVR D3D8 observer and opt-in one-frame no-HMD D3D8 full draw-family stereo-stream proof (pid=%lu).\n"
            : options.d3d8StereoPairProbe
            ? L"[PASS] Started BF1942.exe with the BFVR D3D8 observer and opt-in one-draw no-HMD D3D8 stereo-pair proof (pid=%lu).\n"
            : options.d3d8StateCensusProbe
            ? L"[PASS] Started BF1942.exe with the BFVR D3D8 observer and opt-in one-frame no-HMD D3D8 state census (pid=%lu).\n"
            : options.d3d8CallInventoryProbe
            ? L"[PASS] Started BF1942.exe with the BFVR D3D8 observer and opt-in one-frame no-HMD D3D8 call inventory (pid=%lu).\n"
            : options.sceneBatchProbe
            ? L"[PASS] Started BF1942.exe with the BFVR D3D8 observer and opt-in one-shot read-only scene-batch probe (pid=%lu).\n"
            : options.configuredViewListWriterProbe
            ? L"[PASS] Started BF1942.exe with the BFVR D3D8 observer and opt-in one-shot read-only configured-view list writer probe (pid=%lu).\n"
            : options.configuredViewListProbe
            ? L"[PASS] Started BF1942.exe with the BFVR D3D8 observer and opt-in one-shot read-only configured-view list probe (pid=%lu).\n"
            : options.renderViewSingleEyeProbe
            ? L"[PASS] Started BF1942.exe with the BFVR D3D8 observer and opt-in one-shot RenderView single-eye transform experiment (pid=%lu).\n"
            : options.renderViewSetterBaselineProbe
            ? L"[PASS] Started BF1942.exe with the BFVR D3D8 observer and opt-in one-shot RenderView setter pass-through baseline probe (pid=%lu).\n"
            : options.renderViewTransformProbe
            ? L"[PASS] Started BF1942.exe with the BFVR D3D8 observer and opt-in one-shot read-only RenderView transformation probe (pid=%lu).\n"
            : options.surfaceD3D11UploadProbe
            ? L"[PASS] Started BF1942.exe with the BFVR D3D8 observer and opt-in ordinary-world post-EndScene D3D11 upload probe; no OpenXR session is created (pid=%lu).\n"
            : options.surfaceSceneReadbackProbe
            ? L"[PASS] Started BF1942.exe with the BFVR D3D8 observer and opt-in ordinary-world post-EndScene system-memory readback probe (pid=%lu).\n"
            : options.surfaceReadbackProbe
            ? L"[PASS] Started BF1942.exe with the BFVR D3D8 observer and opt-in ordinary-world system-memory readback probe (pid=%lu).\n"
            : options.surfaceResetProbe
            ? L"[PASS] Started BF1942.exe with the BFVR D3D8 observer and opt-in reset-lifecycle owned-surface probe (pid=%lu).\n"
            : options.surfaceStreamProbe
                ? L"[PASS] Started BF1942.exe with the BFVR D3D8 observer and opt-in reset-aware owned-surface stream probe (pid=%lu).\n"
            : options.surfaceCopyProbe
                ? L"[PASS] Started BF1942.exe with the BFVR D3D8 observer and opt-in same-thread owned-surface copy probe (pid=%lu).\n"
            : options.surfaceDescriptorProbe
                ? L"[PASS] Started BF1942.exe with the BFVR D3D8 observer and opt-in same-thread surface-descriptor probe (pid=%lu).\n"
            : options.presentBridgeProbe
                ? L"[PASS] Started BF1942.exe with the BFVR D3D8 observer and opt-in no-op Present bridge probe (pid=%lu).\n"
                : L"[PASS] Started BF1942.exe with the passive BFVR D3D8 observer (pid=%lu).\n",
        processInfo.dwProcessId);
    if (options.diagnosticTimeoutMs != 0)
    {
        HANDLE waitHandles[2] = {processLifetimeHandle, d3d8ProbeCompletionEvent};
        const DWORD waitHandleCount = d3d8ProbeCompletionEvent == nullptr ? 1 : 2;
        const DWORD wait = WaitForMultipleObjects(waitHandleCount, waitHandles, FALSE, options.diagnosticTimeoutMs);
        if (wait == WAIT_TIMEOUT)
        {
            TerminateProcess(processLifetimeHandle, 0);
            wprintf(L"[INFO] Diagnostic observation window elapsed; closed the process started by this loader.\n");
            AppendLoaderLog(L"Diagnostic timeout elapsed; terminated the directly launched BF1942.exe process.");
        }
        else if (d3d8ProbeCompletionEvent != nullptr && wait == WAIT_OBJECT_0 + 1)
        {
            TerminateProcess(processLifetimeHandle, 0);
            wprintf(L"[INFO] Bounded D3D8 diagnostic completed; closed the process started by this loader.\n");
            AppendLoaderLog(L"Bounded D3D8 diagnostic signaled completion; terminated the directly launched BF1942.exe process.");
        }
        else if (wait == WAIT_OBJECT_0)
        {
            DWORD childExitCode = 0;
            if (GetExitCodeProcess(processLifetimeHandle, &childExitCode))
            {
                wchar_t message[256] = {};
                swprintf_s(message, std::size(message), L"The initially launched BF1942.exe exited with code %lu.", childExitCode);
                AppendLoaderLog(message);
                wprintf(L"[INFO] BF1942.exe exited during the diagnostic observation window with code %lu.\n", childExitCode);
            }
            else
            {
                AppendLoaderError(L"GetExitCodeProcess", GetLastError());
                fwprintf(stderr, L"[WARN] GetExitCodeProcess failed: %lu\n", GetLastError());
            }
        }
        else
        {
            fwprintf(stderr, L"[WARN] WaitForSingleObject failed: %lu\n", GetLastError());
        }
    }
    else if (options.runUntilStopped)
    {
        wprintf(
            L"[INFO] Presentation will follow BF1942's process lifetime, including a direct BF1942 replacement process.\n");
        AppendLoaderLog(
            L"Run-until-stopped OpenXR presentation is active and follows the launched BF1942 process plus any direct BF1942 replacement process.");

        // "End Current Game" exits the active BF1942.exe with code 1 and
        // starts a same-install replacement that owns the 2D main menu.
        // Follow that successor before treating the player session as finished.
        std::vector<DWORD> attachedProcessIds = {processInfo.dwProcessId};
        DWORD handoffSearchStartedAt = 0;
        constexpr DWORD kSuccessorSearchTimeoutMs = 10000;
        for (;;)
        {
            const ReplacementProcess successor =
                FindBf1942Replacement(
                    executablePath,
                    attachedProcessIds);
            if (successor.handle != nullptr)
            {
                DWORD successorModuleBase = 0;
                const bool injectedSuccessor =
                    InjectLibrary(
                        successor.handle,
                        activeClientPath,
                        successorModuleBase) &&
                    InitializeObserver(
                        successor.handle,
                        successor.threadId,
                        activeClientPath,
                        successorModuleBase,
                        options.presentBridgeProbe,
                        options.surfaceDescriptorProbe,
                        options.surfaceCopyProbe,
                        options.surfaceStreamProbe,
                        options.surfaceResetProbe,
                        options.surfaceReadbackProbe,
                        options.surfaceSceneReadbackProbe,
                        options.surfaceD3D11UploadProbe,
                        options.renderViewTransformProbe,
                        options.renderViewSetterBaselineProbe,
                        options.renderViewSingleEyeProbe,
                        options.configuredViewListProbe,
                        options.configuredViewListWriterProbe,
                        options.sceneBatchProbe,
                        options.d3d8CallInventoryProbe,
                        options.d3d8StateCensusProbe,
                        options.d3d8StereoPairProbe,
                        options.d3d8StereoFrameProbe,
                        options.d3d8StereoFramePresentationProbe,
                        options.d3d8To9FlatProbe,
                        options.d3d8To9ObserverProbe,
                        options.playerInputProbe,
                        options.weaponViewModelProbe,
                        options.weaponTransformOwnershipProbe,
                        options.weaponFireProbe);
                attachedProcessIds.push_back(successor.processId);
                if (!injectedSuccessor)
                {
                    AppendLoaderLog(
                        L"A same-install BF1942 replacement process was found, but BFVR injection/initialization failed; it will not be retried for this process id.");
                    CloseHandle(successor.handle);
                    continue;
                }

                CloseHandle(processLifetimeHandle);
                processLifetimeHandle = successor.handle;
                processInfo.dwProcessId = successor.processId;
                handoffSearchStartedAt = 0;
                wchar_t message[256] = {};
                swprintf_s(
                    message,
                    std::size(message),
                    L"Attached BFVR to replacement BF1942.exe pid=%lu; OpenXR presentation will re-enter through its startup menu bridge.",
                    successor.processId);
                AppendLoaderLog(message);
                continue;
            }

            const DWORD wait =
                WaitForSingleObject(processLifetimeHandle, 20);
            if (wait == WAIT_TIMEOUT)
            {
                handoffSearchStartedAt = 0;
                continue;
            }
            if (wait != WAIT_OBJECT_0)
            {
                fwprintf(
                    stderr,
                    L"[WARN] Continuous presentation wait failed: %lu\n",
                    GetLastError());
                break;
            }

            DWORD childExitCode = STILL_ACTIVE;
            const BOOL exitCodeRead = GetExitCodeProcess(
                processLifetimeHandle,
                &childExitCode);
            if (exitCodeRead == FALSE || childExitCode == STILL_ACTIVE)
            {
                AppendLoaderLog(
                    L"BF1942 lifetime handle signaled without a final exit code; continuing successor search.");
            }
            else if (handoffSearchStartedAt == 0)
            {
                handoffSearchStartedAt = GetTickCount();
                wchar_t message[256] = {};
                swprintf_s(
                    message,
                    std::size(message),
                    L"BF1942.exe pid=%lu exited with code %lu; waiting up to %lu ms for a same-install replacement process before ending VR.",
                    processInfo.dwProcessId,
                    childExitCode,
                    kSuccessorSearchTimeoutMs);
                AppendLoaderLog(message);
            }
            if (handoffSearchStartedAt != 0 &&
                GetTickCount() - handoffSearchStartedAt >=
                    kSuccessorSearchTimeoutMs)
            {
                AppendLoaderLog(
                    L"No same-install BF1942 replacement process appeared after the active game exited; ending run-until-stopped presentation.");
                break;
            }
            Sleep(20);
        }
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(d3d8ProbeCompletionEvent);
    CloseHandle(processLifetimeHandle);
    CloseHandle(processInfo.hProcess);
    return 0;
}
