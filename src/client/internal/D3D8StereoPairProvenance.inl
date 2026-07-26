// Included inside D3D8StereoPairProbe.cpp's anonymous namespace after the
// draw snapshot, method table, and frame record have been defined.

void ReadProvenanceRenderStates(void* device, DrawStateSnapshot& snapshot)
{
    const struct
    {
        DWORD state;
        DWORD* value;
    } reads[] = {
        {kD3DRenderStateZEnable, &snapshot.zEnable},
        {kD3DRenderStateZWriteEnable, &snapshot.zWriteEnable},
        {kD3DRenderStateAlphaBlendEnable, &snapshot.alphaBlendEnable},
        {kD3DRenderStateFogEnable, &snapshot.fogEnable},
        {kD3DRenderStateLighting, &snapshot.lighting}};
    for (const auto& read : reads)
    {
        if (FAILED(g_methods.getRenderState(device, read.state, read.value)))
        {
            InterlockedIncrement(&g_frame.renderStateReadFailures);
        }
    }
}

bool MatchesProvenance(
    const DrawProvenanceRecord& record,
    const FrameDrawInvocation& invocation,
    const DrawStateSnapshot& snapshot)
{
    return record.gameStackDepth == invocation.gameStackDepth &&
        std::memcmp(
            record.gameStack,
            invocation.gameStack,
            sizeof(record.gameStack)) == 0 &&
        record.kind == invocation.kind &&
        record.policy == snapshot.drawPolicy &&
        record.semanticClass == snapshot.semanticClass &&
        record.primitiveType == invocation.primitiveType &&
        record.viewHash == HashMatrix(snapshot.view) &&
        record.projectionHash == HashMatrix(snapshot.projection) &&
        record.vertexShaderOrFvf == snapshot.vertexShaderOrFvf &&
        record.originalVertexShaderHash ==
            snapshot.originalVertexShaderHash &&
        record.originalVertexShaderByteCount ==
            snapshot.originalVertexShaderByteCount &&
        record.vertexShaderCreationOrdinal ==
            snapshot.vertexShaderCreationOrdinal &&
        record.zEnable == snapshot.zEnable &&
        record.zWriteEnable == snapshot.zWriteEnable &&
        record.alphaBlendEnable == snapshot.alphaBlendEnable &&
        record.fogEnable == snapshot.fogEnable &&
        record.lighting == snapshot.lighting;
}

void RecordDrawProvenance(
    const FrameDrawInvocation& invocation,
    const DrawStateSnapshot& snapshot)
{
    const LONG primitiveCount = static_cast<LONG>(std::min<UINT>(
        invocation.primitiveCount,
        static_cast<UINT>(LONG_MAX)));
    const LONG recordCount = InterlockedCompareExchange(
        &g_frame.provenanceCount,
        0,
        0);
    for (LONG index = 0; index < recordCount; ++index)
    {
        DrawProvenanceRecord& record = g_frame.provenance[index];
        if (MatchesProvenance(record, invocation, snapshot))
        {
            InterlockedIncrement(&record.drawCount);
            InterlockedExchangeAdd(&record.primitiveCount, primitiveCount);
            return;
        }
    }
    if (recordCount >= kMaximumProvenanceSites)
    {
        InterlockedIncrement(&g_frame.provenanceOverflow);
        return;
    }

    DrawProvenanceRecord& record = g_frame.provenance[recordCount];
    std::memcpy(record.gameStack, invocation.gameStack, sizeof(record.gameStack));
    record.gameStackDepth = invocation.gameStackDepth;
    record.kind = invocation.kind;
    record.policy = snapshot.drawPolicy;
    record.semanticClass = snapshot.semanticClass;
    record.primitiveType = invocation.primitiveType;
    record.vertexShaderOrFvf = snapshot.vertexShaderOrFvf;
    record.originalVertexShaderHash =
        snapshot.originalVertexShaderHash;
    record.originalVertexShaderByteCount =
        snapshot.originalVertexShaderByteCount;
    record.vertexShaderCreationOrdinal =
        snapshot.vertexShaderCreationOrdinal;
    record.zEnable = snapshot.zEnable;
    record.zWriteEnable = snapshot.zWriteEnable;
    record.alphaBlendEnable = snapshot.alphaBlendEnable;
    record.fogEnable = snapshot.fogEnable;
    record.lighting = snapshot.lighting;
    record.viewHash = HashMatrix(snapshot.view);
    record.projectionHash = HashMatrix(snapshot.projection);
    record.viewM30 = snapshot.view.values[3][0];
    record.viewM31 = snapshot.view.values[3][1];
    record.viewM32 = snapshot.view.values[3][2];
    record.projectionM00 = snapshot.projection.values[0][0];
    record.projectionM11 = snapshot.projection.values[1][1];
    record.projectionM20 = snapshot.projection.values[2][0];
    record.projectionM21 = snapshot.projection.values[2][1];
    record.projectionM22 = snapshot.projection.values[2][2];
    record.projectionM23 = snapshot.projection.values[2][3];
    record.projectionM32 = snapshot.projection.values[3][2];
    record.projectionM33 = snapshot.projection.values[3][3];
    record.drawCount = 1;
    record.primitiveCount = primitiveCount;
    MemoryBarrier();
    InterlockedIncrement(&g_frame.provenanceCount);
}
