void AppendModuleForAddress(const wchar_t* label, const void* address)
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(address),
            &module))
    {
        AppendLog(L"%s target=%p module lookup failed (%lu).", label, address, GetLastError());
        return;
    }

    wchar_t path[MAX_PATH] = {};
    if (GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path))) == 0)
    {
        AppendLog(L"%s target=%p module path lookup failed (%lu).", label, address, GetLastError());
        return;
    }
    AppendLog(L"%s target=%p belongs to %s.", label, address, path);
}

void WriteOpenXRBootstrapLog(void*, const wchar_t* message)
{
    AppendLog(L"%s", message);
}

bool ReadCompatibilityLayer(HKEY rootKey, const wchar_t* executablePath, std::wstring& value)
{
    constexpr wchar_t kCompatibilityLayersKey[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers";
    DWORD valueType = 0;
    DWORD valueSize = 0;
    const LSTATUS sizeStatus = RegGetValueW(
        rootKey,
        kCompatibilityLayersKey,
        executablePath,
        RRF_RT_REG_SZ,
        &valueType,
        nullptr,
        &valueSize);
    if (sizeStatus == ERROR_FILE_NOT_FOUND)
    {
        return false;
    }
    if (sizeStatus != ERROR_SUCCESS || valueSize < sizeof(wchar_t))
    {
        AppendLog(L"OpenXR compatibility-layer query failed (root=%p status=%ld).", rootKey, static_cast<long>(sizeStatus));
        return false;
    }

    std::vector<wchar_t> buffer(valueSize / sizeof(wchar_t) + 1, L'\0');
    const LSTATUS valueStatus = RegGetValueW(
        rootKey,
        kCompatibilityLayersKey,
        executablePath,
        RRF_RT_REG_SZ,
        &valueType,
        buffer.data(),
        &valueSize);
    if (valueStatus != ERROR_SUCCESS)
    {
        AppendLog(L"OpenXR compatibility-layer value query failed (root=%p status=%ld).", rootKey, static_cast<long>(valueStatus));
        return false;
    }

    value.assign(buffer.data());
    return true;
}

bool HasKnownOpenXRBlockingCompatibilityLayers()
{
    wchar_t executablePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, executablePath, static_cast<DWORD>(std::size(executablePath))) == 0)
    {
        AppendLog(L"OpenXR compatibility guard could not determine the game executable path.");
        return false;
    }

    std::wstring userLayer;
    std::wstring machineLayer;
    const bool hasUserLayer = ReadCompatibilityLayer(HKEY_CURRENT_USER, executablePath, userLayer);
    const bool hasMachineLayer = ReadCompatibilityLayer(HKEY_LOCAL_MACHINE, executablePath, machineLayer);
    if (hasUserLayer || hasMachineLayer)
    {
        AppendLog(
            L"OpenXR compatibility layers detected: current-user='%s' machine='%s'.",
            hasUserLayer ? userLayer.c_str() : L"(none)",
            hasMachineLayer ? machineLayer.c_str() : L"(none)");
    }

    const bool knownCombination = userLayer.find(L"WIN95") != std::wstring::npos &&
        machineLayer.find(L"NT4SP5") != std::wstring::npos &&
        machineLayer.find(L"RUNASADMIN") != std::wstring::npos;
    if (knownCombination)
    {
        AppendLog(L"OpenXR bootstrap skipped: the known WIN95 + NT4SP5 RUNASADMIN combination makes the Oculus runtime unavailable. Flat fallback remains active; BFVR will not alter compatibility settings.");
    }
    return knownCombination;
}

DWORD WINAPI RunOpenXRBootstrap(LPVOID)
{
    if (g_module == nullptr)
    {
        return 0;
    }

    wchar_t modulePath[MAX_PATH] = {};
    if (GetModuleFileNameW(g_module, modulePath, static_cast<DWORD>(std::size(modulePath))) == 0)
    {
        AppendLog(L"OpenXR bootstrap skipped because the BFVR module directory could not be determined.");
        return 0;
    }

    wchar_t* separator = wcsrchr(modulePath, L'\\');
    if (separator == nullptr)
    {
        AppendLog(L"OpenXR bootstrap skipped because the BFVR module path has no directory component.");
        return 0;
    }
    *separator = L'\0';

    if (HasKnownOpenXRBlockingCompatibilityLayers())
    {
        return 0;
    }

    bfvr::ProbeOpenXRRuntime(modulePath, WriteOpenXRBootstrapLog, nullptr);
    return 0;
}

void StartOpenXRBootstrap()
{
    if (InterlockedCompareExchange(&g_openXRBootstrapStarted, 1, 0) != 0)
    {
        return;
    }

    HANDLE worker = CreateThread(nullptr, 0, RunOpenXRBootstrap, nullptr, 0, nullptr);
    if (worker == nullptr)
    {
        AppendLog(L"OpenXR bootstrap worker could not start (error %lu); flat fallback remains active.", GetLastError());
        return;
    }
    CloseHandle(worker);
    AppendLog(L"Started flat-safe OpenXR bootstrap worker; no session or graphics binding will be created.");
}

constexpr DWORD_PTR kLocalPlayerManagerGlobalAddress = 0x0097D76C;
constexpr std::size_t kPlayerManagerCurrentPlayerSlot = 8;
constexpr std::size_t kBFPlayerCameraOffset = 0x68;
constexpr std::size_t kBFPlayerIsAliveOffset = 0xa9;
constexpr std::size_t kLocalCameraActiveTransformOffset = 0x58;
constexpr DWORD kLocalCameraTransformSamplePeriod = 45;
constexpr LONG kLocalCameraTransformMaximumSamples = 8;
constexpr LONG kSustainedNonZeroCameraTransformSamplesRequired = 8;
constexpr LONG kSustainedLocalPlayerAliveSamplesRequired = 8;

void CaptureLocalCameraTransformAtProjection(LocalCameraTransformSample& sample, const void* expectedAddRef)
{
    sample.tick = GetTickCount();
    sample.localPlayer = static_cast<DWORD>(InterlockedCompareExchange(&g_observedLocalPlayer, 0, 0));
    if (sample.localPlayer == 0 || expectedAddRef == nullptr)
    {
        return;
    }

    __try
    {
        const auto* localPlayer = reinterpret_cast<const std::byte*>(sample.localPlayer);
        const void* const cameraInterface = *reinterpret_cast<void* const*>(localPlayer + kBFPlayerCameraOffset);
        if (cameraInterface == nullptr)
        {
            return;
        }
        auto** const cameraVtable = *reinterpret_cast<void***>(const_cast<void*>(cameraInterface));
        if (cameraVtable == nullptr || cameraVtable[0] != expectedAddRef)
        {
            return;
        }
        sample.cameraInterface = static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(cameraInterface));
        std::memcpy(sample.matrix, reinterpret_cast<const std::byte*>(cameraInterface) + kLocalCameraActiveTransformOffset, sizeof(sample.matrix));
        sample.readable = TRUE;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        sample.readable = FALSE;
    }
}

DWORD WINAPI ProbeLocalPlayerManager(LPVOID)
{
    bool getterReported = false;
    DWORD lastLocalPlayer = 0;
    DWORD lastCamera = 0;
    bool localPlayerAliveKnown = false;
    bool lastLocalPlayerAlive = false;
    bool unexpectedCameraInterfaceReported = false;
    LONG transformSamples = 0;
    const auto* const gameImage = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
    for (DWORD attempt = 0; attempt < 360; ++attempt)
    {
        __try
        {
            auto* const manager = *reinterpret_cast<void* const*>(kLocalPlayerManagerGlobalAddress);
            if (manager != nullptr)
            {
                auto** const managerVtable = *reinterpret_cast<void***>(manager);
                if (managerVtable != nullptr && managerVtable[kPlayerManagerCurrentPlayerSlot] != nullptr)
                {
                    const void* currentPlayerGetter = managerVtable[kPlayerManagerCurrentPlayerSlot];
                    if (!getterReported)
                    {
                        AppendLog(L"Player-manager singleton=%p; vtable+0x20 current-player getter target=%p. The probe only read this metadata and did not invoke the getter.", manager, currentPlayerGetter);
                        AppendModuleForAddress(L"Player-manager vtable+0x20", currentPlayerGetter);
                        getterReported = true;
                    }

                    const auto* const managerBytes = reinterpret_cast<const std::byte*>(manager);
                    const void* const localPlayer = *reinterpret_cast<void* const*>(managerBytes + 0x54);
                    if (localPlayer != nullptr)
                    {
                        InterlockedExchange(&g_observedLocalPlayer, static_cast<LONG>(reinterpret_cast<DWORD_PTR>(localPlayer)));
                        const DWORD localPlayerValue = static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(localPlayer));
                        if (localPlayerValue != lastLocalPlayer)
                        {
                            lastLocalPlayer = localPlayerValue;
                            localPlayerAliveKnown = false;
                            InterlockedExchange(&g_consecutiveLocalPlayerAliveSamples, 0);
                            InterlockedExchange(&g_sustainedLocalPlayerAliveObserved, 0);
                            AppendLog(L"Player-manager confirmed local BFPlayer=%p from manager+0x54; this direct field read matches getter target 0x004A1260 and did not invoke game code.", localPlayer);
                        }

                        const bool localPlayerAlive = *reinterpret_cast<const BYTE*>(reinterpret_cast<const std::byte*>(localPlayer) + kBFPlayerIsAliveOffset) != 0;
                        if (!localPlayerAliveKnown || localPlayerAlive != lastLocalPlayerAlive)
                        {
                            localPlayerAliveKnown = true;
                            lastLocalPlayerAlive = localPlayerAlive;
                            AppendLog(L"Passive local BFPlayer isAlive read at +0x%zX is %d; no manager or player method was invoked.", kBFPlayerIsAliveOffset, localPlayerAlive ? 1 : 0);
                        }
                        if (localPlayerAlive)
                        {
                            const LONG consecutiveAliveSamples = InterlockedIncrement(&g_consecutiveLocalPlayerAliveSamples);
                            if (consecutiveAliveSamples == kSustainedLocalPlayerAliveSamplesRequired &&
                                InterlockedCompareExchange(&g_sustainedLocalPlayerAliveObserved, 1, 0) == 0)
                            {
                                AppendLog(L"Local BFPlayer isAlive remained non-zero for %ld consecutive 4 Hz samples; the D3D8 one-frame inventory may now arm on the confirmed device thread.", kSustainedLocalPlayerAliveSamplesRequired);
                            }
                        }
                        else
                        {
                            InterlockedExchange(&g_consecutiveLocalPlayerAliveSamples, 0);
                            InterlockedExchange(&g_sustainedLocalPlayerAliveObserved, 0);
                        }

                        const void* const cameraInterface = *reinterpret_cast<void* const*>(reinterpret_cast<const std::byte*>(localPlayer) + kBFPlayerCameraOffset);
                        if (cameraInterface != nullptr)
                        {
                            const DWORD cameraValue = static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(cameraInterface));
                            auto** const cameraVtable = *reinterpret_cast<void***>(const_cast<void*>(cameraInterface));
                            const void* const expectedAddRef = gameImage == nullptr ? nullptr : gameImage + kLocalCameraAddRefRva;
                            if (cameraVtable != nullptr && cameraVtable[0] == expectedAddRef)
                            {
                                InterlockedExchange(&g_profiledCameraInterfaceObserved, 1);
                                if (cameraValue != lastCamera)
                                {
                                    lastCamera = cameraValue;
                                    AppendLog(L"Local BFPlayer +0x68 camera interface=%p matches the profiled world::Camera interface (vtable[0]=%p); a candidate matrix is sampled by direct read at interface+0x%zX (root+0x1B4).", cameraInterface, cameraVtable[0], kLocalCameraActiveTransformOffset);
                                }

                                float transform[16] = {};
                                std::memcpy(transform, reinterpret_cast<const std::byte*>(cameraInterface) + kLocalCameraActiveTransformOffset, sizeof(transform));
                                bool hasNonZeroTransformValue = false;
                                for (const float value : transform)
                                {
                                    if (value != 0.0f)
                                    {
                                        hasNonZeroTransformValue = true;
                                        break;
                                    }
                                }
                                if (hasNonZeroTransformValue && InterlockedCompareExchange(&g_activeCameraTransformObserved, 1, 0) == 0)
                                {
                                    AppendLog(L"Local world::Camera candidate matrix is non-zero; map-gated combined D3D8 trace may now arm on the confirmed device thread.");
                                }
                                if (hasNonZeroTransformValue)
                                {
                                    const LONG consecutiveNonZeroSamples = InterlockedIncrement(&g_consecutiveNonZeroCameraTransformSamples);
                                    if (consecutiveNonZeroSamples == kSustainedNonZeroCameraTransformSamplesRequired &&
                                        InterlockedCompareExchange(&g_sustainedActiveCameraTransformObserved, 1, 0) == 0)
                                    {
                                        AppendLog(L"Local world::Camera candidate matrix remained non-zero for %ld consecutive 4 Hz samples; the D3D8 one-frame inventory may now arm on the confirmed device thread.", kSustainedNonZeroCameraTransformSamplesRequired);
                                    }
                                }
                                else
                                {
                                    InterlockedExchange(&g_consecutiveNonZeroCameraTransformSamples, 0);
                                }

                                if ((attempt % kLocalCameraTransformSamplePeriod) == 0 && transformSamples < kLocalCameraTransformMaximumSamples)
                                {
                                    ++transformSamples;
                                    AppendLog(
                                        L"Local world::Camera candidate-matrix sample %ld at 4 Hz probe iteration %lu interface=%p m=[%.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f].",
                                        transformSamples,
                                        static_cast<unsigned long>(attempt),
                                        cameraInterface,
                                        static_cast<double>(transform[0]), static_cast<double>(transform[1]), static_cast<double>(transform[2]), static_cast<double>(transform[3]),
                                        static_cast<double>(transform[4]), static_cast<double>(transform[5]), static_cast<double>(transform[6]), static_cast<double>(transform[7]),
                                        static_cast<double>(transform[8]), static_cast<double>(transform[9]), static_cast<double>(transform[10]), static_cast<double>(transform[11]),
                                        static_cast<double>(transform[12]), static_cast<double>(transform[13]), static_cast<double>(transform[14]), static_cast<double>(transform[15]));
                                }
                            }
                            else if (!unexpectedCameraInterfaceReported)
                            {
                                unexpectedCameraInterfaceReported = true;
                                AppendLog(L"Local BFPlayer +0x68 interface=%p has vtable[0]=%p instead of the profiled world::Camera target=%p; no transform was read.", cameraInterface, cameraVtable == nullptr ? nullptr : cameraVtable[0], expectedAddRef);
                            }
                        }
                    }
                    else
                    {
                        InterlockedExchange(&g_observedLocalPlayer, 0);
                        InterlockedExchange(&g_consecutiveLocalPlayerAliveSamples, 0);
                        InterlockedExchange(&g_sustainedLocalPlayerAliveObserved, 0);
                    }
                }
            }
            else
            {
                InterlockedExchange(&g_observedLocalPlayer, 0);
                InterlockedExchange(&g_consecutiveLocalPlayerAliveSamples, 0);
                InterlockedExchange(&g_sustainedLocalPlayerAliveObserved, 0);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            AppendLog(L"Player-manager metadata probe encountered an unreadable transient pointer; ending without a result.");
            return 0;
        }
        Sleep(250);
    }

    AppendLog(L"Player-manager/camera sampler ended after its 90-second read-only window: localPlayer=%08lX sustainedLocalAlive=%ld candidateMatrixSamples=%ld. It never invoked game code, used no breakpoint, and wrote no game memory.", static_cast<unsigned long>(lastLocalPlayer), InterlockedCompareExchange(&g_sustainedLocalPlayerAliveObserved, 0, 0), transformSamples);
    return 0;
}

void StartLocalPlayerManagerProbe()
{
    if (InterlockedCompareExchange(&g_localPlayerManagerProbeStarted, 1, 0) != 0)
    {
        return;
    }

    HANDLE worker = CreateThread(nullptr, 0, ProbeLocalPlayerManager, nullptr, 0, nullptr);
    if (worker == nullptr)
    {
        AppendLog(L"Player-manager metadata probe could not start (error %lu); no game state was changed.", GetLastError());
        return;
    }
    CloseHandle(worker);
    AppendLog(L"Started read-only player-manager metadata probe; it will not invoke manager methods or set breakpoints.");
}

DWORD WINAPI ReportCreateDeviceBreakpoint(LPVOID)
{
    constexpr DWORD kD3D8LifecycleObservationWindowMs = 90000;
    const DWORD startedAt = GetTickCount();
    while (GetTickCount() - startedAt < kD3D8LifecycleObservationWindowMs)
    {
        if (InterlockedCompareExchange(&g_createDeviceBreakpoint.pending, 0, 0) != 0)
        {
            if (g_createDeviceBreakpoint.stackReadable)
            {
                AppendLog(
                    L"CreateDevice hardware-breakpoint entry target=%p this=%08lX adapter=%lu type=%lu hwnd=%08lX behavior=0x%08lX parameters=%08lX returnedDevice=%08lX",
                    g_createDeviceBreakpoint.target,
                    static_cast<unsigned long>(g_createDeviceBreakpoint.direct3D),
                    static_cast<unsigned long>(g_createDeviceBreakpoint.adapter),
                    static_cast<unsigned long>(g_createDeviceBreakpoint.deviceType),
                    static_cast<unsigned long>(g_createDeviceBreakpoint.focusWindow),
                    static_cast<unsigned long>(g_createDeviceBreakpoint.behaviorFlags),
                    static_cast<unsigned long>(g_createDeviceBreakpoint.presentationParameters),
                    static_cast<unsigned long>(g_createDeviceBreakpoint.returnedDevice));
                if (g_createDeviceBreakpoint.presentationReadable)
                {
                    const D3DPresentParameters& parameters = g_createDeviceBreakpoint.presentation;
                    AppendLog(
                        L"CreateDevice presentation size=%lux%lu format=%lu windowed=%d swap=%lu depth=%d depthFormat=%lu interval=0x%08lX",
                        static_cast<unsigned long>(parameters.backBufferWidth),
                        static_cast<unsigned long>(parameters.backBufferHeight),
                        static_cast<unsigned long>(parameters.backBufferFormat),
                        parameters.windowed,
                        static_cast<unsigned long>(parameters.swapEffect),
                        parameters.enableAutoDepthStencil,
                        static_cast<unsigned long>(parameters.autoDepthStencilFormat),
                        static_cast<unsigned long>(parameters.fullScreenPresentationInterval));
                }
                if (g_createDeviceBreakpoint.presentObserved)
                {
                    AppendLog(
                        L"D3D8 device=%p resetTarget=%p presentTarget=%p clearTarget=%p beginSceneTarget=%p endSceneTarget=%p; first Present entry observed.",
                        g_createDeviceBreakpoint.device,
                        g_createDeviceBreakpoint.resetTarget,
                        g_createDeviceBreakpoint.presentTarget,
                        g_createDeviceBreakpoint.clearTarget,
                        g_createDeviceBreakpoint.beginSceneTarget,
                        g_createDeviceBreakpoint.endSceneTarget);
                    AppendModuleForAddress(L"CreateDevice", g_createDeviceBreakpoint.target);
                    AppendModuleForAddress(L"Reset", g_createDeviceBreakpoint.resetTarget);
                    AppendModuleForAddress(L"Present", g_createDeviceBreakpoint.presentTarget);
                    AppendModuleForAddress(L"Clear", g_createDeviceBreakpoint.clearTarget);
                    AppendModuleForAddress(L"BeginScene", g_createDeviceBreakpoint.beginSceneTarget);
                    AppendModuleForAddress(L"EndScene", g_createDeviceBreakpoint.endSceneTarget);
                    AppendModuleForAddress(L"SetRenderTarget", g_createDeviceBreakpoint.setRenderTargetTarget);
                    AppendModuleForAddress(L"SetTransform", g_createDeviceBreakpoint.setTransformTarget);
                    AppendLog(
                        L"D3D8 startup-pass entries: Clear=%d sequence=%ld; BeginScene=%d sequence=%ld; EndScene=%d sequence=%ld. Each target was observed at most once through a hardware execution breakpoint.",
                        g_createDeviceBreakpoint.clearObserved,
                        InterlockedCompareExchange(&g_createDeviceBreakpoint.clearSequence, 0, 0),
                        g_createDeviceBreakpoint.beginSceneObserved,
                        InterlockedCompareExchange(&g_createDeviceBreakpoint.beginSceneSequence, 0, 0),
                        g_createDeviceBreakpoint.endSceneObserved,
                        InterlockedCompareExchange(&g_createDeviceBreakpoint.endSceneSequence, 0, 0));
                    AppendLog(
                        L"D3D8 post-Reset entries: SetRenderTarget=%d sequence=%ld; SetTransform=%d sequence=%ld; BeginScene=%d sequence=%ld. Each target was observed at most once through a hardware execution breakpoint.",
                        g_createDeviceBreakpoint.setRenderTargetObserved,
                        InterlockedCompareExchange(&g_createDeviceBreakpoint.setRenderTargetSequence, 0, 0),
                        g_createDeviceBreakpoint.setTransformObserved,
                        InterlockedCompareExchange(&g_createDeviceBreakpoint.setTransformSequence, 0, 0),
                        g_createDeviceBreakpoint.beginSceneObserved,
                        InterlockedCompareExchange(&g_createDeviceBreakpoint.beginSceneSequence, 0, 0));
                    if (g_createDeviceBreakpoint.postResetPresentObserved)
                    {
                        AppendLog(
                            L"D3D8 post-Reset Present entry was observed on the device-creation thread %lu at sequence=%ld.",
                            g_createDeviceBreakpoint.postResetPresentThreadId,
                            InterlockedCompareExchange(&g_createDeviceBreakpoint.postResetPresentSequence, 0, 0));
                    }
                }
                else
                {
                    AppendLog(L"CreateDevice return was observed, but the D3D8 device/Present boundary could not be armed safely.");
                }
                if (g_createDeviceBreakpoint.resetObserved)
                {
                    if (g_createDeviceBreakpoint.resetStackReadable)
                    {
                        AppendLog(
                            L"D3D8 Reset hardware-breakpoint entry target=%p this=%08lX parameters=%08lX.",
                            g_createDeviceBreakpoint.resetTarget,
                            static_cast<unsigned long>(g_createDeviceBreakpoint.resetDevice),
                            static_cast<unsigned long>(g_createDeviceBreakpoint.resetPresentationParameters));
                        if (g_createDeviceBreakpoint.resetPresentationReadable)
                        {
                            const D3DPresentParameters& parameters = g_createDeviceBreakpoint.resetPresentation;
                            AppendLog(
                                L"D3D8 Reset presentation size=%lux%lu format=%lu windowed=%d swap=%lu depth=%d depthFormat=%lu interval=0x%08lX.",
                                static_cast<unsigned long>(parameters.backBufferWidth),
                                static_cast<unsigned long>(parameters.backBufferHeight),
                                static_cast<unsigned long>(parameters.backBufferFormat),
                                parameters.windowed,
                                static_cast<unsigned long>(parameters.swapEffect),
                                parameters.enableAutoDepthStencil,
                                static_cast<unsigned long>(parameters.autoDepthStencilFormat),
                                static_cast<unsigned long>(parameters.fullScreenPresentationInterval));
                        }
                    }
                    else
                    {
                        AppendLog(L"D3D8 Reset hardware breakpoint hit target=%p, but its stack could not be read.", g_createDeviceBreakpoint.resetTarget);
                    }
                }
            }
            else
            {
                AppendLog(L"CreateDevice hardware breakpoint hit target=%p, but its stack could not be read.", g_createDeviceBreakpoint.target);
            }
            return 0;
        }
        Sleep(10);
    }

    if (InterlockedCompareExchange(&g_combinedFrameTrace.state, 0, 0) != 0)
    {
        AppendLog(L"Early one-shot D3D8 lifecycle trace handed off to the map-gated combined trace before its 90-second report window ended.");
    }
    else if (g_createDeviceBreakpoint.resetObserved &&
        (!g_createDeviceBreakpoint.setRenderTargetObserved ||
         !g_createDeviceBreakpoint.setTransformObserved ||
         !g_createDeviceBreakpoint.beginSceneObserved))
    {
        AppendLog(
            L"D3D8 startup pass snapshot before Reset: device=%p Clear=%d sequence=%ld target=%p; BeginScene=%d sequence=%ld target=%p; EndScene=%d sequence=%ld target=%p; Present=%d target=%p; Reset=%d target=%p.",
            g_createDeviceBreakpoint.device,
            g_createDeviceBreakpoint.clearObserved,
            InterlockedCompareExchange(&g_createDeviceBreakpoint.clearSequence, 0, 0),
            g_createDeviceBreakpoint.clearTarget,
            g_createDeviceBreakpoint.beginSceneObserved,
            InterlockedCompareExchange(&g_createDeviceBreakpoint.beginSceneSequence, 0, 0),
            g_createDeviceBreakpoint.beginSceneTarget,
            g_createDeviceBreakpoint.endSceneObserved,
            InterlockedCompareExchange(&g_createDeviceBreakpoint.endSceneSequence, 0, 0),
            g_createDeviceBreakpoint.endSceneTarget,
            g_createDeviceBreakpoint.presentObserved,
            g_createDeviceBreakpoint.presentTarget,
            g_createDeviceBreakpoint.resetObserved,
            g_createDeviceBreakpoint.resetTarget);
        AppendLog(
            L"D3D8 post-Reset snapshot: SetRenderTarget=%d sequence=%ld target=%p; SetTransform=%d sequence=%ld target=%p; BeginScene=%d sequence=%ld target=%p.",
            g_createDeviceBreakpoint.setRenderTargetObserved,
            InterlockedCompareExchange(&g_createDeviceBreakpoint.setRenderTargetSequence, 0, 0),
            g_createDeviceBreakpoint.setRenderTargetTarget,
            g_createDeviceBreakpoint.setTransformObserved,
            InterlockedCompareExchange(&g_createDeviceBreakpoint.setTransformSequence, 0, 0),
            g_createDeviceBreakpoint.setTransformTarget,
            g_createDeviceBreakpoint.beginSceneObserved,
            InterlockedCompareExchange(&g_createDeviceBreakpoint.beginSceneSequence, 0, 0),
            g_createDeviceBreakpoint.beginSceneTarget);
        AppendLog(
            L"D3D8 post-Reset Present on the device-creation thread: observed=%d sequence=%ld thread=%lu target=%p.",
            g_createDeviceBreakpoint.postResetPresentObserved,
            InterlockedCompareExchange(&g_createDeviceBreakpoint.postResetPresentSequence, 0, 0),
            g_createDeviceBreakpoint.postResetPresentThreadId,
            g_createDeviceBreakpoint.presentTarget);
        AppendLog(L"D3D8 Reset and the startup pass snapshot were observed, but one or more post-Reset targets were not called during the remaining 90-second observation window.");
    }
    else if (g_createDeviceBreakpoint.presentObserved)
    {
        AppendLog(L"D3D8 first Present was observed and a one-shot Reset breakpoint stayed armed for 90 seconds, but Reset was not called during that window.");
    }
    else
    {
        AppendLog(L"CreateDevice hardware breakpoint did not trigger during the 90-second D3D8 lifecycle observation window.");
    }
    return 0;
}

void ArmCreateDeviceHardwareBreakpoint(void* direct3D)
{
    if (direct3D == nullptr)
    {
        return;
    }
    if (IsDebuggerPresent())
    {
        AppendLog(L"CreateDevice hardware breakpoint skipped because a debugger is attached.");
        return;
    }

    auto** vtable = *reinterpret_cast<void***>(direct3D);
    if (vtable == nullptr || vtable[kDirect3D8CreateDeviceSlot] == nullptr)
    {
        AppendLog(L"CreateDevice hardware breakpoint skipped because the D3D8 vtable is unavailable.");
        return;
    }

    CONTEXT context = {};
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (!GetThreadContext(GetCurrentThread(), &context))
    {
        AppendLog(L"CreateDevice hardware breakpoint skipped: GetThreadContext failed (%lu).", GetLastError());
        return;
    }
    if ((context.Dr7 & 0xFF) != 0)
    {
        AppendLog(L"CreateDevice hardware breakpoint skipped to preserve existing debug-register state (DR7=%p).", reinterpret_cast<void*>(context.Dr7));
        return;
    }

    if (g_createDeviceBreakpointHandler == nullptr)
    {
        g_createDeviceBreakpointHandler = AddVectoredExceptionHandler(1, HandleCreateDeviceBreakpoint);
        if (g_createDeviceBreakpointHandler == nullptr)
        {
            AppendLog(L"CreateDevice hardware breakpoint skipped: AddVectoredExceptionHandler failed (%lu).", GetLastError());
            return;
        }
    }

    g_createDeviceBreakpoint = {};
    g_createDeviceBreakpoint.threadId = GetCurrentThreadId();
    g_createDeviceBreakpoint.target = vtable[kDirect3D8CreateDeviceSlot];

    context.Dr0 = reinterpret_cast<DWORD_PTR>(g_createDeviceBreakpoint.target);
    context.Dr6 = 0;
    context.Dr7 = (context.Dr7 & ~static_cast<DWORD_PTR>(0xF)) | 1;
    if (!SetThreadContext(GetCurrentThread(), &context))
    {
        AppendLog(L"CreateDevice hardware breakpoint skipped: SetThreadContext failed (%lu).", GetLastError());
        return;
    }

    InterlockedExchange(&g_createDeviceBreakpoint.stage, 1);
    HANDLE reporter = CreateThread(nullptr, 0, ReportCreateDeviceBreakpoint, nullptr, 0, nullptr);
    if (reporter != nullptr)
    {
        CloseHandle(reporter);
    }
    AppendLog(L"Armed one-shot CreateDevice hardware breakpoint at %p on thread %lu.", g_createDeviceBreakpoint.target, g_createDeviceBreakpoint.threadId);
}

constexpr LONG kCameraSetterMaximumSamples = 128;
constexpr LONG kVehicleCameraTransitionMaximumSamples = 128;

struct CameraSetterSample
{
    DWORD player = 0;
    DWORD camera = 0;
    BOOL stackReadable = FALSE;
};

struct CameraSetterBreakpointRecord
{
    volatile LONG reservedSamples = 0;
    volatile LONG capturedSamples = 0;
    DWORD threadId = 0;
    void* setterTarget = nullptr;
    void* vehicleTransitionTarget = nullptr;
    CameraSetterSample samples[kCameraSetterMaximumSamples] = {};
    struct VehicleCameraTransitionSample
    {
        DWORD player = 0;
        DWORD vehicleArgument = 0;
        DWORD inputIdArgument = 0;
        DWORD vehicleField = 0;
        DWORD cameraField = 0;
        DWORD inputIdField = 0;
        BOOL entryReadable = FALSE;
        BOOL returnReadable = FALSE;
    } vehicleTransitions[kVehicleCameraTransitionMaximumSamples] = {};
    volatile LONG reservedVehicleTransitions = 0;
    volatile LONG capturedVehicleTransitions = 0;
    volatile LONG vehicleTransitionStage = 0;
    LONG pendingVehicleTransition = -1;
    DWORD pendingVehicleTransitionReturnAddress = 0;
    volatile LONG stopRequested = 0;
};

CameraSetterBreakpointRecord g_cameraSetterBreakpoint = {};
PVOID g_cameraSetterBreakpointHandler = nullptr;

LONG CALLBACK HandleCameraSetterBreakpoint(EXCEPTION_POINTERS* exceptionPointers)
{
    if (exceptionPointers == nullptr || exceptionPointers->ExceptionRecord == nullptr || exceptionPointers->ContextRecord == nullptr)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    CONTEXT* context = exceptionPointers->ContextRecord;
    if (exceptionPointers->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP ||
        GetCurrentThreadId() != g_cameraSetterBreakpoint.threadId)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    const LONG vehicleTransitionStage = InterlockedCompareExchange(&g_cameraSetterBreakpoint.vehicleTransitionStage, 0, 0);
    const bool setterHit = context->Eip == reinterpret_cast<DWORD_PTR>(g_cameraSetterBreakpoint.setterTarget);
    const bool vehicleTransitionEntryHit = vehicleTransitionStage == 1 &&
        context->Eip == reinterpret_cast<DWORD_PTR>(g_cameraSetterBreakpoint.vehicleTransitionTarget);
    const bool vehicleTransitionReturnHit = vehicleTransitionStage == 2 &&
        context->Eip == g_cameraSetterBreakpoint.pendingVehicleTransitionReturnAddress;
    if (!setterHit && !vehicleTransitionEntryHit && !vehicleTransitionReturnHit)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    context->Dr6 = 0;
    if (InterlockedCompareExchange(&g_cameraSetterBreakpoint.stopRequested, 0, 0) != 0)
    {
        context->Dr0 = 0;
        context->Dr1 = 0;
        context->Dr7 &= ~static_cast<DWORD_PTR>(0x5);
        InterlockedExchange(&g_cameraSetterBreakpoint.vehicleTransitionStage, 0);
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    if (setterHit)
    {
        const LONG sampleIndex = InterlockedIncrement(&g_cameraSetterBreakpoint.reservedSamples) - 1;
        if (sampleIndex >= kCameraSetterMaximumSamples)
        {
            context->Dr0 = 0;
            context->Dr7 &= ~static_cast<DWORD_PTR>(1);
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        const DWORD* stack = reinterpret_cast<const DWORD*>(context->Esp);
        CameraSetterSample& sample = g_cameraSetterBreakpoint.samples[sampleIndex];
        __try
        {
            sample.player = context->Ecx;
            sample.camera = stack[1];
            sample.stackReadable = TRUE;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            sample.stackReadable = FALSE;
        }
        InterlockedIncrement(&g_cameraSetterBreakpoint.capturedSamples);
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    if (vehicleTransitionEntryHit)
    {
        const LONG sampleIndex = InterlockedIncrement(&g_cameraSetterBreakpoint.reservedVehicleTransitions) - 1;
        if (sampleIndex >= kVehicleCameraTransitionMaximumSamples)
        {
            context->Dr1 = 0;
            context->Dr7 &= ~static_cast<DWORD_PTR>(4);
            InterlockedExchange(&g_cameraSetterBreakpoint.vehicleTransitionStage, 0);
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        const DWORD* stack = reinterpret_cast<const DWORD*>(context->Esp);
        auto& sample = g_cameraSetterBreakpoint.vehicleTransitions[sampleIndex];
        __try
        {
            sample.player = context->Ecx;
            sample.vehicleArgument = stack[1];
            sample.inputIdArgument = stack[2];
            sample.entryReadable = TRUE;
            g_cameraSetterBreakpoint.pendingVehicleTransition = sampleIndex;
            g_cameraSetterBreakpoint.pendingVehicleTransitionReturnAddress = stack[0];
            context->Dr1 = g_cameraSetterBreakpoint.pendingVehicleTransitionReturnAddress;
            InterlockedExchange(&g_cameraSetterBreakpoint.vehicleTransitionStage, 2);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            sample.entryReadable = FALSE;
            InterlockedIncrement(&g_cameraSetterBreakpoint.capturedVehicleTransitions);
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    const LONG sampleIndex = g_cameraSetterBreakpoint.pendingVehicleTransition;
    if (sampleIndex >= 0 && sampleIndex < kVehicleCameraTransitionMaximumSamples)
    {
        auto& sample = g_cameraSetterBreakpoint.vehicleTransitions[sampleIndex];
        __try
        {
            sample.vehicleField = *reinterpret_cast<const DWORD*>(sample.player + 0x64);
            sample.cameraField = *reinterpret_cast<const DWORD*>(sample.player + 0x68);
            sample.inputIdField = *reinterpret_cast<const DWORD*>(sample.player + 0x6c);
            sample.returnReadable = TRUE;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            sample.returnReadable = FALSE;
        }
        InterlockedIncrement(&g_cameraSetterBreakpoint.capturedVehicleTransitions);
    }

    g_cameraSetterBreakpoint.pendingVehicleTransition = -1;
    g_cameraSetterBreakpoint.pendingVehicleTransitionReturnAddress = 0;
    if (InterlockedCompareExchange(&g_cameraSetterBreakpoint.reservedVehicleTransitions, 0, 0) >= kVehicleCameraTransitionMaximumSamples)
    {
        context->Dr1 = 0;
        context->Dr7 &= ~static_cast<DWORD_PTR>(4);
        InterlockedExchange(&g_cameraSetterBreakpoint.vehicleTransitionStage, 0);
    }
    else
    {
        context->Dr1 = reinterpret_cast<DWORD_PTR>(g_cameraSetterBreakpoint.vehicleTransitionTarget);
        InterlockedExchange(&g_cameraSetterBreakpoint.vehicleTransitionStage, 1);
    }

    return EXCEPTION_CONTINUE_EXECUTION;
}

DWORD WINAPI ReportCameraSetterBreakpoint(LPVOID)
{
    LONG reportedSamples = 0;
    LONG reportedVehicleTransitions = 0;
    for (DWORD attempt = 0; attempt < 9000; ++attempt)
    {
        const LONG capturedSamples = InterlockedCompareExchange(&g_cameraSetterBreakpoint.capturedSamples, 0, 0);
        while (reportedSamples < capturedSamples)
        {
            const CameraSetterSample& sample = g_cameraSetterBreakpoint.samples[reportedSamples];
            const LONG sampleNumber = reportedSamples + 1;
            if (!sample.stackReadable)
            {
                AppendLog(L"BFPlayer +0x68 setter sample %ld hit target=%p, but its stack could not be read.", sampleNumber, g_cameraSetterBreakpoint.setterTarget);
            }
            else
            {
                AppendLog(
                    L"BFPlayer +0x68 setter sample %ld target=%p player=%08lX candidateCamera=%08lX.",
                    sampleNumber,
                    g_cameraSetterBreakpoint.setterTarget,
                    static_cast<unsigned long>(sample.player),
                    static_cast<unsigned long>(sample.camera));
                if (sample.camera != 0)
                {
                    __try
                    {
                        auto** cameraVtable = *reinterpret_cast<void***>(sample.camera);
                        if (cameraVtable != nullptr)
                        {
                            AppendModuleForAddress(L"BFPlayer +0x68 candidate vtable[0]", cameraVtable[0]);
                        }
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER)
                    {
                        AppendLog(L"BFPlayer +0x68 candidateCamera=%08lX vtable could not be read.", static_cast<unsigned long>(sample.camera));
                    }
                }
            }
            ++reportedSamples;
        }
        const LONG capturedVehicleTransitions = InterlockedCompareExchange(&g_cameraSetterBreakpoint.capturedVehicleTransitions, 0, 0);
        while (reportedVehicleTransitions < capturedVehicleTransitions)
        {
            const auto& sample = g_cameraSetterBreakpoint.vehicleTransitions[reportedVehicleTransitions];
            const LONG sampleNumber = reportedVehicleTransitions + 1;
            if (!sample.entryReadable)
            {
                AppendLog(L"BFPlayer vehicle/camera transaction sample %ld hit target=%p, but its entry stack could not be read.", sampleNumber, g_cameraSetterBreakpoint.vehicleTransitionTarget);
            }
            else if (!sample.returnReadable)
            {
                AppendLog(L"BFPlayer vehicle/camera transaction sample %ld player=%08lX vehicleArg=%08lX inputIdArg=%08lX, but its post-return fields could not be read.", sampleNumber, static_cast<unsigned long>(sample.player), static_cast<unsigned long>(sample.vehicleArgument), static_cast<unsigned long>(sample.inputIdArgument));
            }
            else
            {
                const DWORD observedLocalPlayer = static_cast<DWORD>(InterlockedCompareExchange(&g_observedLocalPlayer, 0, 0));
                AppendLog(L"BFPlayer vehicle/camera transaction sample %ld player=%08lX vehicleArg=%08lX inputIdArg=%08lX postVehicle=%08lX postCamera=%08lX postInputId=%08lX observedLocal=%08lX.", sampleNumber, static_cast<unsigned long>(sample.player), static_cast<unsigned long>(sample.vehicleArgument), static_cast<unsigned long>(sample.inputIdArgument), static_cast<unsigned long>(sample.vehicleField), static_cast<unsigned long>(sample.cameraField), static_cast<unsigned long>(sample.inputIdField), static_cast<unsigned long>(observedLocalPlayer));
                if (observedLocalPlayer != 0 && sample.player == observedLocalPlayer)
                {
                    AppendLog(L"BFPlayer vehicle/camera transaction sample %ld belongs to the observed local player.", sampleNumber);
                }
                if (sample.cameraField != 0)
                {
                    __try
                    {
                        auto** cameraVtable = *reinterpret_cast<void***>(sample.cameraField);
                        if (cameraVtable != nullptr)
                        {
                            AppendModuleForAddress(L"BFPlayer vehicle/camera postCamera vtable[0]", cameraVtable[0]);
                            if (observedLocalPlayer != 0 && sample.player == observedLocalPlayer && cameraVtable[16] != nullptr)
                            {
                                AppendLog(L"BFPlayer local +0x68 vtable+0x40 target=%p; this is metadata for the method used by FUN_00407ec0 and was not invoked.", cameraVtable[16]);
                                AppendModuleForAddress(L"BFPlayer local +0x68 vtable+0x40", cameraVtable[16]);
                            }
                        }
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER)
                    {
                        AppendLog(L"BFPlayer vehicle/camera postCamera=%08lX vtable could not be read.", static_cast<unsigned long>(sample.cameraField));
                    }
                }
            }
            ++reportedVehicleTransitions;
        }
        Sleep(10);
    }

    InterlockedExchange(&g_cameraSetterBreakpoint.stopRequested, 1);
    AppendLog(L"BFPlayer camera passive observation window ended after 90 seconds with %ld setter samples and %ld vehicle/camera transactions; the next observed breakpoint clears both debug registers.", reportedSamples, reportedVehicleTransitions);
    return 0;
}

void ArmCameraObservationBreakpoints()
{
    if (IsDebuggerPresent())
    {
        AppendLog(L"BFPlayer camera observation breakpoints skipped because a debugger is attached.");
        return;
    }

    CONTEXT context = {};
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (!GetThreadContext(GetCurrentThread(), &context))
    {
        AppendLog(L"BFPlayer camera observation breakpoints skipped: GetThreadContext failed (%lu).", GetLastError());
        return;
    }
    if ((context.Dr7 & 0xFF) != 0)
    {
        AppendLog(L"BFPlayer camera observation breakpoints skipped to preserve existing debug-register state (DR7=%p).", reinterpret_cast<void*>(context.Dr7));
        return;
    }

    if (g_cameraSetterBreakpointHandler == nullptr)
    {
        g_cameraSetterBreakpointHandler = AddVectoredExceptionHandler(1, HandleCameraSetterBreakpoint);
        if (g_cameraSetterBreakpointHandler == nullptr)
        {
            AppendLog(L"BFPlayer camera observation breakpoints skipped: AddVectoredExceptionHandler failed (%lu).", GetLastError());
            return;
        }
    }

    const auto* gameImage = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
    if (gameImage == nullptr)
    {
        return;
    }

    g_cameraSetterBreakpoint = {};
    g_cameraSetterBreakpoint.threadId = GetCurrentThreadId();
    g_cameraSetterBreakpoint.setterTarget = const_cast<std::byte*>(gameImage) + 0x9480;
    g_cameraSetterBreakpoint.vehicleTransitionTarget = const_cast<std::byte*>(gameImage) + 0x8010;
    context.Dr0 = reinterpret_cast<DWORD_PTR>(g_cameraSetterBreakpoint.setterTarget);
    context.Dr1 = reinterpret_cast<DWORD_PTR>(g_cameraSetterBreakpoint.vehicleTransitionTarget);
    context.Dr6 = 0;
    context.Dr7 = (context.Dr7 & ~static_cast<DWORD_PTR>(0xFF)) | 5;
    if (!SetThreadContext(GetCurrentThread(), &context))
    {
        AppendLog(L"BFPlayer camera observation breakpoints skipped: SetThreadContext failed (%lu).", GetLastError());
        return;
    }

    InterlockedExchange(&g_cameraSetterBreakpoint.vehicleTransitionStage, 1);
    HANDLE reporter = CreateThread(nullptr, 0, ReportCameraSetterBreakpoint, nullptr, 0, nullptr);
    if (reporter != nullptr)
    {
        CloseHandle(reporter);
    }
    AppendLog(L"Armed passive BFPlayer +0x68 setter breakpoint at %p and vehicle/camera transaction breakpoint at %p on thread %lu; retaining up to %ld samples of each for 90 seconds.", g_cameraSetterBreakpoint.setterTarget, g_cameraSetterBreakpoint.vehicleTransitionTarget, g_cameraSetterBreakpoint.threadId, kCameraSetterMaximumSamples);
}

HRESULT WINAPI HookCreateDevice(
    void* direct3D,
    UINT adapter,
    UINT deviceType,
    HWND focusWindow,
    DWORD behaviorFlags,
    D3DPresentParameters* presentationParameters,
    void** returnedDevice)
{
    if (g_originalCreateDevice == nullptr)
    {
        return E_FAIL;
    }

    const bfvr::BF1942FrameLimiterOverrideResult limiterBefore =
        bfvr::ApplyRequestedBF1942FrameLimiterOverride(
            GetModuleHandleW(nullptr));
    if (limiterBefore.status !=
            bfvr::BF1942FrameLimiterOverrideStatus::NotRequested &&
        limiterBefore.status !=
            bfvr::BF1942FrameLimiterOverrideStatus::AlreadyApplied)
    {
        AppendLog(
            L"BFVR-only renderer.lockFPS -1 before CreateDevice: %s (ownerSlot=%p value=%p previous=%.3f).",
            bfvr::DescribeBF1942FrameLimiterOverrideStatus(
                limiterBefore.status),
            reinterpret_cast<void*>(limiterBefore.ownerPointerAddress),
            reinterpret_cast<void*>(limiterBefore.valueAddress),
            static_cast<double>(limiterBefore.previousValue));
    }

    const HRESULT result = g_originalCreateDevice(
        direct3D,
        adapter,
        deviceType,
        focusWindow,
        behaviorFlags,
        presentationParameters,
        returnedDevice);

    const bfvr::BF1942FrameLimiterOverrideResult limiterAfter =
        bfvr::ApplyRequestedBF1942FrameLimiterOverride(
            GetModuleHandleW(nullptr));
    if (limiterAfter.status !=
            bfvr::BF1942FrameLimiterOverrideStatus::NotRequested &&
        limiterAfter.status !=
            bfvr::BF1942FrameLimiterOverrideStatus::AlreadyApplied)
    {
        AppendLog(
            L"BFVR-only renderer.lockFPS -1 after CreateDevice: %s (ownerSlot=%p value=%p previous=%.3f).",
            bfvr::DescribeBF1942FrameLimiterOverrideStatus(
                limiterAfter.status),
            reinterpret_cast<void*>(limiterAfter.ownerPointerAddress),
            reinterpret_cast<void*>(limiterAfter.valueAddress),
            static_cast<double>(limiterAfter.previousValue));
    }

    const void* device = returnedDevice == nullptr ? nullptr : *returnedDevice;
    if (presentationParameters == nullptr)
    {
        AppendLog(
            L"CreateDevice result=0x%08lX device=%p adapter=%u type=%u behavior=0x%08lX parameters=null",
            static_cast<unsigned long>(result),
            device,
            adapter,
            deviceType,
            static_cast<unsigned long>(behaviorFlags));
        return result;
    }

    AppendLog(
        L"CreateDevice result=0x%08lX device=%p adapter=%u type=%u behavior=0x%08lX size=%ux%u format=%u windowed=%d swap=%u depth=%d depthFormat=%u interval=0x%08lX",
        static_cast<unsigned long>(result),
        device,
        adapter,
        deviceType,
        static_cast<unsigned long>(behaviorFlags),
        presentationParameters->backBufferWidth,
        presentationParameters->backBufferHeight,
        presentationParameters->backBufferFormat,
        presentationParameters->windowed,
        presentationParameters->swapEffect,
        presentationParameters->enableAutoDepthStencil,
        presentationParameters->autoDepthStencilFormat,
        static_cast<unsigned long>(presentationParameters->fullScreenPresentationInterval));
    return result;
}

void HookCreateDeviceOnDirect3D8(void* direct3D)
{
    if (direct3D == nullptr)
    {
        return;
    }

    if (g_originalCreateDevice != nullptr)
    {
        AppendLog(L"Direct3DCreate8 returned a second interface at %p; retaining the first CreateDevice observer.", direct3D);
        return;
    }

    auto** originalVtable = *reinterpret_cast<void***>(direct3D);
    if (originalVtable == nullptr || originalVtable[kDirect3D8CreateDeviceSlot] == nullptr)
    {
        AppendLog(L"Direct3DCreate8 returned %p with an unusable vtable.", direct3D);
        return;
    }

    auto** clonedVtable = static_cast<void**>(VirtualAlloc(
        nullptr,
        sizeof(void*) * kDirect3D8VtableSlots,
        MEM_RESERVE | MEM_COMMIT,
        PAGE_READWRITE));
    if (clonedVtable == nullptr)
    {
        AppendLog(L"Unable to allocate Direct3D8 observer vtable for %p (error %lu).", direct3D, GetLastError());
        return;
    }

    std::memcpy(clonedVtable, originalVtable, sizeof(void*) * kDirect3D8VtableSlots);
    const auto originalCreateDevice = reinterpret_cast<CreateDeviceFn>(clonedVtable[kDirect3D8CreateDeviceSlot]);
    clonedVtable[kDirect3D8CreateDeviceSlot] = reinterpret_cast<void*>(&HookCreateDevice);

    DWORD previousProtection = 0;
    if (!VirtualProtect(direct3D, sizeof(void*), PAGE_READWRITE, &previousProtection))
    {
        AppendLog(L"Unable to update Direct3D8 interface %p (error %lu).", direct3D, GetLastError());
        VirtualFree(clonedVtable, 0, MEM_RELEASE);
        return;
    }

    if (*reinterpret_cast<void***>(direct3D) != originalVtable)
    {
        DWORD ignoredProtection = 0;
        VirtualProtect(direct3D, sizeof(void*), previousProtection, &ignoredProtection);
        VirtualFree(clonedVtable, 0, MEM_RELEASE);
        AppendLog(L"Direct3D8 interface %p changed before observer installation.", direct3D);
        return;
    }

    g_originalCreateDevice = originalCreateDevice;
    *reinterpret_cast<void***>(direct3D) = clonedVtable;

    DWORD ignoredProtection = 0;
    VirtualProtect(direct3D, sizeof(void*), previousProtection, &ignoredProtection);
    AppendLog(L"Installed CreateDevice observer on Direct3D8 interface %p.", direct3D);
}

void* WINAPI HookDirect3DCreate8(UINT sdkVersion)
{
    if (g_originalDirect3DCreate8 == nullptr)
    {
        return nullptr;
    }

    const bfvr::BF1942FrameLimiterOverrideResult limiterBefore =
        bfvr::ApplyRequestedBF1942FrameLimiterOverride(
            GetModuleHandleW(nullptr));
    if (limiterBefore.status !=
            bfvr::BF1942FrameLimiterOverrideStatus::NotRequested &&
        limiterBefore.status !=
            bfvr::BF1942FrameLimiterOverrideStatus::AlreadyApplied)
    {
        AppendLog(
            L"BFVR-only renderer.lockFPS -1 before Direct3DCreate8: %s (ownerSlot=%p value=%p previous=%.3f).",
            bfvr::DescribeBF1942FrameLimiterOverrideStatus(
                limiterBefore.status),
            reinterpret_cast<void*>(limiterBefore.ownerPointerAddress),
            reinterpret_cast<void*>(limiterBefore.valueAddress),
            static_cast<double>(limiterBefore.previousValue));
    }

    void* direct3D = g_originalDirect3DCreate8(sdkVersion);

    const bfvr::BF1942FrameLimiterOverrideResult limiterAfter =
        bfvr::ApplyRequestedBF1942FrameLimiterOverride(
            GetModuleHandleW(nullptr));
    if (limiterAfter.status !=
            bfvr::BF1942FrameLimiterOverrideStatus::NotRequested &&
        limiterAfter.status !=
            bfvr::BF1942FrameLimiterOverrideStatus::AlreadyApplied)
    {
        AppendLog(
            L"BFVR-only renderer.lockFPS -1 after Direct3DCreate8: %s (ownerSlot=%p value=%p previous=%.3f).",
            bfvr::DescribeBF1942FrameLimiterOverrideStatus(
                limiterAfter.status),
            reinterpret_cast<void*>(limiterAfter.ownerPointerAddress),
            reinterpret_cast<void*>(limiterAfter.valueAddress),
            static_cast<double>(limiterAfter.previousValue));
    }
    AppendLog(L"Direct3DCreate8 sdkVersion=%u returned=%p.", sdkVersion, direct3D);
    ArmCreateDeviceHardwareBreakpoint(direct3D);
    if constexpr (kEnableCameraTransactionBreakpoints)
    {
        ArmCameraObservationBreakpoints();
    }
    else
    {
        AppendLog(L"Camera transaction breakpoints are compiled but disabled; the candidate-matrix sampler uses only 4 Hz direct reads to avoid map-load exception overhead.");
    }
    if (bfvr::IsD3D8RuntimeDiagnosticsEnabled(
            bfvr::ReadD3D8RuntimeDiagnosticLevel()))
    {
        StartOpenXRBootstrap();
        StartLocalPlayerManagerProbe();
    }
    return direct3D;
}
