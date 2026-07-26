void ApplyFrameSemanticPolicy(
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
    if (snapshot.semanticClass ==
        bfvr::stereo::D3D8SemanticDrawClass::SkyboxCubeFace)
    {
        if (IsPresentationMode())
        {
            const bool rotationBuilt =
                bfvr::BuildD3D8RuntimeRotationOnlyTransforms(
                g_runtimeRenderRequest,
                g_renderViewPoseHook.EyeReference(
                    g_runtimeRenderRequest.sequence,
                    g_runtimeHeadReference),
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
