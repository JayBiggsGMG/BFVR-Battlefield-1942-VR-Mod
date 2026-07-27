#include "client/D3D8StereoProbeRecords.h"

namespace bfvr::d3d8probe
{

void ResetStereoFrameRecordForResourceReuse(
    StereoFrameRecord& record) noexcept
{
    void* const colors[2] = {
        record.ownedColor[0],
        record.ownedColor[1]};
    void* const depths[2] = {
        record.ownedDepth[0],
        record.ownedDepth[1]};
    void* const menuColor = record.menuColor;
    void* const menuDepth = record.menuDepth;
    void* const readbacks[3] = {
        record.reusableReadback[0],
        record.reusableReadback[1],
        record.reusableReadback[2]};
    const D3DSurfaceDescription colorDescription =
        record.colorDescription;
    const D3DSurfaceDescription depthDescription =
        record.depthDescription;
    const D3DSurfaceDescription menuColorDescription =
        record.menuColorDescription;
    const D3DSurfaceDescription readbackDescriptions[3] = {
        record.reusableReadbackDescription[0],
        record.reusableReadbackDescription[1],
        record.reusableReadbackDescription[2]};

    record = StereoFrameRecord{};
    record.ownedColor[0] = colors[0];
    record.ownedColor[1] = colors[1];
    record.ownedDepth[0] = depths[0];
    record.ownedDepth[1] = depths[1];
    record.menuColor = menuColor;
    record.menuDepth = menuDepth;
    record.reusableReadback[0] = readbacks[0];
    record.reusableReadback[1] = readbacks[1];
    record.reusableReadback[2] = readbacks[2];
    record.colorDescription = colorDescription;
    record.depthDescription = depthDescription;
    record.menuColorDescription = menuColorDescription;
    record.reusableReadbackDescription[0] = readbackDescriptions[0];
    record.reusableReadbackDescription[1] = readbackDescriptions[1];
    record.reusableReadbackDescription[2] = readbackDescriptions[2];
}

void ResetStereoPairAttemptRecord(
    StereoPairRecord& record) noexcept
{
    record.primitiveType = 0;
    record.minimumVertexIndex = 0;
    record.vertexCount = 0;
    record.startIndex = 0;
    record.primitiveCount = 0;
    record.colorDescription = {};
    record.depthDescription = {};
    record.originalViewport = {};
    record.originalView = {};
    record.originalProjection = {};
    record.leftView = {};
    record.rightView = {};
    record.leftProjection = {};
    record.rightProjection = {};
    record.leftDrawResult = E_FAIL;
    record.rightDrawResult = E_FAIL;
    record.restoreTargetResult = E_FAIL;
    record.restoreViewportResult = E_FAIL;
    record.restoreViewResult = E_FAIL;
    record.restoreProjectionResult = E_FAIL;
    record.targetRestored = FALSE;
    record.depthRestored = FALSE;
    record.viewportRestored = FALSE;
    record.viewRestored = FALSE;
    record.projectionRestored = FALSE;
    record.allStateRestored = FALSE;
    record.pairDrawn = FALSE;
    record.readback[0] = {};
    record.readback[1] = {};
    record.ownedColorRelease[0] = static_cast<ULONG>(-1);
    record.ownedColorRelease[1] = static_cast<ULONG>(-1);
    record.ownedDepthRelease[0] = static_cast<ULONG>(-1);
    record.ownedDepthRelease[1] = static_cast<ULONG>(-1);
    record.sourceColorRelease = static_cast<ULONG>(-1);
    record.sourceDepthRelease = static_cast<ULONG>(-1);
}

void CountStereoFrameDrawPolicy(
    StereoFrameRecord& record,
    stereo::D3D8DrawPolicy policy) noexcept
{
    switch (policy)
    {
    case stereo::D3D8DrawPolicy::StereoPerspective:
        InterlockedIncrement(&record.stereoPerspectiveDraws);
        break;
    case stereo::D3D8DrawPolicy::MonoPretransformed:
        InterlockedIncrement(&record.monoPretransformedDraws);
        break;
    case stereo::D3D8DrawPolicy::MonoNonPerspective:
        InterlockedIncrement(&record.monoNonPerspectiveDraws);
        break;
    }
}

void CountStereoFrameSemanticDraw(
    StereoFrameRecord& record,
    stereo::D3D8SemanticDrawClass semanticClass) noexcept
{
    switch (semanticClass)
    {
    case stereo::D3D8SemanticDrawClass::SkyboxCubeFace:
        InterlockedIncrement(&record.skyboxCubeFaceDraws);
        break;
    case stereo::D3D8SemanticDrawClass::BillboardBatch:
        InterlockedIncrement(&record.billboardBatchDraws);
        break;
    case stereo::D3D8SemanticDrawClass::TreeMeshAlphaBlock:
        InterlockedIncrement(&record.treeMeshAlphaBlockDraws);
        break;
    case stereo::D3D8SemanticDrawClass::TreeMeshProgrammableSprite:
        InterlockedIncrement(&record.treeMeshProgrammableSpriteDraws);
        break;
    case stereo::D3D8SemanticDrawClass::AnimatedMeshSkinning:
        InterlockedIncrement(&record.animatedMeshSkinningDraws);
        break;
    case stereo::D3D8SemanticDrawClass::TranslucentSprite:
        InterlockedIncrement(&record.translucentSpriteDraws);
        break;
    case stereo::D3D8SemanticDrawClass::Ref2FontGlyphBatch:
        InterlockedIncrement(&record.ref2FontGlyphBatchDraws);
        break;
    case stereo::D3D8SemanticDrawClass::Ref2MenuQuad:
        InterlockedIncrement(&record.ref2MenuQuadDraws);
        break;
    case stereo::D3D8SemanticDrawClass::Unclassified:
        break;
    }
}

void AccumulateContinuousPresentationFrame(
    PresentationRunRecord& run,
    const StereoFrameRecord& frame,
    LONG sequence) noexcept
{
    InterlockedIncrement(&run.publishedFrames);
    InterlockedIncrement(&run.presentedFrames);
    InterlockedExchangeAdd(&run.totalDraws, frame.mirroredDraws);
    InterlockedExchangeAdd(&run.totalWorldDraws, frame.worldEyeDraws);
    InterlockedExchangeAdd(&run.totalUiDraws, frame.menuLayerDraws);
    InterlockedExchangeAdd(&run.totalRestoreChecks, frame.restoreChecks);
    InterlockedExchangeAdd(&run.totalRestoreFailures, frame.restoreFailures);
    InterlockedExchangeAdd(
        &run.totalTreeRendererBillboardDraws,
        frame.billboardBatchDraws);
    InterlockedExchangeAdd(
        &run.totalTreeMeshAlphaBlockDraws,
        frame.treeMeshAlphaBlockDraws);
    InterlockedExchangeAdd(
        &run.totalTreeMeshProgrammableSpriteDraws,
        frame.treeMeshProgrammableSpriteDraws);
    InterlockedExchangeAdd(
        &run.totalAnimatedMeshSkinningDraws,
        frame.animatedMeshSkinningDraws);
    InterlockedExchangeAdd(
        &run.totalSuppressedFirstPersonArmDraws,
        frame.suppressedFirstPersonArmDraws);
    InterlockedExchangeAdd(
        &run.totalTranslucentSpriteDraws,
        frame.translucentSpriteDraws);
    run.totalReplayQpcTicks += frame.replayQpcTicks;
    run.totalReadbackQpcTicks += frame.readbackQpcTicks;
    run.totalUploadQpcTicks += frame.uploadQpcTicks;
    if (run.firstSequence == 0)
    {
        run.firstSequence = sequence;
    }
    run.lastSequence = sequence;
}

} // namespace bfvr::d3d8probe
