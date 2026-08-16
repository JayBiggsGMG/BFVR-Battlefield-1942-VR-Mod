#include "client/D3D8StereoProbeReporting.h"

#include <algorithm>
#include <array>

namespace bfvr::d3d8probe
{

const wchar_t* DescribeDrawKind(FrameDrawKind kind)
{
    switch (kind)
    {
    case FrameDrawKind::Primitive:
        return L"DrawPrimitive";
    case FrameDrawKind::IndexedPrimitive:
        return L"DrawIndexedPrimitive";
    case FrameDrawKind::PrimitiveUP:
        return L"DrawPrimitiveUP";
    case FrameDrawKind::IndexedPrimitiveUP:
        return L"DrawIndexedPrimitiveUP";
    default:
        return L"unknown";
    }
}

const wchar_t* DescribeDrawPolicy(stereo::D3D8DrawPolicy policy)
{
    switch (policy)
    {
    case stereo::D3D8DrawPolicy::StereoPerspective:
        return L"stereo-perspective";
    case stereo::D3D8DrawPolicy::MonoPretransformed:
        return L"mono-pretransformed";
    case stereo::D3D8DrawPolicy::MonoNonPerspective:
        return L"mono-non-perspective";
    default:
        return L"unknown";
    }
}

const wchar_t* DescribeSemanticClass(
    stereo::D3D8SemanticDrawClass semanticClass)
{
    if (semanticClass == stereo::D3D8SemanticDrawClass::SkyboxCubeFace)
    {
        return L"skybox-cube-face";
    }
    if (semanticClass == stereo::D3D8SemanticDrawClass::BillboardBatch)
    {
        return L"tree-renderer-billboards";
    }
    if (semanticClass == stereo::D3D8SemanticDrawClass::TreeMeshAlphaBlock)
    {
        return L"tree-mesh-alpha-block";
    }
    if (semanticClass ==
        stereo::D3D8SemanticDrawClass::TreeMeshProgrammableSprite)
    {
        return L"tree-mesh-programmable-sprite";
    }
    if (semanticClass == stereo::D3D8SemanticDrawClass::AnimatedMeshSkinning)
    {
        return L"animated-mesh-skinning";
    }
    if (semanticClass == stereo::D3D8SemanticDrawClass::ProjectedTerrainShadow)
    {
        return L"projected-terrain-shadow";
    }
    if (semanticClass == stereo::D3D8SemanticDrawClass::WaterSurface)
    {
        return L"water-surface";
    }
    if (semanticClass == stereo::D3D8SemanticDrawClass::TranslucentSprite)
    {
        return L"translucent-sprite";
    }
    if (semanticClass ==
        stereo::D3D8SemanticDrawClass::Ref2FontGlyphBatch)
    {
        return L"ref2-font-glyph-batch";
    }
    return semanticClass == stereo::D3D8SemanticDrawClass::Ref2MenuQuad
        ? L"ref2-menu-quad"
        : L"unclassified";
}

void ReportStereoPairResult(
    FormattedLogCallback appendLog,
    StereoPairRecord& record,
    float diagnosticHalfEyeOffset,
    float diagnosticConvergenceDistance)
{
    appendLog(
        L"D3D8 stereo-pair result: state=%ld rejectedCandidates=%ld emptyOrIdenticalCandidates=%ld device=%p thread=%lu primitive[type=%lu minVertex=%u vertices=%u startIndex=%u count=%u] sourceColor[format=%u size=%ux%u ms=%u] sourceDepth[format=%u size=%ux%u ms=%u].",
        InterlockedCompareExchange(&record.state, 0, 0),
        InterlockedCompareExchange(&record.rejectedCandidates, 0, 0),
        InterlockedCompareExchange(&record.emptyOrIdenticalCandidates, 0, 0),
        record.device,
        record.deviceThreadId,
        static_cast<unsigned long>(record.primitiveType),
        record.minimumVertexIndex,
        record.vertexCount,
        record.startIndex,
        record.primitiveCount,
        record.colorDescription.format,
        record.colorDescription.width,
        record.colorDescription.height,
        record.colorDescription.multiSampleType,
        record.depthDescription.format,
        record.depthDescription.width,
        record.depthDescription.height,
        record.depthDescription.multiSampleType);
    appendLog(
        L"D3D8 stereo-pair draws: game=0x%08lX left=0x%08lX right=0x%08lX pairDrawn=%d diagnosticHalfEye=%.3f convergence=%.3f leftViewX=%.6f rightViewX=%.6f leftProjectionCenter=%.6f rightProjectionCenter=%.6f.",
        static_cast<unsigned long>(record.originalDrawResult),
        static_cast<unsigned long>(record.leftDrawResult),
        static_cast<unsigned long>(record.rightDrawResult),
        record.pairDrawn,
        diagnosticHalfEyeOffset,
        diagnosticConvergenceDistance,
        record.leftView.values[3][0],
        record.rightView.values[3][0],
        record.leftProjection.values[2][0],
        record.rightProjection.values[2][0]);
    appendLog(
        L"D3D8 stereo-pair restoration: SetRenderTarget=0x%08lX SetViewport=0x%08lX SetView=0x%08lX SetProjection=0x%08lX exact[target=%d depth=%d viewport=%d view=%d projection=%d all=%d] releases[sourceColor=%lu sourceDepth=%lu ownedColor=%lu/%lu ownedDepth=%lu/%lu].",
        static_cast<unsigned long>(record.restoreTargetResult),
        static_cast<unsigned long>(record.restoreViewportResult),
        static_cast<unsigned long>(record.restoreViewResult),
        static_cast<unsigned long>(record.restoreProjectionResult),
        record.targetRestored,
        record.depthRestored,
        record.viewportRestored,
        record.viewRestored,
        record.projectionRestored,
        record.allStateRestored,
        record.sourceColorRelease,
        record.sourceDepthRelease,
        record.ownedColorRelease[0],
        record.ownedColorRelease[1],
        record.ownedDepthRelease[0],
        record.ownedDepthRelease[1]);
    appendLog(
        L"D3D8 stereo-pair readback: left[create=0x%08lX copy=0x%08lX lock=0x%08lX unlock=0x%08lX release=%lu hash=%016llX nonClear=%llu] right[create=0x%08lX copy=0x%08lX lock=0x%08lX unlock=0x%08lX release=%lu hash=%016llX nonClear=%llu] hashesDiffer=%d. The targets were never presented or exposed to BF1942.",
        static_cast<unsigned long>(record.readback[0].createResult),
        static_cast<unsigned long>(record.readback[0].copyResult),
        static_cast<unsigned long>(record.readback[0].lockResult),
        static_cast<unsigned long>(record.readback[0].unlockResult),
        record.readback[0].releaseResult,
        static_cast<unsigned long long>(record.readback[0].hash),
        static_cast<unsigned long long>(record.readback[0].nonClearPixels),
        static_cast<unsigned long>(record.readback[1].createResult),
        static_cast<unsigned long>(record.readback[1].copyResult),
        static_cast<unsigned long>(record.readback[1].lockResult),
        static_cast<unsigned long>(record.readback[1].unlockResult),
        record.readback[1].releaseResult,
        static_cast<unsigned long long>(record.readback[1].hash),
        static_cast<unsigned long long>(record.readback[1].nonClearPixels),
        record.readback[0].hash != record.readback[1].hash);
}

void ReportStereoFrameResult(
    FormattedLogCallback appendLog,
    StereoPairRecord& record,
    StereoFrameRecord& frame)
{
    appendLog(
        L"D3D8 full-draw-frame stereo result: state=%ld device=%p thread=%lu mirroredDraws=%ld mirroredPrimitives=%ld excludedTargetDraws=%ld boundedSkips=%ld restoreChecks=%ld restoreVerifications=%ld restoreFailures=%ld allRestorationsAccepted=%d sourceReleaseChecks=%ld sourceReleaseFailures=%ld resetAborted=%ld resetResult=0x%08lX.",
        InterlockedCompareExchange(&record.state, 0, 0),
        record.device,
        record.deviceThreadId,
        InterlockedCompareExchange(&frame.mirroredDraws, 0, 0),
        InterlockedCompareExchange(&frame.mirroredPrimitives, 0, 0),
        InterlockedCompareExchange(&frame.excludedTargetDraws, 0, 0),
        InterlockedCompareExchange(&frame.boundedDrawSkips, 0, 0),
        InterlockedCompareExchange(&frame.restoreChecks, 0, 0),
        InterlockedCompareExchange(&frame.restoreVerifications, 0, 0),
        InterlockedCompareExchange(&frame.restoreFailures, 0, 0),
        frame.allRestorationsAccepted,
        InterlockedCompareExchange(&frame.sourceReleaseChecks, 0, 0),
        InterlockedCompareExchange(&frame.sourceReleaseFailures, 0, 0),
        InterlockedCompareExchange(&frame.resetAborted, 0, 0),
        static_cast<unsigned long>(frame.resetResult));
    appendLog(
        L"D3D8 full-draw-frame families: mirrored[DrawPrimitive=%ld DrawIndexedPrimitive=%ld DrawPrimitiveUP=%ld DrawIndexedPrimitiveUP=%ld] excluded[DrawPrimitive=%ld DrawIndexedPrimitive=%ld DrawPrimitiveUP=%ld DrawIndexedPrimitiveUP=%ld].",
        InterlockedCompareExchange(&frame.mirroredByKind[0], 0, 0),
        InterlockedCompareExchange(&frame.mirroredByKind[1], 0, 0),
        InterlockedCompareExchange(&frame.mirroredByKind[2], 0, 0),
        InterlockedCompareExchange(&frame.mirroredByKind[3], 0, 0),
        InterlockedCompareExchange(&frame.excludedByKind[0], 0, 0),
        InterlockedCompareExchange(&frame.excludedByKind[1], 0, 0),
        InterlockedCompareExchange(&frame.excludedByKind[2], 0, 0),
        InterlockedCompareExchange(&frame.excludedByKind[3], 0, 0));
    appendLog(
        L"D3D8 full-draw-frame transform policy: stereoPerspective=%ld monoPretransformed=%ld monoNonPerspective=%ld skyboxCubeFaces=%ld billboardBatches=%ld treeMeshAlphaBlocks=%ld treeMeshProgrammableSprites=%ld animatedMeshSkinning=%ld projectedTerrainShadows=%ld projectedShadowTextureEyes=%ld projectedShadowTextureFailures=%ld waterSurfaces=%ld stereoStableWaterReflections=%ld waterReflectionStateFailures=%ld waterTextureBasisEyes=%ld waterTextureBasisFailures=%ld translucentSprites=%ld ref2FontGlyphBatches=%ld ref2MenuQuads=%ld vertexShaderReadFailures=%ld skinningPrepareFailures=%ld skinningSourceMismatches=%ld skinningApplyFailures=%ld spritePrepareFailures=%ld spriteSourceMismatches=%ld spriteApplyFailures=%ld treeSpritePrepareFailures=%ld treeSpriteSourceMismatches=%ld treeSpriteApplyFailures=%ld renderStateReadFailures=%ld provenanceSites=%ld/%zu provenanceOverflow=%ld. Exact PatchCellBlock shadow applications receive a per-eye stage-0 texture transform that preserves BF1942's authored world-space projection under the replay View. The exact WinPC additive water pass keeps Battlefield's native normal-map, generated-light, SpecularColor, SpecularStreakFactor, and LOCALVIEWER response. It uses the current head-centre material View in both eyes and folds each residual eye View into Projection, which preserves the original per-eye clip geometry exactly while removing eye-dependent reflection coordinates. Set BFVR_STEREO_WATER_REFLECTION=1 to restore the fully legacy per-eye camera-relative reflection path; exact SkinningShader2Bones draws receive per-eye c0-c3 world-view-projection constants, exact translated TranslucentBucketDB sprites receive per-eye c0-c7 plus c9 camera constants, and exact TreeMesh programmable sprites receive per-eye c0-c7 constants. All receive required restoration; deep diagnostics additionally reads the restored registers back for exact verification.",
        InterlockedCompareExchange(&frame.stereoPerspectiveDraws, 0, 0),
        InterlockedCompareExchange(&frame.monoPretransformedDraws, 0, 0),
        InterlockedCompareExchange(&frame.monoNonPerspectiveDraws, 0, 0),
        InterlockedCompareExchange(&frame.skyboxCubeFaceDraws, 0, 0),
        InterlockedCompareExchange(&frame.billboardBatchDraws, 0, 0),
        InterlockedCompareExchange(&frame.treeMeshAlphaBlockDraws, 0, 0),
        InterlockedCompareExchange(
            &frame.treeMeshProgrammableSpriteDraws,
            0,
            0),
        InterlockedCompareExchange(&frame.animatedMeshSkinningDraws, 0, 0),
        InterlockedCompareExchange(&frame.projectedTerrainShadowDraws, 0, 0),
        InterlockedCompareExchange(
            &frame.projectedShadowTextureEyeApplications,
            0,
            0),
        InterlockedCompareExchange(
            &frame.projectedShadowTextureFailures,
            0,
            0),
        InterlockedCompareExchange(&frame.waterSurfaceDraws, 0, 0),
        InterlockedCompareExchange(&frame.stereoStableWaterReflectionDraws, 0, 0),
        InterlockedCompareExchange(&frame.waterReflectionStateFailures, 0, 0),
        InterlockedCompareExchange(&frame.waterTextureBasisEyeApplications, 0, 0),
        InterlockedCompareExchange(&frame.waterTextureBasisFailures, 0, 0),
        InterlockedCompareExchange(&frame.translucentSpriteDraws, 0, 0),
        InterlockedCompareExchange(&frame.ref2FontGlyphBatchDraws, 0, 0),
        InterlockedCompareExchange(&frame.ref2MenuQuadDraws, 0, 0),
        InterlockedCompareExchange(&frame.vertexShaderReadFailures, 0, 0),
        InterlockedCompareExchange(&frame.skinningShaderPrepareFailures, 0, 0),
        InterlockedCompareExchange(&frame.skinningShaderSourceMismatches, 0, 0),
        InterlockedCompareExchange(&frame.skinningShaderApplyFailures, 0, 0),
        InterlockedCompareExchange(&frame.spriteShaderPrepareFailures, 0, 0),
        InterlockedCompareExchange(&frame.spriteShaderSourceMismatches, 0, 0),
        InterlockedCompareExchange(&frame.spriteShaderApplyFailures, 0, 0),
        InterlockedCompareExchange(
            &frame.treeSpriteShaderPrepareFailures,
            0,
            0),
        InterlockedCompareExchange(
            &frame.treeSpriteShaderSourceMismatches,
            0,
            0),
        InterlockedCompareExchange(
            &frame.treeSpriteShaderApplyFailures,
            0,
            0),
        InterlockedCompareExchange(&frame.renderStateReadFailures, 0, 0),
        InterlockedCompareExchange(&frame.provenanceCount, 0, 0),
        kMaximumProvenanceSites,
        InterlockedCompareExchange(&frame.provenanceOverflow, 0, 0));
    appendLog(
        L"D3D8 full-draw-frame composition: worldEyeDraws=%ld uiLayerDraws=%ld partitionExact=%d. All monoscopic pretransformed/non-perspective draws, including confirmed Ref2 font and menu-quad families, are omitted from both world-eye targets and replayed once into the separate transparent UI layer.",
        InterlockedCompareExchange(&frame.worldEyeDraws, 0, 0),
        InterlockedCompareExchange(&frame.menuLayerDraws, 0, 0),
        InterlockedCompareExchange(&frame.mirroredDraws, 0, 0) ==
            InterlockedCompareExchange(&frame.worldEyeDraws, 0, 0) +
            InterlockedCompareExchange(&frame.menuLayerDraws, 0, 0));
    appendLog(
        L"D3D8 water-mask transport: alphaOnlyDraws=%ld failures=%ld frameMaskValid=%d. The mask admits only the exact additive WaterSurface pass, preserves its material alpha, and is carried in packed-depth alpha while RGB depth is resolved independently.",
        InterlockedCompareExchange(&frame.waterMaskDraws, 0, 0),
        InterlockedCompareExchange(&frame.waterMaskFailures, 0, 0),
        frame.waterMaskValid);
    const LONG provenanceCount =
        InterlockedCompareExchange(&frame.provenanceCount, 0, 0);
    for (LONG index = 0; index < provenanceCount; ++index)
    {
        const DrawProvenanceRecord& provenance = frame.provenance[index];
        appendLog(
            L"D3D8 provenance[%ld]: family=%s policy=%s semantic=%s gameStack=[%p,%p,%p,%p] depth=%u primitiveType=%lu fvfOrShader=0x%08lX originalShader[hash=%016llX bytes=%lu ordinal=%lu] view[hash=%016llX t=(%.4f,%.4f,%.4f)] projection[hash=%016llX m00=%.4f m11=%.4f m20=%.4f m21=%.4f m22=%.6f m23=%.6f m32=%.6f m33=%.6f] states[z=%lu zWrite=%lu alphaBlend=%lu fog=%lu lighting=%lu] draws=%ld primitives=%ld.",
            index,
            DescribeDrawKind(provenance.kind),
            DescribeDrawPolicy(provenance.policy),
            DescribeSemanticClass(provenance.semanticClass),
            provenance.gameStack[0],
            provenance.gameStack[1],
            provenance.gameStack[2],
            provenance.gameStack[3],
            provenance.gameStackDepth,
            static_cast<unsigned long>(provenance.primitiveType),
            static_cast<unsigned long>(provenance.vertexShaderOrFvf),
            static_cast<unsigned long long>(
                provenance.originalVertexShaderHash),
            static_cast<unsigned long>(
                provenance.originalVertexShaderByteCount),
            static_cast<unsigned long>(
                provenance.vertexShaderCreationOrdinal),
            static_cast<unsigned long long>(provenance.viewHash),
            provenance.viewM30,
            provenance.viewM31,
            provenance.viewM32,
            static_cast<unsigned long long>(provenance.projectionHash),
            provenance.projectionM00,
            provenance.projectionM11,
            provenance.projectionM20,
            provenance.projectionM21,
            provenance.projectionM22,
            provenance.projectionM23,
            provenance.projectionM32,
            provenance.projectionM33,
            static_cast<unsigned long>(provenance.zEnable),
            static_cast<unsigned long>(provenance.zWriteEnable),
            static_cast<unsigned long>(provenance.alphaBlendEnable),
            static_cast<unsigned long>(provenance.fogEnable),
            static_cast<unsigned long>(provenance.lighting),
            provenance.drawCount,
            provenance.primitiveCount);
    }
    appendLog(
        L"D3D8 full-draw-frame targets: color[format=%u size=%ux%u ms=%u] depth[format=%u size=%ux%u ms=%u] lastDraw[left=0x%08lX right=0x%08lX] releases[color=%lu/%lu depth=%lu/%lu].",
        frame.colorDescription.format,
        frame.colorDescription.width,
        frame.colorDescription.height,
        frame.colorDescription.multiSampleType,
        frame.depthDescription.format,
        frame.depthDescription.width,
        frame.depthDescription.height,
        frame.depthDescription.multiSampleType,
        static_cast<unsigned long>(frame.lastLeftDrawResult),
        static_cast<unsigned long>(frame.lastRightDrawResult),
        frame.ownedColorRelease[0],
        frame.ownedColorRelease[1],
        frame.ownedDepthRelease[0],
        frame.ownedDepthRelease[1]);
    appendLog(
        L"D3D8 UI-layer target: color[format=%u size=%ux%u ms=%u] lastDraw=0x%08lX releases[color=%lu depth=%lu].",
        frame.menuColorDescription.format,
        frame.menuColorDescription.width,
        frame.menuColorDescription.height,
        frame.menuColorDescription.multiSampleType,
        static_cast<unsigned long>(frame.lastMenuDrawResult),
        frame.menuColorRelease,
        frame.menuDepthRelease);
    const ULONG leftReadbackRelease =
        frame.readback[0].releaseResult != static_cast<ULONG>(-1)
        ? frame.readback[0].releaseResult
        : frame.reusableReadbackRelease[0];
    const ULONG rightReadbackRelease =
        frame.readback[1].releaseResult != static_cast<ULONG>(-1)
        ? frame.readback[1].releaseResult
        : frame.reusableReadbackRelease[1];
    const ULONG menuReadbackRelease =
        frame.menuReadback.releaseResult != static_cast<ULONG>(-1)
        ? frame.menuReadback.releaseResult
        : frame.reusableReadbackRelease[2];
    appendLog(
        L"D3D8 full-draw-frame readback: left[create=0x%08lX copy=0x%08lX lock=0x%08lX unlock=0x%08lX release=%lu hash=%016llX nonClear=%llu] right[create=0x%08lX copy=0x%08lX lock=0x%08lX unlock=0x%08lX release=%lu hash=%016llX nonClear=%llu] hashesDiffer=%d complete=%d. RTT-sized/depthless targets were excluded and neither eye target was presented to BF1942.",
        static_cast<unsigned long>(frame.readback[0].createResult),
        static_cast<unsigned long>(frame.readback[0].copyResult),
        static_cast<unsigned long>(frame.readback[0].lockResult),
        static_cast<unsigned long>(frame.readback[0].unlockResult),
        leftReadbackRelease,
        static_cast<unsigned long long>(frame.readback[0].hash),
        static_cast<unsigned long long>(frame.readback[0].nonClearPixels),
        static_cast<unsigned long>(frame.readback[1].createResult),
        static_cast<unsigned long>(frame.readback[1].copyResult),
        static_cast<unsigned long>(frame.readback[1].lockResult),
        static_cast<unsigned long>(frame.readback[1].unlockResult),
        rightReadbackRelease,
        static_cast<unsigned long long>(frame.readback[1].hash),
        static_cast<unsigned long long>(frame.readback[1].nonClearPixels),
        frame.readback[0].hash != frame.readback[1].hash,
        frame.completedWithDifferingColor);
    appendLog(
        L"D3D8 UI-layer readback: create=0x%08lX copy=0x%08lX lock=0x%08lX unlock=0x%08lX release=%lu hash=%016llX nonClear=%llu nonZeroAlpha=%llu.",
        static_cast<unsigned long>(frame.menuReadback.createResult),
        static_cast<unsigned long>(frame.menuReadback.copyResult),
        static_cast<unsigned long>(frame.menuReadback.lockResult),
        static_cast<unsigned long>(frame.menuReadback.unlockResult),
        menuReadbackRelease,
        static_cast<unsigned long long>(frame.menuReadback.hash),
        static_cast<unsigned long long>(frame.menuReadback.nonClearPixels),
        static_cast<unsigned long long>(frame.menuReadback.nonZeroAlphaPixels));
}

std::int64_t ReadPerformanceCounter() noexcept
{
    LARGE_INTEGER counter = {};
    return QueryPerformanceCounter(&counter)
        ? counter.QuadPart
        : 0;
}

bool IsContinuousPresentationTimingReportDue(
    DWORD now,
    DWORD& lastReportAt) noexcept
{
    constexpr DWORD kTimingReportPeriodMs = 30000;
    if (lastReportAt == 0)
    {
        lastReportAt = now;
        return false;
    }
    if (now - lastReportAt < kTimingReportPeriodMs)
    {
        return false;
    }
    lastReportAt = now;
    return true;
}

void ReportContinuousPresentationResult(
    FormattedLogCallback appendLog,
    const PresentationRunRecord& run,
    UINT worldWidth,
    UINT worldHeight)
{
    appendLog(
        L"D3D8 OpenXR continuous handoff: sequences=%ld..%ld nativeWorld=%ux%u published=%ld presented=%ld failed=%ld draws=%ld world=%ld UI=%ld restorationWrites=%ld/%ld deepVerifications=%ld. Runtime centre-head pose drives RenderView while residual eye poses and asymmetric FOV drive D3D8 replay.",
        run.firstSequence,
        run.lastSequence,
        worldWidth,
        worldHeight,
        run.publishedFrames,
        run.presentedFrames,
        run.failedFrames,
        run.totalDraws,
        run.totalWorldDraws,
        run.totalUiDraws,
        run.totalRestoreChecks - run.totalRestoreFailures,
        run.totalRestoreChecks,
        run.totalRestoreVerifications);
    appendLog(
        L"D3D8 OpenXR session geometry families: treeRendererBillboards=%ld treeMeshAlphaBlocks=%ld treeMeshProgrammableSprites=%ld animatedMeshSkinning=%ld projectedTerrainShadows=%ld firstPersonArmsSuppressed=%ld translucentSprites=%ld. These are accumulated across every presented world frame, including frames before a menu exit.",
        run.totalTreeRendererBillboardDraws,
        run.totalTreeMeshAlphaBlockDraws,
        run.totalTreeMeshProgrammableSpriteDraws,
        run.totalAnimatedMeshSkinningDraws,
        run.totalProjectedTerrainShadowDraws,
        run.totalSuppressedFirstPersonArmDraws,
        run.totalTranslucentSpriteDraws);

    LARGE_INTEGER frequency = {};
    if (!QueryPerformanceFrequency(&frequency) ||
        frequency.QuadPart <= 0 ||
        run.presentedFrames <= 0)
    {
        return;
    }
    const double millisecondsPerTick =
        1000.0 / static_cast<double>(frequency.QuadPart);
    const double frameCount = static_cast<double>(run.presentedFrames);
    const LONG originalPresentCalls = run.originalPresentCalls;
    const double presentCallCount = static_cast<double>(
        originalPresentCalls > 0 ? originalPresentCalls : 1);
    appendLog(
        run.gpuResidentTransport
            ? L"D3D8 OpenXR GPU-resident stage timing: replay=%.3f ms/frame readback=%.3f ms/frame gpuSyncPublish=%.3f ms/frame consumeWait=%.3f ms/frame nextRequestWait=%.3f ms/frame."
            : L"D3D8 OpenXR stage timing: replay=%.3f ms/frame readback=%.3f ms/frame upload=%.3f ms/frame consumeWait=%.3f ms/frame nextRequestWait=%.3f ms/frame.",
        static_cast<double>(run.totalReplayQpcTicks) *
            millisecondsPerTick / frameCount,
        static_cast<double>(run.totalReadbackQpcTicks) *
            millisecondsPerTick / frameCount,
        static_cast<double>(run.totalUploadQpcTicks) *
            millisecondsPerTick / frameCount,
        static_cast<double>(run.totalConsumptionWaitQpcTicks) *
            millisecondsPerTick / frameCount,
        static_cast<double>(run.totalRequestWaitQpcTicks) *
            millisecondsPerTick / frameCount);
    appendLog(
        L"D3D8 OpenXR replay detail: prepare=%.3f ms/frame eyeOrUiDraw=%.3f ms/frame restoreWrites=%.3f ms/frame deepRestoreReadback=%.3f ms/frame deepProvenance=%.3f ms/frame nativePresent=%.3f ms/call (%ld calls). Normal diagnostics always restores state but skips the two deep proof-readback stages.",
        static_cast<double>(run.totalPreparationQpcTicks) *
            millisecondsPerTick / frameCount,
        static_cast<double>(run.totalEyeOrLayerDrawQpcTicks) *
            millisecondsPerTick / frameCount,
        static_cast<double>(run.totalRestoreWriteQpcTicks) *
            millisecondsPerTick / frameCount,
        static_cast<double>(run.totalRestoreVerifyQpcTicks) *
            millisecondsPerTick / frameCount,
        static_cast<double>(run.totalProvenanceQpcTicks) *
            millisecondsPerTick / frameCount,
        static_cast<double>(run.totalOriginalPresentQpcTicks) *
            millisecondsPerTick / presentCallCount,
        originalPresentCalls);

    if (run.framePacingSamplesStored != 0)
    {
        std::array<std::int64_t, kFramePacingSampleCount> samples = {};
        std::copy_n(
            run.framePacingQpcTicks,
            run.framePacingSamplesStored,
            samples.begin());
        std::sort(
            samples.begin(),
            samples.begin() + run.framePacingSamplesStored);
        const auto percentile = [&samples, &run](double fraction)
        {
            const std::size_t index = static_cast<std::size_t>(
                fraction * static_cast<double>(run.framePacingSamplesStored - 1));
            return samples[index];
        };
        appendLog(
            L"D3D8 OpenXR new-frame pacing over the latest %zu intervals: median=%.3f ms p95=%.3f ms p99=%.3f ms worst=%.3f ms. These intervals measure completed new BFVR stereo frames, not vanilla BF1942's frame rate.",
            run.framePacingSamplesStored,
            static_cast<double>(percentile(0.50)) * millisecondsPerTick,
            static_cast<double>(percentile(0.95)) * millisecondsPerTick,
            static_cast<double>(percentile(0.99)) * millisecondsPerTick,
            static_cast<double>(samples[run.framePacingSamplesStored - 1]) *
                millisecondsPerTick);
    }
}

} // namespace bfvr::d3d8probe
