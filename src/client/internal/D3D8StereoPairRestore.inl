bool RestoreFrameState(void* device, const DrawStateSnapshot& snapshot)
{
    const bfvr::d3d8probe::D3D8VertexShaderConstantApi shaderApi = {
        g_methods.setVertexShaderConstant,
        g_methods.getVertexShaderConstant};

    bool writesSucceeded = false;
    {
        bfvr::d3d8probe::ScopedPerformanceAccumulator timer(
            g_frame.restoreWriteQpcTicks,
            bfvr::IsD3D8RuntimeDiagnosticsEnabled(g_runtimeDiagnostics));
        const bool skinningRestored =
            bfvr::d3d8probe::RestoreD3D8SkinningShaderConstants(
                shaderApi,
                device,
                snapshot.skinningShaderTransform);
        const bool spriteRestored =
            bfvr::d3d8probe::RestoreD3D8SpriteShaderConstants(
                shaderApi,
                device,
                snapshot.spriteShaderTransform);
        const bool treeSpriteRestored =
            bfvr::d3d8probe::RestoreD3D8TreeSpriteShaderConstants(
                shaderApi,
                device,
                snapshot.treeSpriteShaderTransform);
        const HRESULT targetResult = g_methods.setRenderTarget(
            device,
            snapshot.sourceColor,
            snapshot.sourceDepth);
        const HRESULT viewportResult =
            g_methods.setViewport(device, &snapshot.viewport);
        const HRESULT viewResult =
            g_methods.setTransform(device, kD3DTransformView, &snapshot.view);
        const HRESULT projectionResult = g_methods.setTransform(
            device,
            kD3DTransformProjection,
            &snapshot.projection);
        const HRESULT worldResult =
            g_methods.setTransform(device, kD3DTransformWorld, &snapshot.world);
        const HRESULT waterTextureResult = snapshot.waterTexture0Overridden
            ? g_methods.setTransform(
                device,
                kD3DTransformTexture0,
                &snapshot.waterTexture0)
            : S_OK;
        const HRESULT projectedShadowTextureResult =
            snapshot.projectedShadowTexture0Overridden
            ? g_methods.setTransform(
                device,
                kD3DTransformTexture0,
                &snapshot.projectedShadowTexture0)
            : S_OK;
        writesSucceeded =
            skinningRestored &&
            spriteRestored &&
            treeSpriteRestored &&
            SUCCEEDED(targetResult) &&
            SUCCEEDED(viewportResult) &&
            SUCCEEDED(viewResult) &&
            SUCCEEDED(projectionResult) &&
            SUCCEEDED(worldResult) &&
            SUCCEEDED(waterTextureResult) &&
            SUCCEEDED(projectedShadowTextureResult);
        if (snapshot.projectedShadowTexture0Overridden &&
            SUCCEEDED(projectedShadowTextureResult) &&
            bfvr::IsD3D8RuntimeDiagnosticsEnabled(g_runtimeDiagnostics) &&
            (InterlockedOr(&g_projectedShadowAuditMask, 0x20) & 0x20) == 0)
        {
            AppendLog(
                L"PROJECTED_SHADOW_AUDIT restored textureResult=0x%08lX transactionWritesSucceeded=%d.",
                static_cast<unsigned long>(projectedShadowTextureResult),
                writesSucceeded ? 1 : 0);
        }
    }

    bool exact = writesSucceeded;
    if (bfvr::IsDeepD3D8RuntimeDiagnostics(g_runtimeDiagnostics))
    {
        InterlockedIncrement(&g_frame.restoreVerifications);
        bfvr::d3d8probe::ScopedPerformanceAccumulator timer(
            g_frame.restoreVerifyQpcTicks);
        const bool shaderConstantsExact =
            bfvr::d3d8probe::VerifyD3D8SkinningShaderConstants(
                shaderApi,
                device,
                snapshot.skinningShaderTransform) &&
            bfvr::d3d8probe::VerifyD3D8SpriteShaderConstants(
                shaderApi,
                device,
                snapshot.spriteShaderTransform) &&
            bfvr::d3d8probe::VerifyD3D8TreeSpriteShaderConstants(
                shaderApi,
                device,
                snapshot.treeSpriteShaderTransform);
        void* actualColor = nullptr;
        void* actualDepth = nullptr;
        D3DViewport actualViewport = {};
        D3DMatrix actualWorld = {};
        D3DMatrix actualView = {};
        D3DMatrix actualProjection = {};
        D3DMatrix actualWaterTexture = {};
        D3DMatrix actualProjectedShadowTexture = {};
        const HRESULT getColorResult =
            g_methods.getRenderTarget(device, &actualColor);
        const HRESULT getDepthResult =
            g_methods.getDepthStencilSurface(device, &actualDepth);
        const HRESULT getViewportResult =
            g_methods.getViewport(device, &actualViewport);
        const HRESULT getWorldResult = g_methods.getTransform(
            device,
            kD3DTransformWorld,
            &actualWorld);
        const HRESULT getViewResult = g_methods.getTransform(
            device,
            kD3DTransformView,
            &actualView);
        const HRESULT getProjectionResult = g_methods.getTransform(
            device,
            kD3DTransformProjection,
            &actualProjection);
        const HRESULT getWaterTextureResult =
            snapshot.waterTexture0Overridden
            ? g_methods.getTransform(
                device,
                kD3DTransformTexture0,
                &actualWaterTexture)
            : S_OK;
        const HRESULT getProjectedShadowTextureResult =
            snapshot.projectedShadowTexture0Overridden
            ? g_methods.getTransform(
                device,
                kD3DTransformTexture0,
                &actualProjectedShadowTexture)
            : S_OK;
        exact =
            writesSucceeded &&
            SUCCEEDED(getColorResult) &&
            SUCCEEDED(getDepthResult) &&
            SUCCEEDED(getViewportResult) &&
            SUCCEEDED(getWorldResult) &&
            SUCCEEDED(getViewResult) &&
            SUCCEEDED(getProjectionResult) &&
            SUCCEEDED(getWaterTextureResult) &&
            SUCCEEDED(getProjectedShadowTextureResult) &&
            shaderConstantsExact &&
            actualColor == snapshot.sourceColor &&
            actualDepth == snapshot.sourceDepth &&
            EqualViewport(actualViewport, snapshot.viewport) &&
            EqualMatrix(actualWorld, snapshot.world) &&
            EqualMatrix(actualView, snapshot.view) &&
            EqualMatrix(actualProjection, snapshot.projection) &&
            (!snapshot.waterTexture0Overridden ||
             EqualMatrix(actualWaterTexture, snapshot.waterTexture0)) &&
            (!snapshot.projectedShadowTexture0Overridden ||
             EqualMatrix(
                 actualProjectedShadowTexture,
                 snapshot.projectedShadowTexture0));
        ReleaseUnknown(actualColor);
        ReleaseUnknown(actualDepth);
    }

    InterlockedIncrement(&g_frame.restoreChecks);
    if (!exact)
    {
        g_frame.allRestorationsAccepted = FALSE;
        InterlockedIncrement(&g_frame.restoreFailures);
    }
    return exact;
}
