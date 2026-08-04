// Included inside D3D8StereoPairProbe.cpp after InvokeOriginalFrameDraw.
// The packed-depth export's alpha channel is a dedicated water mask. Replaying
// the exact additive water material with alpha-only writes preserves the
// material's effective opacity instead of stamping classified triangles solid.

void CaptureVisibleWaterMask(
    void* device,
    const FrameDrawInvocation& invocation,
    const DrawStateSnapshot& snapshot,
    std::size_t eye)
{
    const bool exactAdditiveWaterPass =
        snapshot.semanticClass ==
            bfvr::stereo::D3D8SemanticDrawClass::WaterSurface &&
        snapshot.zWriteEnable != 0;
    if (!exactAdditiveWaterPass ||
        !IsPresentationMode() ||
        !g_presentationBridge.WaterReflectionsRequested() ||
        g_frame.waterMaskValid == FALSE ||
        eye >= 2 ||
        g_frame.depthExport[eye] == nullptr ||
        g_frame.ownedColor[eye] == nullptr ||
        g_frame.ownedDepth[eye] == nullptr)
    {
        return;
    }

    DWORD priorColorWrite = 0;
    const HRESULT readResult = g_methods.getRenderState(
        device,
        kD3DRenderStateColorWriteEnable,
        &priorColorWrite);
    const HRESULT targetResult = SUCCEEDED(readResult)
        ? g_methods.setRenderTarget(
            device,
            g_frame.depthExport[eye],
            g_frame.ownedDepth[eye])
        : readResult;
    const HRESULT alphaOnlyResult = SUCCEEDED(targetResult)
        ? g_methods.setRenderState(
            device,
            kD3DRenderStateColorWriteEnable,
            kD3DColorWriteEnableAlpha)
        : targetResult;
    const HRESULT drawResult = SUCCEEDED(alphaOnlyResult)
        ? InvokeOriginalFrameDraw(device, invocation)
        : alphaOnlyResult;

    const HRESULT colorWriteRestoreResult = SUCCEEDED(readResult)
        ? g_methods.setRenderState(
            device,
            kD3DRenderStateColorWriteEnable,
            priorColorWrite)
        : readResult;
    const HRESULT targetRestoreResult = g_methods.setRenderTarget(
        device,
        g_frame.ownedColor[eye],
        g_frame.ownedDepth[eye]);

    bool exact =
        SUCCEEDED(readResult) &&
        SUCCEEDED(targetResult) &&
        SUCCEEDED(alphaOnlyResult) &&
        SUCCEEDED(drawResult) &&
        SUCCEEDED(colorWriteRestoreResult) &&
        SUCCEEDED(targetRestoreResult);
    if (exact && bfvr::IsDeepD3D8RuntimeDiagnostics(g_runtimeDiagnostics))
    {
        DWORD actualColorWrite = 0;
        void* actualColor = nullptr;
        void* actualDepth = nullptr;
        const HRESULT stateRead = g_methods.getRenderState(
            device,
            kD3DRenderStateColorWriteEnable,
            &actualColorWrite);
        const HRESULT colorRead =
            g_methods.getRenderTarget(device, &actualColor);
        const HRESULT depthRead =
            g_methods.getDepthStencilSurface(device, &actualDepth);
        exact =
            SUCCEEDED(stateRead) &&
            SUCCEEDED(colorRead) &&
            SUCCEEDED(depthRead) &&
            actualColorWrite == priorColorWrite &&
            actualColor == g_frame.ownedColor[eye] &&
            actualDepth == g_frame.ownedDepth[eye];
        ReleaseUnknown(actualColor);
        ReleaseUnknown(actualDepth);
    }

    if (exact)
    {
        InterlockedIncrement(&g_frame.waterMaskDraws);
    }
    else
    {
        g_frame.waterMaskValid = FALSE;
        InterlockedIncrement(&g_frame.waterMaskFailures);
    }
}
