void ApplyFrameSemanticPolicy(
    void* device,
    const FrameDrawInvocation& invocation,
    DrawStateSnapshot& snapshot)
{
    const auto addressAt = [&invocation](UINT index) {
        return index < invocation.gameStackDepth
            ? static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(invocation.gameStack[index]))
            : 0U;
    };
    const bfvr::stereo::BF1942D3D8DrawSignature signature = {
        addressAt(0),
        addressAt(1),
        invocation.kind == FrameDrawKind::IndexedPrimitive,
        snapshot.drawPolicy == bfvr::stereo::D3D8DrawPolicy::StereoPerspective,
        invocation.primitiveType,
        invocation.primitiveCount,
        snapshot.vertexShaderOrFvf,
        snapshot.zEnable,
        snapshot.zWriteEnable,
        snapshot.alphaBlendEnable,
        snapshot.fogEnable,
        snapshot.lighting,
        addressAt(2),
        snapshot.originalVertexShaderHash,
        snapshot.originalVertexShaderByteCount,
        snapshot.vertexShaderCreationOrdinal};
    snapshot.semanticClass =
        bfvr::stereo::ClassifyBF1942Win32SemanticDraw(signature);

    // The statically matched PatchTerrain::drawShadowCells return remains the
    // zero-overhead fast path. Some live terrain submissions have a different
    // outer return, so a bounded discovery window also observes BF1942's exact
    // native projected-texture state. It can promote only the already-profiled
    // shared PatchCellBlock indexed draw; all other draws are audit-only.
    const bool sharedPatchCellCandidate =
        signature.wrapperReturnAddress == kGameDrawIndexedPrimitiveReturn &&
        signature.rendererReturnAddress == kPatchCellBlockDrawReturn &&
        signature.indexedPrimitive &&
        signature.perspective &&
        signature.primitiveType == 4 &&
        signature.primitiveCount != 0;
    const LONG dynamicProducer = InterlockedCompareExchange(
        &g_projectedShadowDynamicProducerReturn,
        0,
        0);
    const bool cachedDynamicCandidate =
        dynamicProducer != 0 &&
        sharedPatchCellCandidate &&
        signature.producerReturnAddress ==
            static_cast<std::uint32_t>(dynamicProducer);
    const LONG discoveryBudget = InterlockedCompareExchange(
        &g_projectedShadowDiscoveryBudget,
        0,
        0);
    LONG discoveryTicket = -1;
    const bool boundedDiscoveryShape =
        IsPresentationMode() &&
        dynamicProducer == 0 &&
        discoveryBudget > 0 &&
        signature.perspective &&
        signature.alphaBlendEnable == 1 &&
        signature.primitiveType >= 4 &&
        signature.primitiveType <= 6 &&
        signature.primitiveCount != 0;
    if (boundedDiscoveryShape)
    {
        discoveryTicket =
            InterlockedDecrement(&g_projectedShadowDiscoveryBudget);
    }
    const bool boundedDiscoveryCandidate = discoveryTicket >= 0;
    if (snapshot.semanticClass !=
            bfvr::stereo::D3D8SemanticDrawClass::ProjectedTerrainShadow &&
        (cachedDynamicCandidate || boundedDiscoveryCandidate) &&
        g_methods.getTextureStageState != nullptr)
    {
        constexpr DWORD kD3DTextureStageStateTexCoordIndex = 11;
        constexpr DWORD kD3DTextureStageStateTransformFlags = 24;
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
        if (SUCCEEDED(texCoordResult) &&
            SUCCEEDED(transformFlagsResult) &&
            bfvr::stereo::IsD3D8ProjectedShadowTextureState(
                texCoordIndex,
                transformFlags))
        {
            if (bfvr::IsD3D8RuntimeDiagnosticsEnabled(g_runtimeDiagnostics) &&
                (InterlockedOr(&g_projectedShadowAuditMask, 0x80) &
                 0x80) == 0)
            {
                AppendLog(
                    L"PROJECTED_SHADOW_AUDIT stateMatch wrapper=0x%08lX renderer=0x%08lX producer=0x%08lX indexed=%d primitiveType=%lu primitiveCount=%lu texCoord=0x%08lX transformFlags=0x%08lX.",
                    static_cast<unsigned long>(
                        signature.wrapperReturnAddress),
                    static_cast<unsigned long>(
                        signature.rendererReturnAddress),
                    static_cast<unsigned long>(
                        signature.producerReturnAddress),
                    signature.indexedPrimitive ? 1 : 0,
                    static_cast<unsigned long>(signature.primitiveType),
                    static_cast<unsigned long>(signature.primitiveCount),
                    static_cast<unsigned long>(texCoordIndex),
                    static_cast<unsigned long>(transformFlags));
            }
            if (sharedPatchCellCandidate)
            {
                snapshot.semanticClass = bfvr::stereo::
                    D3D8SemanticDrawClass::ProjectedTerrainShadow;
                if (signature.producerReturnAddress != 0)
                {
                    InterlockedCompareExchange(
                        &g_projectedShadowDynamicProducerReturn,
                        static_cast<LONG>(signature.producerReturnAddress),
                        0);
                }
                if (bfvr::IsD3D8RuntimeDiagnosticsEnabled(
                        g_runtimeDiagnostics) &&
                    (InterlockedOr(&g_projectedShadowAuditMask, 0x200) &
                     0x200) == 0)
                {
                    AppendLog(
                        L"PROJECTED_SHADOW_AUDIT dynamicallyPromoted producer=0x%08lX by exact native texture state.",
                        static_cast<unsigned long>(
                            signature.producerReturnAddress));
                }
            }
        }
    }
    if (discoveryTicket == 0 &&
        InterlockedCompareExchange(
            &g_projectedShadowDynamicProducerReturn,
            0,
            0) == 0 &&
        bfvr::IsD3D8RuntimeDiagnosticsEnabled(g_runtimeDiagnostics) &&
        (InterlockedOr(&g_projectedShadowAuditMask, 0x400) & 0x400) == 0)
    {
        AppendLog(
            L"PROJECTED_SHADOW_AUDIT discoveryExhausted alphaPerspectiveTriangleCandidates=65536 lastWrapper=0x%08lX lastRenderer=0x%08lX lastProducer=0x%08lX.",
            static_cast<unsigned long>(signature.wrapperReturnAddress),
            static_cast<unsigned long>(signature.rendererReturnAddress),
            static_cast<unsigned long>(signature.producerReturnAddress));
    }
    if (IsPresentationMode() &&
        snapshot.semanticClass == bfvr::stereo::
            D3D8SemanticDrawClass::ProjectedTerrainShadow &&
        bfvr::IsD3D8RuntimeDiagnosticsEnabled(g_runtimeDiagnostics) &&
        (InterlockedOr(&g_projectedShadowAuditMask, 0x4) & 0x4) == 0)
    {
        AppendLog(
            L"PROJECTED_SHADOW_AUDIT receiverCandidate wrapper=0x%08lX renderer=0x%08lX producer=0x%08lX indexed=%d perspective=%d primitiveType=%lu primitiveCount=%lu fvfOrShader=0x%08lX z=%lu zWrite=%lu alphaBlend=%lu fog=%lu lighting=%lu semanticClass=%lu.",
            static_cast<unsigned long>(signature.wrapperReturnAddress),
            static_cast<unsigned long>(signature.rendererReturnAddress),
            static_cast<unsigned long>(signature.producerReturnAddress),
            signature.indexedPrimitive ? 1 : 0,
            signature.perspective ? 1 : 0,
            static_cast<unsigned long>(signature.primitiveType),
            static_cast<unsigned long>(signature.primitiveCount),
            static_cast<unsigned long>(signature.vertexShaderOrFvf),
            static_cast<unsigned long>(signature.zEnable),
            static_cast<unsigned long>(signature.zWriteEnable),
            static_cast<unsigned long>(signature.alphaBlendEnable),
            static_cast<unsigned long>(signature.fogEnable),
            static_cast<unsigned long>(signature.lighting),
            static_cast<unsigned long>(snapshot.semanticClass));
    }
    if (snapshot.semanticClass ==
        bfvr::stereo::D3D8SemanticDrawClass::SkyboxCubeFace)
    {
        if (IsPresentationMode())
        {
            const bool rotationBuilt =
                bfvr::BuildD3D8RuntimeRotationOnlyTransforms(
                g_runtimeRenderRequest,
                g_runtimeFramePosePolicy.EyeReference(
                    g_renderViewPoseHook.WasApplied(
                        g_runtimeRenderRequest.sequence)),
                snapshot.view,
                snapshot.projection,
                snapshot.leftView,
                snapshot.rightView,
                snapshot.leftProjection,
                snapshot.rightProjection);
            if (!rotationBuilt)
            {
                snapshot.leftView = snapshot.view;
                snapshot.rightView = snapshot.view;
            }
        }
        else
        {
            snapshot.leftView = snapshot.view;
            snapshot.rightView = snapshot.view;
            snapshot.leftProjection = snapshot.projection;
            snapshot.rightProjection = snapshot.projection;
        }
    }
}

bool PrepareFrameSkinningShaderTransforms(
    void* device,
    DrawStateSnapshot& snapshot)
{
    if (snapshot.semanticClass !=
        bfvr::stereo::D3D8SemanticDrawClass::AnimatedMeshSkinning)
    {
        return true;
    }

    D3DMatrix world = {};
    if (FAILED(g_methods.getTransform(device, kD3DTransformWorld, &world)))
    {
        InterlockedIncrement(&g_frame.skinningShaderPrepareFailures);
        return false;
    }
    const bfvr::d3d8probe::D3D8VertexShaderConstantApi api = {
        g_methods.setVertexShaderConstant,
        g_methods.getVertexShaderConstant};
    const auto result =
        bfvr::d3d8probe::PrepareD3D8SkinningShaderTransforms(
            api,
            device,
            world,
            snapshot.view,
            snapshot.projection,
            snapshot.leftView,
            snapshot.leftProjection,
            snapshot.rightView,
            snapshot.rightProjection,
            snapshot.skinningShaderTransform);
    if (result ==
        bfvr::d3d8probe::SkinningShaderPrepareResult::SourceConstantsMismatch)
    {
        InterlockedIncrement(&g_frame.skinningShaderSourceMismatches);
    }
    if (result != bfvr::d3d8probe::SkinningShaderPrepareResult::Prepared)
    {
        InterlockedIncrement(&g_frame.skinningShaderPrepareFailures);
        return false;
    }
    return true;
}

bool PrepareFrameSpriteShaderTransforms(
    void* device,
    DrawStateSnapshot& snapshot)
{
    if (snapshot.semanticClass !=
        bfvr::stereo::D3D8SemanticDrawClass::TranslucentSprite)
    {
        return true;
    }

    const bfvr::d3d8probe::D3D8VertexShaderConstantApi api = {
        g_methods.setVertexShaderConstant,
        g_methods.getVertexShaderConstant};
    const auto result =
        bfvr::d3d8probe::PrepareD3D8SpriteShaderTransforms(
            api,
            device,
            snapshot.view,
            snapshot.projection,
            snapshot.leftView,
            snapshot.leftProjection,
            snapshot.rightView,
            snapshot.rightProjection,
            snapshot.spriteShaderTransform);
    if (result ==
        bfvr::d3d8probe::SpriteShaderPrepareResult::SourceConstantsMismatch)
    {
        InterlockedIncrement(&g_frame.spriteShaderSourceMismatches);
    }
    if (result != bfvr::d3d8probe::SpriteShaderPrepareResult::Prepared)
    {
        InterlockedIncrement(&g_frame.spriteShaderPrepareFailures);
        return false;
    }
    return true;
}

bool PrepareFrameTreeSpriteShaderTransforms(
    void* device,
    DrawStateSnapshot& snapshot)
{
    if (snapshot.semanticClass !=
        bfvr::stereo::D3D8SemanticDrawClass::TreeMeshProgrammableSprite)
    {
        return true;
    }

    D3DMatrix world = {};
    if (FAILED(g_methods.getTransform(device, kD3DTransformWorld, &world)))
    {
        InterlockedIncrement(&g_frame.treeSpriteShaderPrepareFailures);
        return false;
    }
    const bfvr::d3d8probe::D3D8VertexShaderConstantApi api = {
        g_methods.setVertexShaderConstant,
        g_methods.getVertexShaderConstant};
    const auto result =
        bfvr::d3d8probe::PrepareD3D8TreeSpriteShaderTransforms(
            api,
            device,
            world,
            snapshot.view,
            snapshot.projection,
            snapshot.leftView,
            snapshot.leftProjection,
            snapshot.rightView,
            snapshot.rightProjection,
            snapshot.treeSpriteShaderTransform);
    if (result ==
        bfvr::d3d8probe::TreeSpriteShaderPrepareResult::SourceConstantsMismatch)
    {
        InterlockedIncrement(&g_frame.treeSpriteShaderSourceMismatches);
    }
    if (result !=
        bfvr::d3d8probe::TreeSpriteShaderPrepareResult::Prepared)
    {
        InterlockedIncrement(&g_frame.treeSpriteShaderPrepareFailures);
        return false;
    }
    return true;
}
