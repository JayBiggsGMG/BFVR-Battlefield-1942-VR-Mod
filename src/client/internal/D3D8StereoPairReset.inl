// Included inside D3D8StereoPairProbe.cpp's anonymous namespace after the
// presentation bridge and frame-resource helpers have been defined.

HRESULT WINAPI HookReset(void* device, void* presentationParameters)
{
    InterlockedIncrement(&g_record.activeCallbacks);
    g_vertexShaderIdentityResolver.ClearCache();
    const bool restartContinuousPresentation =
        IsPresentationMode() &&
        g_runUntilStopped &&
        device == g_record.device;
    if (IsFullFrameMode() &&
        device == g_record.device)
    {
        const LONG state = InterlockedCompareExchange(&g_record.state, 0, 0);
        if (state == 2 || state == 3 ||
            restartContinuousPresentation)
        {
            ReleaseFrameOwnedResources();
            InterlockedExchange(&g_frame.resetAborted, 1);
            InterlockedExchange(
                &g_record.state,
                restartContinuousPresentation ? 0 : 5);
        }
        if (restartContinuousPresentation)
        {
            g_presentationBridge.Shutdown();
        }
    }

    const HRESULT result = g_originalReset == nullptr
        ? E_FAIL
        : g_originalReset(device, presentationParameters);
    g_frame.resetResult = result;
    if (restartContinuousPresentation)
    {
        const bool restarted =
            SUCCEEDED(result) &&
            g_presentationBridge.Initialize(
                g_lifecycle.backBufferWidth,
                g_lifecycle.backBufferHeight,
                g_presentationConfiguration.worldRenderScale,
                g_offlinePresentation
                    ? bfvr::D3D8PresentationCompanion::OfflineTransport
                    : bfvr::D3D8PresentationCompanion::OpenXR,
                AppendPresentationLog);
        bfvr::d3d8probe::ResetStereoFrameRecordForResourceReuse(g_frame);
        ResetWorldCrosshairFrameTransforms();
        g_frame.resetResult = result;
        InterlockedExchange(&g_frame.resetAborted, 1);
        g_runtimeRenderRequest = {};
        g_runtimeFramePosePolicy = {};
        g_renderViewPoseHook.ClearPose();
        // The process-lifetime OpenXR LOCAL origin is constant. Reset replaces
        // D3D8 resources only; it must never rearm a HMD-derived camera or
        // weapon basis while the user is alive, dead, or choosing a spawn.
        g_frameUiPlacement = {};
        bfvr::stereo::ResetUiMenuAnchor(g_menuAnchorTracker);
        g_nativeMenuActive = false;
        bfvr::ClearActiveMenuWorldAnchor();
        g_presentationFramePublished = false;
        InterlockedExchange(&g_record.state, restarted ? 1 : 5);
        AppendLog(
            restarted
                ? L"Recreated the process-lifetime OpenXR transport after BF1942 Reset; startup/menu presentation will continue across spawn."
                : L"Failed to recreate the process-lifetime OpenXR transport after BF1942 Reset (Reset result=0x%08lX).",
            static_cast<unsigned long>(result));
    }
    InterlockedDecrement(&g_record.activeCallbacks);
    return result;
}
