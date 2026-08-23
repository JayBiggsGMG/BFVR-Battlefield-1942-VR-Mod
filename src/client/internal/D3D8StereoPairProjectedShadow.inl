// Included inside D3D8StereoPairProbe.cpp after AppendLog is defined.

void RecordProjectedShadowTextureFailure(bool stateReadFailure)
{
    if (stateReadFailure)
    {
        InterlockedIncrement(&g_frame.renderStateReadFailures);
    }
    InterlockedIncrement(&g_frame.projectedShadowTextureFailures);
    if (bfvr::IsD3D8RuntimeDiagnosticsEnabled(g_runtimeDiagnostics))
    {
        InterlockedIncrement(&g_projectedShadowAuditFailures);
    }
}

void PrepareProjectedShadowTextureTransform(
    void* device,
    DrawStateSnapshot& snapshot)
{
    if (snapshot.semanticClass !=
            bfvr::stereo::D3D8SemanticDrawClass::ProjectedTerrainShadow ||
        snapshot.drawPolicy !=
            bfvr::stereo::D3D8DrawPolicy::StereoPerspective)
    {
        return;
    }
    if (bfvr::IsD3D8RuntimeDiagnosticsEnabled(g_runtimeDiagnostics))
    {
        InterlockedIncrement(&g_projectedShadowAuditDraws);
    }

    constexpr DWORD kD3DTextureStageStateTexCoordIndex = 11;
    constexpr DWORD kD3DTextureStageStateTransformFlags = 24;
    if (g_methods.getTextureStageState == nullptr ||
        g_methods.getTransform == nullptr ||
        g_methods.setTransform == nullptr)
    {
        RecordProjectedShadowTextureFailure(true);
        return;
    }

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
        &snapshot.projectedShadowTexture0);
    if (bfvr::IsD3D8RuntimeDiagnosticsEnabled(g_runtimeDiagnostics) &&
        (InterlockedOr(&g_projectedShadowAuditMask, 0x1) & 0x1) == 0)
    {
        AppendLog(
            L"PROJECTED_SHADOW_AUDIT firstDraw texCoordResult=0x%08lX texCoord=0x%08lX transformFlagsResult=0x%08lX transformFlags=0x%08lX textureResult=0x%08lX.",
            static_cast<unsigned long>(texCoordResult),
            static_cast<unsigned long>(texCoordIndex),
            static_cast<unsigned long>(transformFlagsResult),
            static_cast<unsigned long>(transformFlags),
            static_cast<unsigned long>(textureResult));
    }
    if (FAILED(texCoordResult) ||
        FAILED(transformFlagsResult) ||
        FAILED(textureResult))
    {
        RecordProjectedShadowTextureFailure(true);
        return;
    }
    if (!bfvr::stereo::IsD3D8ProjectedShadowTextureState(
            texCoordIndex,
            transformFlags))
    {
        RecordProjectedShadowTextureFailure(false);
        return;
    }

    bfvr::stereo::Matrix4 sourceView = {};
    bfvr::stereo::Matrix4 originalTexture = {};
    bfvr::stereo::Matrix4 replayViews[2] = {};
    std::memcpy(&sourceView, &snapshot.view, sizeof(sourceView));
    std::memcpy(
        &originalTexture,
        &snapshot.projectedShadowTexture0,
        sizeof(originalTexture));
    std::memcpy(&replayViews[0], &snapshot.leftView, sizeof(replayViews[0]));
    std::memcpy(&replayViews[1], &snapshot.rightView, sizeof(replayViews[1]));
    const auto left =
        bfvr::stereo::MakeD3D8ProjectedShadowTextureTransform(
            sourceView,
            replayViews[0],
            originalTexture);
    const auto right =
        bfvr::stereo::MakeD3D8ProjectedShadowTextureTransform(
            sourceView,
            replayViews[1],
            originalTexture);
    if (!left.has_value() || !right.has_value())
    {
        RecordProjectedShadowTextureFailure(false);
        return;
    }

    std::memcpy(
        &snapshot.projectedShadowEyeTexture0[0],
        &*left,
        sizeof(snapshot.projectedShadowEyeTexture0[0]));
    std::memcpy(
        &snapshot.projectedShadowEyeTexture0[1],
        &*right,
        sizeof(snapshot.projectedShadowEyeTexture0[1]));
    if (bfvr::IsD3D8RuntimeDiagnosticsEnabled(g_runtimeDiagnostics) &&
        (InterlockedOr(&g_projectedShadowAuditMask, 0x2) & 0x2) == 0)
    {
        float nativeDelta = 0.0F;
        float stereoDelta = 0.0F;
        for (std::size_t row = 0; row < 4; ++row)
        {
            for (std::size_t column = 0; column < 4; ++column)
            {
                nativeDelta = (std::max)(
                    nativeDelta,
                    std::fabs(
                        left->values[row][column] -
                        originalTexture.values[row][column]));
                stereoDelta = (std::max)(
                    stereoDelta,
                    std::fabs(
                        left->values[row][column] -
                        right->values[row][column]));
            }
        }
        AppendLog(
            L"PROJECTED_SHADOW_AUDIT prepared nativeDelta=%.9f leftRightDelta=%.9f.",
            static_cast<double>(nativeDelta),
            static_cast<double>(stereoDelta));
    }
    snapshot.projectedShadowTexturePrepared = true;
}

HRESULT ApplyProjectedShadowTextureTransform(
    void* device,
    DrawStateSnapshot& snapshot,
    std::size_t eye)
{
    if (!snapshot.projectedShadowTexturePrepared || eye >= 2)
    {
        return S_OK;
    }

    const HRESULT result = g_methods.setTransform(
        device,
        kD3DTransformTexture0,
        &snapshot.projectedShadowEyeTexture0[eye]);
    if (SUCCEEDED(result))
    {
        snapshot.projectedShadowTexture0Overridden = true;
        InterlockedIncrement(
            &g_frame.projectedShadowTextureEyeApplications);
        if (bfvr::IsD3D8RuntimeDiagnosticsEnabled(g_runtimeDiagnostics))
        {
            InterlockedIncrement(&g_projectedShadowAuditEyeApplications);
        }
        const LONG eyeAuditBit = eye == 0 ? 0x8 : 0x10;
        if (bfvr::IsD3D8RuntimeDiagnosticsEnabled(g_runtimeDiagnostics) &&
            (InterlockedOr(&g_projectedShadowAuditMask, eyeAuditBit) &
             eyeAuditBit) == 0)
        {
            AppendLog(
                L"PROJECTED_SHADOW_AUDIT eyeApplied eye=%lu result=0x%08lX.",
                static_cast<unsigned long>(eye),
                static_cast<unsigned long>(result));
        }
        return result;
    }

    RecordProjectedShadowTextureFailure(false);
    // Restore the native matrix immediately. This lets the current eye fall
    // back to BF1942's accepted flat path and prevents a successful left-eye
    // correction from leaking into the right eye after a failed write.
    const HRESULT restoreResult = g_methods.setTransform(
        device,
        kD3DTransformTexture0,
        &snapshot.projectedShadowTexture0);
    if (FAILED(restoreResult))
    {
        RecordProjectedShadowTextureFailure(false);
        return restoreResult;
    }
    return S_FALSE;
}
