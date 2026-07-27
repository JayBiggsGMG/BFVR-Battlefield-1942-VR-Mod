#pragma once

#include "stereo/D3D8DrawPolicy.h"
#include "stereo/D3D8SemanticDrawPolicy.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace bfvr::d3d8probe
{

constexpr LONG kMaximumProvenanceSites = 64;
constexpr std::size_t kProvenanceStackDepth = 4;

struct D3DMatrix
{
    float values[4][4];
};
static_assert(sizeof(D3DMatrix) == sizeof(float) * 16);

struct D3DViewport
{
    DWORD x;
    DWORD y;
    DWORD width;
    DWORD height;
    float minZ;
    float maxZ;
};
static_assert(sizeof(D3DViewport) == 24);

struct D3DSurfaceDescription
{
    UINT format;
    UINT type;
    DWORD usage;
    UINT pool;
    UINT size;
    UINT multiSampleType;
    UINT width;
    UINT height;
};
static_assert(sizeof(D3DSurfaceDescription) == sizeof(UINT) * 8);

enum class FrameDrawKind : std::size_t
{
    Primitive,
    IndexedPrimitive,
    PrimitiveUP,
    IndexedPrimitiveUP,
    Count
};

struct DrawProvenanceRecord
{
    void* gameStack[kProvenanceStackDepth] = {};
    UINT gameStackDepth = 0;
    FrameDrawKind kind = FrameDrawKind::Primitive;
    stereo::D3D8DrawPolicy policy =
        stereo::D3D8DrawPolicy::MonoNonPerspective;
    stereo::D3D8SemanticDrawClass semanticClass =
        stereo::D3D8SemanticDrawClass::Unclassified;
    DWORD primitiveType = 0;
    DWORD vertexShaderOrFvf = 0;
    std::uint64_t originalVertexShaderHash = 0;
    DWORD originalVertexShaderByteCount = 0;
    DWORD vertexShaderCreationOrdinal = 0;
    DWORD zEnable = 0;
    DWORD zWriteEnable = 0;
    DWORD alphaBlendEnable = 0;
    DWORD fogEnable = 0;
    DWORD lighting = 0;
    std::uint64_t viewHash = 0;
    std::uint64_t projectionHash = 0;
    float viewM30 = 0.0F;
    float viewM31 = 0.0F;
    float viewM32 = 0.0F;
    float projectionM00 = 0.0F;
    float projectionM11 = 0.0F;
    float projectionM20 = 0.0F;
    float projectionM21 = 0.0F;
    float projectionM22 = 0.0F;
    float projectionM23 = 0.0F;
    float projectionM32 = 0.0F;
    float projectionM33 = 0.0F;
    LONG drawCount = 0;
    LONG primitiveCount = 0;
};

struct ReadbackResult
{
    HRESULT createResult = E_FAIL;
    HRESULT copyResult = E_FAIL;
    HRESULT lockResult = E_FAIL;
    HRESULT unlockResult = E_FAIL;
    ULONG releaseResult = static_cast<ULONG>(-1);
    std::uint64_t hash = 0;
    std::uint64_t nonClearPixels = 0;
    std::uint64_t nonZeroAlphaPixels = 0;
};

struct StereoPairRecord
{
    volatile LONG state = 0;
    volatile LONG activeCallbacks = 0;
    volatile LONG rejectedCandidates = 0;
    volatile LONG emptyOrIdenticalCandidates = 0;
    void* device = nullptr;
    DWORD deviceThreadId = 0;
    void* resetTarget = nullptr;
    void* presentTarget = nullptr;
    void* drawPrimitiveTarget = nullptr;
    void* drawIndexedPrimitiveTarget = nullptr;
    void* drawPrimitiveUPTarget = nullptr;
    void* drawIndexedPrimitiveUPTarget = nullptr;
    DWORD primitiveType = 0;
    UINT minimumVertexIndex = 0;
    UINT vertexCount = 0;
    UINT startIndex = 0;
    UINT primitiveCount = 0;
    D3DSurfaceDescription colorDescription = {};
    D3DSurfaceDescription depthDescription = {};
    D3DViewport originalViewport = {};
    D3DMatrix originalView = {};
    D3DMatrix originalProjection = {};
    D3DMatrix leftView = {};
    D3DMatrix rightView = {};
    D3DMatrix leftProjection = {};
    D3DMatrix rightProjection = {};
    HRESULT originalDrawResult = E_FAIL;
    HRESULT leftDrawResult = E_FAIL;
    HRESULT rightDrawResult = E_FAIL;
    HRESULT restoreTargetResult = E_FAIL;
    HRESULT restoreViewportResult = E_FAIL;
    HRESULT restoreViewResult = E_FAIL;
    HRESULT restoreProjectionResult = E_FAIL;
    BOOL targetRestored = FALSE;
    BOOL depthRestored = FALSE;
    BOOL viewportRestored = FALSE;
    BOOL viewRestored = FALSE;
    BOOL projectionRestored = FALSE;
    BOOL allStateRestored = FALSE;
    BOOL pairDrawn = FALSE;
    ReadbackResult readback[2] = {};
    ULONG ownedColorRelease[2] = {static_cast<ULONG>(-1), static_cast<ULONG>(-1)};
    ULONG ownedDepthRelease[2] = {static_cast<ULONG>(-1), static_cast<ULONG>(-1)};
    ULONG sourceColorRelease = static_cast<ULONG>(-1);
    ULONG sourceDepthRelease = static_cast<ULONG>(-1);
};

struct StereoFrameRecord
{
    void* ownedColor[2] = {};
    void* ownedDepth[2] = {};
    void* menuColor = nullptr;
    void* menuDepth = nullptr;
    void* reusableReadback[3] = {};
    D3DSurfaceDescription colorDescription = {};
    D3DSurfaceDescription depthDescription = {};
    D3DSurfaceDescription menuColorDescription = {};
    D3DSurfaceDescription reusableReadbackDescription[3] = {};
    volatile LONG resourcesReady = 0;
    volatile LONG mirroredDraws = 0;
    volatile LONG mirroredPrimitives = 0;
    volatile LONG excludedTargetDraws = 0;
    volatile LONG mirroredByKind[static_cast<std::size_t>(FrameDrawKind::Count)] = {};
    volatile LONG excludedByKind[static_cast<std::size_t>(FrameDrawKind::Count)] = {};
    volatile LONG boundedDrawSkips = 0;
    volatile LONG restoreChecks = 0;
    volatile LONG restoreFailures = 0;
    volatile LONG sourceReleaseChecks = 0;
    volatile LONG sourceReleaseFailures = 0;
    volatile LONG stereoPerspectiveDraws = 0;
    volatile LONG monoPretransformedDraws = 0;
    volatile LONG monoNonPerspectiveDraws = 0;
    volatile LONG skyboxCubeFaceDraws = 0;
    volatile LONG billboardBatchDraws = 0;
    volatile LONG treeMeshAlphaBlockDraws = 0;
    volatile LONG treeMeshProgrammableSpriteDraws = 0;
    volatile LONG animatedMeshSkinningDraws = 0;
    volatile LONG suppressedFirstPersonArmDraws = 0;
    volatile LONG translucentSpriteDraws = 0;
    volatile LONG ref2FontGlyphBatchDraws = 0;
    volatile LONG ref2MenuQuadDraws = 0;
    volatile LONG worldEyeDraws = 0;
    volatile LONG menuLayerDraws = 0;
    volatile LONG vertexShaderReadFailures = 0;
    volatile LONG skinningShaderPrepareFailures = 0;
    volatile LONG skinningShaderSourceMismatches = 0;
    volatile LONG skinningShaderApplyFailures = 0;
    volatile LONG spriteShaderPrepareFailures = 0;
    volatile LONG spriteShaderSourceMismatches = 0;
    volatile LONG spriteShaderApplyFailures = 0;
    volatile LONG treeSpriteShaderPrepareFailures = 0;
    volatile LONG treeSpriteShaderSourceMismatches = 0;
    volatile LONG treeSpriteShaderApplyFailures = 0;
    volatile LONG renderStateReadFailures = 0;
    DrawProvenanceRecord provenance[kMaximumProvenanceSites] = {};
    volatile LONG provenanceCount = 0;
    volatile LONG provenanceOverflow = 0;
    volatile LONG resetAborted = 0;
    HRESULT resetResult = E_FAIL;
    HRESULT lastLeftDrawResult = E_FAIL;
    HRESULT lastRightDrawResult = E_FAIL;
    HRESULT lastMenuDrawResult = E_FAIL;
    BOOL allRestorationsExact = TRUE;
    ReadbackResult readback[2] = {};
    ReadbackResult menuReadback = {};
    ULONG ownedColorRelease[2] = {static_cast<ULONG>(-1), static_cast<ULONG>(-1)};
    ULONG ownedDepthRelease[2] = {static_cast<ULONG>(-1), static_cast<ULONG>(-1)};
    ULONG menuColorRelease = static_cast<ULONG>(-1);
    ULONG menuDepthRelease = static_cast<ULONG>(-1);
    ULONG reusableReadbackRelease[3] = {
        static_cast<ULONG>(-1),
        static_cast<ULONG>(-1),
        static_cast<ULONG>(-1)};
    std::int64_t replayQpcTicks = 0;
    std::int64_t readbackQpcTicks = 0;
    std::int64_t uploadQpcTicks = 0;
    BOOL completedWithDifferingColor = FALSE;
};

struct PresentationRunRecord
{
    DWORD startedAt = 0;
    BOOL gpuResidentTransport = FALSE;
    volatile LONG publishedFrames = 0;
    volatile LONG presentedFrames = 0;
    volatile LONG failedFrames = 0;
    volatile LONG totalDraws = 0;
    volatile LONG totalWorldDraws = 0;
    volatile LONG totalUiDraws = 0;
    volatile LONG totalRestoreChecks = 0;
    volatile LONG totalRestoreFailures = 0;
    volatile LONG totalTreeRendererBillboardDraws = 0;
    volatile LONG totalTreeMeshAlphaBlockDraws = 0;
    volatile LONG totalTreeMeshProgrammableSpriteDraws = 0;
    volatile LONG totalAnimatedMeshSkinningDraws = 0;
    volatile LONG totalSuppressedFirstPersonArmDraws = 0;
    volatile LONG totalTranslucentSpriteDraws = 0;
    std::int64_t totalReplayQpcTicks = 0;
    std::int64_t totalReadbackQpcTicks = 0;
    std::int64_t totalUploadQpcTicks = 0;
    std::int64_t totalPresentationWaitQpcTicks = 0;
    std::int64_t totalRequestWaitQpcTicks = 0;
    LONG firstSequence = 0;
    LONG lastSequence = 0;
};

void ResetStereoFrameRecordForResourceReuse(
    StereoFrameRecord& record) noexcept;

void ResetStereoPairAttemptRecord(
    StereoPairRecord& record) noexcept;

void CountStereoFrameDrawPolicy(
    StereoFrameRecord& record,
    stereo::D3D8DrawPolicy policy) noexcept;

void CountStereoFrameSemanticDraw(
    StereoFrameRecord& record,
    stereo::D3D8SemanticDrawClass semanticClass) noexcept;

void AccumulateContinuousPresentationFrame(
    PresentationRunRecord& run,
    const StereoFrameRecord& frame,
    LONG sequence) noexcept;

} // namespace bfvr::d3d8probe
