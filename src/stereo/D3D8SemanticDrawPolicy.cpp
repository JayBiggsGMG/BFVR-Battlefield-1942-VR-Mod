#include "stereo/D3D8SemanticDrawPolicy.h"

#include <cmath>

namespace
{

constexpr std::uint32_t kDrawIndexedPrimitiveWrapperReturn = 0x0066800A;
constexpr std::uint32_t kDrawPrimitiveWrapperReturn = 0x00667EF4;
constexpr std::uint32_t kDrawPrimitiveUpWrapperReturn = 0x00667DFD;
constexpr std::uint32_t kGenericMeshLoopReturn = 0x0062B83F;
constexpr std::uint32_t kTreeRendererDrawBillboardsReturn = 0x0064D84C;
constexpr std::uint32_t kNewRendFontDrawReturn = 0x0065D140;
constexpr std::uint32_t kTreeMeshDrawBlocksReturn = 0x0067C997;
constexpr std::uint32_t kAnimatedMeshDrawMeshReturn = 0x005AF40F;
constexpr std::uint32_t kPatchCellBlockDrawReturn = 0x0069922E;
constexpr std::uint32_t kPatchTerrainShadowCellDrawReturn = 0x00683ADD;
constexpr std::uint32_t kWaterSurfaceDrawReturn = 0x00654571;
constexpr std::uint32_t kSpriteInfoDrawReturn = 0x0062E8BE;
constexpr std::uint32_t kMenuQuadFlushReturn = 0x00664CF6;
constexpr std::uint32_t kMenuQuadCacheReturn = 0x00665098;
constexpr std::uint32_t kMenuQuadImmediateReturn = 0x00666018;
constexpr std::uint32_t kRendererDrawPrimitiveUpReturn = 0x007EBFF6;
constexpr std::uint32_t kTriangleList = 4;
constexpr std::uint32_t kTriangleStrip = 5;
constexpr std::uint32_t kTriangleFan = 6;
constexpr std::uint32_t kSkyboxFvf = 0x112;
constexpr std::uint32_t kTreeBillboardFvf = 0x2C2;
constexpr std::uint32_t kTreeMeshFvf = 0x252;
constexpr std::uint32_t kSkinningShader2BonesHandle = 0x17;
constexpr std::uint64_t kTranslatedSkinningShader2BonesHash =
    0x64F6D635ADDB7328ULL;
constexpr std::uint32_t kTranslatedSkinningShader2BonesByteCount = 1836;
constexpr std::uint64_t kTranslatedTreeSpriteShaderHash =
    0xC3A0389DCCB0E1B0ULL;
constexpr std::uint32_t kTranslatedTreeSpriteShaderByteCount = 2256;
constexpr std::uint64_t kTranslatedTranslucentSpriteShaderHash =
    0xEE5AA9145B53F1BEULL;
constexpr std::uint32_t kTranslatedTranslucentSpriteShaderByteCount = 3056;
constexpr std::uint32_t kTranslatedTranslucentSpriteShaderCreationOrdinal = 5;
constexpr std::uint32_t kRef2FontFvf = 0x144;
constexpr std::uint32_t kMenuQuadFvf = 0x142;

} // namespace

namespace bfvr::stereo
{

D3D8SemanticDrawClass ClassifyBF1942Win32SemanticDraw(
    const BF1942D3D8DrawSignature& signature) noexcept
{
    const bool skyboxCubeFace =
        signature.wrapperReturnAddress == kDrawIndexedPrimitiveWrapperReturn &&
        signature.rendererReturnAddress == kGenericMeshLoopReturn &&
        signature.indexedPrimitive &&
        signature.perspective &&
        signature.primitiveType == kTriangleList &&
        signature.primitiveCount == 2 &&
        signature.vertexShaderOrFvf == kSkyboxFvf &&
        signature.zEnable == 0 &&
        signature.zWriteEnable == 1 &&
        signature.alphaBlendEnable == 0 &&
        signature.fogEnable == 0 &&
        signature.lighting == 0;
    if (skyboxCubeFace)
    {
        return D3D8SemanticDrawClass::SkyboxCubeFace;
    }

    const bool billboardBatch =
        signature.wrapperReturnAddress == kDrawPrimitiveWrapperReturn &&
        signature.rendererReturnAddress == kTreeRendererDrawBillboardsReturn &&
        !signature.indexedPrimitive &&
        signature.perspective &&
        signature.primitiveType == kTriangleList &&
        signature.primitiveCount != 0 &&
        signature.vertexShaderOrFvf == kTreeBillboardFvf &&
        signature.zEnable == 1 &&
        signature.zWriteEnable == 0 &&
        signature.alphaBlendEnable == 1 &&
        signature.fogEnable == 1 &&
        signature.lighting == 0;
    if (billboardBatch)
    {
        return D3D8SemanticDrawClass::BillboardBatch;
    }

    const bool treeMeshAlphaBlock =
        signature.wrapperReturnAddress == kDrawIndexedPrimitiveWrapperReturn &&
        signature.rendererReturnAddress == kTreeMeshDrawBlocksReturn &&
        signature.indexedPrimitive &&
        signature.perspective &&
        signature.primitiveType == kTriangleList &&
        signature.primitiveCount != 0 &&
        signature.vertexShaderOrFvf == kTreeMeshFvf &&
        signature.zEnable == 1 &&
        signature.zWriteEnable == 0 &&
        signature.alphaBlendEnable == 1 &&
        signature.fogEnable == 1 &&
        signature.lighting == 1;
    if (treeMeshAlphaBlock)
    {
        return D3D8SemanticDrawClass::TreeMeshAlphaBlock;
    }

    const bool treeMeshProgrammableSprite =
        signature.wrapperReturnAddress == kDrawIndexedPrimitiveWrapperReturn &&
        signature.rendererReturnAddress == kTreeMeshDrawBlocksReturn &&
        signature.indexedPrimitive &&
        signature.perspective &&
        signature.primitiveType == kTriangleList &&
        signature.primitiveCount != 0 &&
        signature.originalVertexShaderHash ==
            kTranslatedTreeSpriteShaderHash &&
        signature.originalVertexShaderByteCount ==
            kTranslatedTreeSpriteShaderByteCount &&
        // Creation order varies with which renderer resources a level loads
        // first. The executable call site, original shader bytes, and exact
        // TreeMesh sprite states identify this draw without that ordinal.
        signature.zEnable == 1 &&
        signature.zWriteEnable == 0 &&
        signature.alphaBlendEnable == 1 &&
        signature.fogEnable == 1 &&
        signature.lighting == 1;
    if (treeMeshProgrammableSprite)
    {
        return D3D8SemanticDrawClass::TreeMeshProgrammableSprite;
    }

    const bool skinningShader2Bones =
        signature.vertexShaderOrFvf ==
            kSkinningShader2BonesHandle ||
        (signature.originalVertexShaderHash ==
             kTranslatedSkinningShader2BonesHash &&
         signature.originalVertexShaderByteCount ==
             kTranslatedSkinningShader2BonesByteCount);
    const bool animatedMeshSkinning =
        signature.wrapperReturnAddress == kDrawIndexedPrimitiveWrapperReturn &&
        signature.rendererReturnAddress == kAnimatedMeshDrawMeshReturn &&
        signature.indexedPrimitive &&
        signature.perspective &&
        (signature.primitiveType == kTriangleList ||
         signature.primitiveType == kTriangleStrip) &&
        signature.primitiveCount != 0 &&
        skinningShader2Bones &&
        signature.zEnable == 1 &&
        signature.zWriteEnable == 1 &&
        signature.alphaBlendEnable == 0 &&
        signature.fogEnable == 1 &&
        signature.lighting == 1;
    if (animatedMeshSkinning)
    {
        return D3D8SemanticDrawClass::AnimatedMeshSkinning;
    }

    const bool projectedTerrainShadow =
        signature.wrapperReturnAddress == kDrawIndexedPrimitiveWrapperReturn &&
        signature.rendererReturnAddress == kPatchCellBlockDrawReturn &&
        signature.producerReturnAddress ==
            kPatchTerrainShadowCellDrawReturn &&
        signature.indexedPrimitive &&
        signature.perspective &&
        signature.primitiveType == kTriangleList &&
        signature.primitiveCount != 0;
    if (projectedTerrainShadow)
    {
        return D3D8SemanticDrawClass::ProjectedTerrainShadow;
    }

    const bool waterSurface =
        signature.wrapperReturnAddress == kDrawIndexedPrimitiveWrapperReturn &&
        signature.rendererReturnAddress == kWaterSurfaceDrawReturn &&
        signature.indexedPrimitive &&
        signature.perspective &&
        signature.primitiveType == kTriangleList &&
        signature.primitiveCount != 0 &&
        signature.zEnable == 1 &&
        (signature.zWriteEnable == 0 || signature.zWriteEnable == 1) &&
        signature.alphaBlendEnable == 1 &&
        signature.fogEnable == 1 &&
        signature.lighting == 0;
    if (waterSurface)
    {
        return D3D8SemanticDrawClass::WaterSurface;
    }

    const bool translucentSprite =
        signature.wrapperReturnAddress == kDrawIndexedPrimitiveWrapperReturn &&
        signature.rendererReturnAddress == kSpriteInfoDrawReturn &&
        signature.indexedPrimitive &&
        signature.perspective &&
        signature.primitiveType == kTriangleList &&
        signature.primitiveCount != 0 &&
        signature.originalVertexShaderHash ==
            kTranslatedTranslucentSpriteShaderHash &&
        signature.originalVertexShaderByteCount ==
            kTranslatedTranslucentSpriteShaderByteCount &&
        signature.originalVertexShaderCreationOrdinal ==
            kTranslatedTranslucentSpriteShaderCreationOrdinal &&
        signature.zEnable == 1 &&
        signature.zWriteEnable == 0 &&
        signature.alphaBlendEnable == 1;
    if (translucentSprite)
    {
        return D3D8SemanticDrawClass::TranslucentSprite;
    }

    const bool ref2FontGlyphBatch =
        signature.wrapperReturnAddress == kDrawPrimitiveWrapperReturn &&
        signature.rendererReturnAddress == kNewRendFontDrawReturn &&
        !signature.indexedPrimitive &&
        signature.primitiveType == kTriangleList &&
        signature.primitiveCount != 0 &&
        (signature.primitiveCount % 2) == 0 &&
        signature.vertexShaderOrFvf == kRef2FontFvf &&
        // NewRendFont always disables depth testing, but its authored font
        // flags choose alpha blending, alpha testing, or neither. The method
        // does not own Z-write, fog, or lighting, so those inherited states
        // cannot be part of the semantic identity. The exact renderer return,
        // wrapper, topology, and XYZRHW glyph format remain fail-closed.
        signature.zEnable == 0;
    if (ref2FontGlyphBatch)
    {
        return D3D8SemanticDrawClass::Ref2FontGlyphBatch;
    }

    const bool immediateMenuQuadDrawSite =
        (signature.rendererReturnAddress == kMenuQuadFlushReturn &&
         signature.primitiveType == kTriangleList) ||
        (signature.rendererReturnAddress == kRendererDrawPrimitiveUpReturn &&
         signature.producerReturnAddress == kMenuQuadFlushReturn &&
         signature.primitiveType == kTriangleList) ||
        (signature.rendererReturnAddress == kMenuQuadImmediateReturn &&
         signature.primitiveType == kTriangleFan);
    const bool immediateRef2MenuQuad =
        signature.wrapperReturnAddress == kDrawPrimitiveUpWrapperReturn &&
        immediateMenuQuadDrawSite &&
        !signature.indexedPrimitive &&
        !signature.perspective &&
        signature.primitiveCount != 0 &&
        signature.vertexShaderOrFvf == kMenuQuadFvf &&
        (signature.zEnable == 0 || signature.zEnable == 1) &&
        signature.zWriteEnable == 0 &&
        signature.alphaBlendEnable == 1 &&
        signature.fogEnable == 0 &&
        signature.lighting == 0;
    const bool cachedRef2MenuQuad =
        signature.wrapperReturnAddress == kDrawIndexedPrimitiveWrapperReturn &&
        signature.rendererReturnAddress == kMenuQuadCacheReturn &&
        signature.indexedPrimitive &&
        !signature.perspective &&
        signature.primitiveType == kTriangleList &&
        signature.primitiveCount != 0 &&
        signature.vertexShaderOrFvf == kMenuQuadFvf &&
        signature.zEnable == 1 &&
        signature.zWriteEnable == 1 &&
        signature.alphaBlendEnable == 0 &&
        signature.fogEnable == 0 &&
        signature.lighting == 0;
    return immediateRef2MenuQuad || cachedRef2MenuQuad
        ? D3D8SemanticDrawClass::Ref2MenuQuad
        : D3D8SemanticDrawClass::Unclassified;
}

bool IsBF1942FirstPersonArmDraw(
    D3D8SemanticDrawClass semanticClass,
    bool perspective,
    float projectionM00,
    float projectionM11,
    bool magnifiedWorldViewActive) noexcept
{
    return
        !magnifiedWorldViewActive &&
        semanticClass == D3D8SemanticDrawClass::AnimatedMeshSkinning &&
        perspective &&
        std::isfinite(projectionM00) &&
        std::isfinite(projectionM11) &&
        projectionM00 >= 2.0F &&
        projectionM11 >= 3.5F;
}

bool ShouldUseBF1942StereoStableWaterReflection(
    D3D8SemanticDrawClass semanticClass,
    std::uint32_t zWriteEnable,
    bool legacyStereoReflectionRequested) noexcept
{
    return !legacyStereoReflectionRequested &&
        semanticClass == D3D8SemanticDrawClass::WaterSurface &&
        zWriteEnable != 0;
}

} // namespace bfvr::stereo
