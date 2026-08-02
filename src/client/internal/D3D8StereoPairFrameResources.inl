void ReleaseFrameOwnedResources()
{
    if (IsPresentationMode() &&
        g_presentationBridge.UsesGpuSharedTargets())
    {
        g_presentationBridge.PrepareForResourceRelease();
    }
    for (std::size_t index = 0; index < 3; ++index)
    {
        if (g_frame.reusableReadback[index] != nullptr)
        {
            g_frame.reusableReadbackRelease[index] =
                bfvr::d3d8probe::ReleaseReusableReadback(
                    g_readbackApi,
                    g_frame.reusableReadback[index]);
            g_frame.reusableReadbackDescription[index] = {};
        }
    }
    if (g_frame.menuDepth != nullptr)
    {
        g_frame.menuDepthRelease = ReleaseUnknown(g_frame.menuDepth);
        g_frame.menuDepth = nullptr;
    }
    if (g_frame.menuColor != nullptr)
    {
        g_frame.menuColorRelease = ReleaseUnknown(g_frame.menuColor);
        g_frame.menuColor = nullptr;
    }
    for (std::size_t eye = 0; eye < 2; ++eye)
    {
        if (g_frame.ownedDepth[eye] != nullptr)
        {
            g_frame.ownedDepthRelease[eye] =
                ReleaseUnknown(g_frame.ownedDepth[eye]);
            g_frame.ownedDepth[eye] = nullptr;
        }
        if (g_frame.ownedColor[eye] != nullptr)
        {
            g_frame.ownedColorRelease[eye] =
                ReleaseUnknown(g_frame.ownedColor[eye]);
            g_frame.ownedColor[eye] = nullptr;
        }
    }
    InterlockedExchange(&g_frame.resourcesReady, 0);
}

bool CreateAndClearFrameResources(void* device, const DrawStateSnapshot& snapshot)
{
    if (InterlockedCompareExchange(&g_frame.resourcesReady, 0, 0) != 0)
    {
        return true;
    }

    g_frame.colorDescription = snapshot.colorDescription;
    g_frame.depthDescription = snapshot.depthDescription;
    if (IsPresentationMode())
    {
        g_frame.colorDescription.width =
            g_presentationBridge.LeftWorldWidth();
        g_frame.colorDescription.height =
            g_presentationBridge.LeftWorldHeight();
        g_frame.colorDescription.format =
            g_presentationBridge.WorldD3DFormat();
        g_frame.depthDescription.width = g_frame.colorDescription.width;
        g_frame.depthDescription.height = g_frame.colorDescription.height;
        g_runtimeWorldViewport = {
            0,
            0,
            g_frame.colorDescription.width,
            g_frame.colorDescription.height,
            snapshot.viewport.minZ,
            snapshot.viewport.maxZ};
    }
    g_frame.menuColorDescription = snapshot.colorDescription;
    if (IsPresentationMode())
    {
        g_frame.menuColorDescription.width =
            g_presentationBridge.UiWidth();
        g_frame.menuColorDescription.height =
            g_presentationBridge.UiHeight();
        g_frame.menuColorDescription.format =
            g_presentationBridge.UiD3DFormat();
    }
    else
    {
        g_frame.menuColorDescription.format = kD3DFormatA8R8G8B8;
    }
    bool created =
        g_frame.ownedColor[0] != nullptr &&
        g_frame.ownedColor[1] != nullptr &&
        g_frame.ownedDepth[0] != nullptr &&
        g_frame.ownedDepth[1] != nullptr &&
        g_frame.menuColor != nullptr &&
        g_frame.menuDepth != nullptr;
    if (!created)
    {
        created = true;
        for (std::size_t eye = 0; eye < 2; ++eye)
        {
            const HRESULT colorResult =
                g_frame.ownedColor[eye] != nullptr
                ? S_OK
                : g_methods.createRenderTarget(
                    device,
                    g_frame.colorDescription.width,
                    g_frame.colorDescription.height,
                    g_frame.colorDescription.format,
                    g_frame.colorDescription.multiSampleType,
                    FALSE,
                    &g_frame.ownedColor[eye]);
            const HRESULT depthResult =
                g_frame.ownedDepth[eye] != nullptr
                ? S_OK
                : g_methods.createDepthStencilSurface(
                    device,
                    g_frame.depthDescription.width,
                    g_frame.depthDescription.height,
                    g_frame.depthDescription.format,
                    g_frame.depthDescription.multiSampleType,
                    &g_frame.ownedDepth[eye]);
            if (FAILED(colorResult) ||
                FAILED(depthResult) ||
                g_frame.ownedColor[eye] == nullptr ||
                g_frame.ownedDepth[eye] == nullptr)
            {
                created = false;
                break;
            }
        }
        if (created)
        {
            const HRESULT menuColorResult =
                g_frame.menuColor != nullptr
                ? S_OK
                : g_methods.createRenderTarget(
                    device,
                    g_frame.menuColorDescription.width,
                    g_frame.menuColorDescription.height,
                    g_frame.menuColorDescription.format,
                    g_frame.menuColorDescription.multiSampleType,
                    FALSE,
                    &g_frame.menuColor);
            const HRESULT menuDepthResult =
                g_frame.menuDepth != nullptr
                ? S_OK
                : g_methods.createDepthStencilSurface(
                    device,
                    g_frame.menuColorDescription.width,
                    g_frame.menuColorDescription.height,
                    snapshot.depthDescription.format,
                    snapshot.depthDescription.multiSampleType,
                    &g_frame.menuDepth);
            created =
                SUCCEEDED(menuColorResult) &&
                SUCCEEDED(menuDepthResult) &&
                g_frame.menuColor != nullptr &&
                g_frame.menuDepth != nullptr;
        }
    }

    bool cleared = created;
    if (created)
    {
        const DWORD clearFlags = kD3DClearTarget |
            kD3DClearZBuffer |
            (HasStencil(snapshot.depthDescription.format) ? kD3DClearStencil : 0);
        for (std::size_t eye = 0; eye < 2; ++eye)
        {
            const HRESULT targetResult = g_methods.setRenderTarget(
                device,
                g_frame.ownedColor[eye],
                g_frame.ownedDepth[eye]);
            const D3DViewport& viewport = IsPresentationMode()
                ? g_runtimeWorldViewport
                : snapshot.viewport;
            const HRESULT viewportResult = SUCCEEDED(targetResult)
                ? g_methods.setViewport(device, &viewport)
                : E_FAIL;
            const HRESULT clearResult = SUCCEEDED(viewportResult)
                ? g_methods.clear(
                    device,
                    0,
                    nullptr,
                    clearFlags,
                    kOwnedTargetClearColor,
                    1.0F,
                    0)
                : E_FAIL;
            if (FAILED(clearResult))
            {
                cleared = false;
                break;
            }
        }
        if (cleared)
        {
            const HRESULT targetResult = g_methods.setRenderTarget(
                device,
                g_frame.menuColor,
                g_frame.menuDepth);
            const HRESULT viewportResult = SUCCEEDED(targetResult)
                ? g_methods.setViewport(device, &snapshot.viewport)
                : E_FAIL;
            const HRESULT clearResult = SUCCEEDED(viewportResult)
                ? g_methods.clear(
                    device,
                    0,
                    nullptr,
                    clearFlags,
                    kMenuLayerClearColor,
                    1.0F,
                    0)
                : E_FAIL;
            cleared = SUCCEEDED(clearResult);
        }
    }

    const bool restored = RestoreFrameState(device, snapshot);
    if (!created || !cleared || !restored)
    {
        ReleaseFrameOwnedResources();
        return false;
    }
    InterlockedExchange(&g_frame.resourcesReady, 1);
    return true;
}
