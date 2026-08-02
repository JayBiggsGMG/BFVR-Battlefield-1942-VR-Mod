// Included inside D3D8StereoPairProbe.cpp's anonymous namespace after the
// frame-resource and shared-presentation helpers have been defined.

bool FinalizeFrameTargets(void* device)
{
    if (InterlockedCompareExchange(&g_frame.resourcesReady, 0, 0) == 0)
    {
        return false;
    }

    const bool gpuSharedTargets =
        IsPresentationMode() &&
        g_presentationBridge.UsesGpuSharedTargets();
    const bool analyzePixels =
        !gpuSharedTargets &&
        (!IsPresentationMode() ||
            g_presentationRun.presentedFrames == 0 ||
            (g_runtimeRenderRequest.sequence % 60) == 0 ||
            (!g_runUntilStopped &&
             GetTickCount() - g_presentationRun.startedAt >=
                 kPresentationDurationMs - 1000));
    if (g_worldCrosshairFrame.valid)
    {
        bfvr::D3D8WorldCrosshairRenderFrame crosshairFrame = {};
        crosshairFrame.device = device;
        crosshairFrame.colorTargets[0] = g_frame.ownedColor[0];
        crosshairFrame.colorTargets[1] = g_frame.ownedColor[1];
        crosshairFrame.depthTargets[0] = g_frame.ownedDepth[0];
        crosshairFrame.depthTargets[1] = g_frame.ownedDepth[1];
        crosshairFrame.viewport = IsPresentationMode()
            ? g_runtimeWorldViewport
            : D3DViewport{
                0,
                0,
                g_frame.colorDescription.width,
                g_frame.colorDescription.height,
                0.0F,
                1.0F};
        crosshairFrame.eyeViews[0] =
            g_worldCrosshairFrame.eyeViews[0];
        crosshairFrame.eyeViews[1] =
            g_worldCrosshairFrame.eyeViews[1];
        crosshairFrame.eyeProjections[0] =
            g_worldCrosshairFrame.eyeProjections[0];
        crosshairFrame.eyeProjections[1] =
            g_worldCrosshairFrame.eyeProjections[1];
        (void)bfvr::RenderD3D8WorldCrosshair(crosshairFrame);
    }
    if (IsPresentationMode())
    {
        g_presentationFramePublished =
            bfvr::d3d8probe::TransferStereoFrameToSharedPresentation(
                g_readbackApi,
                device,
                g_frame,
                kOwnedTargetClearColor,
                kMenuLayerClearColor,
                g_presentationBridge,
                g_runtimeRenderRequest,
                analyzePixels,
                g_frameUiPlacement);
    }
    else
    {
        const std::array<void*, 3> sources = {
            g_frame.ownedColor[0],
            g_frame.ownedColor[1],
            g_frame.menuColor};
        const std::array<D3DSurfaceDescription, 3> descriptions = {
            g_frame.colorDescription,
            g_frame.colorDescription,
            g_frame.menuColorDescription};
        const std::array<DWORD, 3> clearColors = {
            kOwnedTargetClearColor,
            kOwnedTargetClearColor,
            kMenuLayerClearColor};
        std::array<ReadbackResult*, 3> results = {
            &g_frame.readback[0],
            &g_frame.readback[1],
            &g_frame.menuReadback};
        const std::int64_t readbackStarted =
            bfvr::d3d8probe::ReadPerformanceCounter();
        for (std::size_t index = 0; index < results.size(); ++index)
        {
            *results[index] = bfvr::d3d8probe::ReadbackOwnedTarget(
                g_readbackApi,
                device,
                sources[index],
                descriptions[index],
                clearColors[index],
                nullptr,
                analyzePixels);
        }
        g_frame.readbackQpcTicks =
            bfvr::d3d8probe::ReadPerformanceCounter() - readbackStarted;
    }

    const auto readbackComplete = [](const ReadbackResult& result)
    {
        return IsPresentationMode()
            ? bfvr::d3d8probe::IsReadbackTransferComplete(result)
            : bfvr::d3d8probe::IsReadbackComplete(result);
    };
    const LONG mirroredDraws =
        InterlockedCompareExchange(&g_frame.mirroredDraws, 0, 0);
    const LONG worldEyeDraws =
        InterlockedCompareExchange(&g_frame.worldEyeDraws, 0, 0);
    const LONG menuLayerDraws =
        InterlockedCompareExchange(&g_frame.menuLayerDraws, 0, 0);
    const bool bothEyesHaveColor =
        gpuSharedTargets
        ? worldEyeDraws != 0
        : analyzePixels
        ? g_frame.readback[0].nonClearPixels != 0 &&
            g_frame.readback[1].nonClearPixels != 0
        : readbackComplete(g_frame.readback[0]) &&
            readbackComplete(g_frame.readback[1]);
    const bool hashesDiffer =
        gpuSharedTargets ||
        !analyzePixels ||
        g_frame.readback[0].hash != g_frame.readback[1].hash;
    const bool layerPartitionExact =
        mirroredDraws == worldEyeDraws + menuLayerDraws;
    const bool menuLayerHasContent =
        menuLayerDraws != 0 &&
        (gpuSharedTargets ||
            !analyzePixels ||
            g_frame.menuReadback.nonClearPixels != 0 ||
            g_frame.menuReadback.nonZeroAlphaPixels != 0) &&
        (gpuSharedTargets ||
            readbackComplete(g_frame.menuReadback));
    const bfvr::stereo::D3D8FrameCompletionFacts completionFacts = {
        mirroredDraws != 0,
        InterlockedCompareExchange(&g_frame.restoreFailures, 0, 0) == 0 &&
            InterlockedCompareExchange(&g_frame.sourceReleaseFailures, 0, 0) == 0 &&
            g_frame.allRestorationsAccepted,
        bothEyesHaveColor,
        hashesDiffer,
        layerPartitionExact,
        menuLayerHasContent,
        !IsPresentationMode() || g_presentationFramePublished,
        IsPresentationMode()};
    g_frame.completedWithDifferingColor =
        bfvr::stereo::IsD3D8FrameCompositionComplete(completionFacts);
    if (IsPresentationMode())
    {
        InterlockedExchange(&g_frame.resourcesReady, 0);
    }
    else
    {
        ReleaseFrameOwnedResources();
    }
    return g_frame.completedWithDifferingColor != FALSE;
}
