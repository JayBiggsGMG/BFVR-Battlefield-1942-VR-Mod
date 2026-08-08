// Included inside D3D8StereoPairProbe.cpp after AppendLog is defined.

void PrepareStereoStableWaterReflection(DrawStateSnapshot& snapshot)
{
    if (!bfvr::stereo::ShouldUseBF1942StereoStableWaterReflection(
            snapshot.semanticClass,
            snapshot.zWriteEnable,
            g_legacyStereoWaterReflection))
    {
        return;
    }
    bfvr::stereo::Matrix4 sharedView = {};
    bfvr::stereo::Matrix4 leftView = {};
    bfvr::stereo::Matrix4 leftProjection = {};
    bfvr::stereo::Matrix4 rightView = {};
    bfvr::stereo::Matrix4 rightProjection = {};
    std::memcpy(&sharedView, &snapshot.view, sizeof(sharedView));
    std::memcpy(&leftView, &snapshot.leftView, sizeof(leftView));
    std::memcpy(
        &leftProjection,
        &snapshot.leftProjection,
        sizeof(leftProjection));
    std::memcpy(&rightView, &snapshot.rightView, sizeof(rightView));
    std::memcpy(
        &rightProjection,
        &snapshot.rightProjection,
        sizeof(rightProjection));
    const auto transforms =
        bfvr::stereo::MakeD3D8WaterReflectionStereoTransforms(
            sharedView,
            leftView,
            leftProjection,
            rightView,
            rightProjection);
    if (!transforms.has_value())
    {
        InterlockedIncrement(&g_frame.waterReflectionStateFailures);
        return;
    }

    std::memcpy(
        &snapshot.waterSharedView,
        &transforms->sharedView,
        sizeof(snapshot.waterSharedView));
    std::memcpy(
        &snapshot.waterEyeProjection[0],
        &transforms->eyeProjections[0],
        sizeof(snapshot.waterEyeProjection[0]));
    std::memcpy(
        &snapshot.waterEyeProjection[1],
        &transforms->eyeProjections[1],
        sizeof(snapshot.waterEyeProjection[1]));
    snapshot.waterStereoPrepared = true;
}

void RecordWaterTextureBasisFailure(bool stateReadFailure)
{
    if (stateReadFailure)
    {
        InterlockedIncrement(&g_frame.renderStateReadFailures);
    }
    InterlockedIncrement(&g_frame.waterReflectionStateFailures);
    InterlockedIncrement(&g_frame.waterTextureBasisFailures);
}

void PrepareWaterReflectionTextureBasis(
    void* device,
    DrawStateSnapshot& snapshot)
{
    if (!g_waterReflectionTextureBasisEnabled ||
        !IsPresentationMode() ||
        !bfvr::stereo::ShouldUseBF1942StereoStableWaterReflection(
            snapshot.semanticClass,
            snapshot.zWriteEnable,
            g_legacyStereoWaterReflection))
    {
        return;
    }

    constexpr DWORD kD3DTextureStageStateTexCoordIndex = 11;
    constexpr DWORD kD3DTextureStageStateTransformFlags = 24;
    constexpr DWORD kD3DTciCameraSpaceReflectionVector = 0x00030000;
    constexpr DWORD kD3DTextureTransformCount3 = 3;
    DWORD texCoordIndex = 0;
    DWORD transformFlags = 0;
    const HRESULT texCoordResult = g_methods.getTextureStageState(
        device,
        0,
        kD3DTextureStageStateTexCoordIndex,
        &texCoordIndex);
    const HRESULT transformFlagsResult = g_methods.getTextureStageState(
        device,
        0,
        kD3DTextureStageStateTransformFlags,
        &transformFlags);
    const HRESULT textureResult = g_methods.getTransform(
        device,
        kD3DTransformTexture0,
        &snapshot.waterTexture0);
    if (FAILED(texCoordResult) ||
        FAILED(transformFlagsResult) ||
        FAILED(textureResult))
    {
        RecordWaterTextureBasisFailure(true);
        return;
    }
    if (texCoordIndex != kD3DTciCameraSpaceReflectionVector ||
        transformFlags != kD3DTextureTransformCount3)
    {
        RecordWaterTextureBasisFailure(false);
        return;
    }

    bfvr::stereo::Matrix4 logicalCamera = {};
    if (!g_renderViewPoseHook.TryGetAppliedSourceCamera(
            g_runtimeRenderRequest.sequence,
            logicalCamera))
    {
        RecordWaterTextureBasisFailure(false);
        return;
    }

    bfvr::stereo::Matrix4 originalTexture = {};
    bfvr::stereo::Matrix4 replayViews[2] = {};
    std::memcpy(
        &originalTexture,
        &snapshot.waterTexture0,
        sizeof(originalTexture));
    std::memcpy(&replayViews[0], &snapshot.leftView, sizeof(replayViews[0]));
    std::memcpy(&replayViews[1], &snapshot.rightView, sizeof(replayViews[1]));
    const auto left =
        bfvr::stereo::MakeD3D8WaterReflectionTextureTransform(
            logicalCamera,
            replayViews[0],
            originalTexture);
    const auto right =
        bfvr::stereo::MakeD3D8WaterReflectionTextureTransform(
            logicalCamera,
            replayViews[1],
            originalTexture);
    if (!left.has_value() || !right.has_value())
    {
        RecordWaterTextureBasisFailure(false);
        return;
    }

    std::memcpy(
        &snapshot.waterEyeTexture0[0],
        &*left,
        sizeof(snapshot.waterEyeTexture0[0]));
    std::memcpy(
        &snapshot.waterEyeTexture0[1],
        &*right,
        sizeof(snapshot.waterEyeTexture0[1]));
    for (const D3DMatrix& eyeTexture : snapshot.waterEyeTexture0)
    {
        for (std::size_t row = 0; row < 4; ++row)
        {
            for (std::size_t column = 0; column < 4; ++column)
            {
                snapshot.waterTextureBasisMaxDelta = (std::max)(
                    snapshot.waterTextureBasisMaxDelta,
                    std::fabs(
                        eyeTexture.values[row][column] -
                        snapshot.waterTexture0.values[row][column]));
            }
        }
    }
    snapshot.waterTextureBasisPrepared = true;
}

HRESULT ApplyWaterReflectionTextureBasis(
    void* device,
    DrawStateSnapshot& snapshot,
    std::size_t eye)
{
    if (!snapshot.waterTextureBasisPrepared || eye >= 2)
    {
        return S_OK;
    }

    const HRESULT result = g_methods.setTransform(
        device,
        kD3DTransformTexture0,
        &snapshot.waterEyeTexture0[eye]);
    if (SUCCEEDED(result))
    {
        snapshot.waterTexture0Overridden = true;
        InterlockedIncrement(&g_frame.waterTextureBasisEyeApplications);
        return result;
    }

    RecordWaterTextureBasisFailure(false);
    // A failed right-eye write can leave the successful left-eye correction
    // active. Restore BF1942's matrix immediately so this eye still draws the
    // accepted baseline. If even that fails, abort this replay rather than
    // rendering two eyes with an unknown texture basis.
    const HRESULT restoreResult = g_methods.setTransform(
        device,
        kD3DTransformTexture0,
        &snapshot.waterTexture0);
    if (FAILED(restoreResult))
    {
        RecordWaterTextureBasisFailure(false);
        return restoreResult;
    }
    return S_FALSE;
}

void LogWaterPassState(
    void* device,
    const DrawStateSnapshot& snapshot)
{
    if (g_methods.getRenderState == nullptr ||
        g_methods.getTextureStageState == nullptr)
    {
        return;
    }

    const LONG passBit = snapshot.zWriteEnable != 0 ? 0x2 : 0x1;
    if ((InterlockedOr(&g_loggedWaterPassStateMask, passBit) & passBit) != 0)
    {
        return;
    }

    DWORD sourceBlend = 0;
    DWORD destinationBlend = 0;
    DWORD textureFactor = 0;
    DWORD localViewer = 0;
    const HRESULT sourceBlendResult = g_methods.getRenderState(
        device,
        kD3DRenderStateSourceBlend,
        &sourceBlend);
    const HRESULT destinationBlendResult = g_methods.getRenderState(
        device,
        kD3DRenderStateDestinationBlend,
        &destinationBlend);
    const HRESULT textureFactorResult = g_methods.getRenderState(
        device,
        kD3DRenderStateTextureFactor,
        &textureFactor);
    const HRESULT localViewerResult = g_methods.getRenderState(
        device,
        kD3DRenderStateLocalViewer,
        &localViewer);

    constexpr std::array<DWORD, 8> kWaterStageStates = {
        1,  // D3DTSS_COLOROP
        2,  // D3DTSS_COLORARG1
        3,  // D3DTSS_COLORARG2
        4,  // D3DTSS_ALPHAOP
        5,  // D3DTSS_ALPHAARG1
        6,  // D3DTSS_ALPHAARG2
        11, // D3DTSS_TEXCOORDINDEX
        24  // D3DTSS_TEXTURETRANSFORMFLAGS
    };
    std::array<std::array<DWORD, kWaterStageStates.size()>, 2> stages = {};
    DWORD successfulStageReads = 0;
    for (DWORD stage = 0; stage < stages.size(); ++stage)
    {
        for (std::size_t state = 0; state < kWaterStageStates.size(); ++state)
        {
            if (SUCCEEDED(g_methods.getTextureStageState(
                    device,
                    stage,
                    kWaterStageStates[state],
                    &stages[stage][state])))
            {
                successfulStageReads |= 1U <<
                    (stage * kWaterStageStates.size() + state);
            }
        }
    }

    AppendLog(
        L"D3D8 water pass diagnostic: zWrite=%lu basis[prepared=%u maxDelta=%.6f] blend[src=%lu(%08lX) dst=%lu(%08lX)] textureFactor=%08lX(%08lX) localViewer=%lu(%08lX) stageReads=%04lX stage0[colorOp=%lu arg1=%lu arg2=%lu alphaOp=%lu arg1=%lu arg2=%lu coord=%08lX transform=%lu] stage1[colorOp=%lu arg1=%lu arg2=%lu alphaOp=%lu arg1=%lu arg2=%lu coord=%08lX transform=%lu].",
        snapshot.zWriteEnable,
        snapshot.waterTextureBasisPrepared ? 1U : 0U,
        static_cast<double>(snapshot.waterTextureBasisMaxDelta),
        sourceBlend,
        static_cast<unsigned long>(sourceBlendResult),
        destinationBlend,
        static_cast<unsigned long>(destinationBlendResult),
        textureFactor,
        static_cast<unsigned long>(textureFactorResult),
        localViewer,
        static_cast<unsigned long>(localViewerResult),
        successfulStageReads,
        stages[0][0],
        stages[0][1],
        stages[0][2],
        stages[0][3],
        stages[0][4],
        stages[0][5],
        stages[0][6],
        stages[0][7],
        stages[1][0],
        stages[1][1],
        stages[1][2],
        stages[1][3],
        stages[1][4],
        stages[1][5],
        stages[1][6],
        stages[1][7]);
}
