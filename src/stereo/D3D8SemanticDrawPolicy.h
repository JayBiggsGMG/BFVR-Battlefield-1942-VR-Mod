#pragma once

#include <cstdint>

namespace bfvr::stereo
{

enum class D3D8SemanticDrawClass
{
    Unclassified,
    SkyboxCubeFace,
    BillboardBatch,
    TreeMeshAlphaBlock,
    TreeMeshProgrammableSprite,
    AnimatedMeshSkinning,
    TranslucentSprite,
    Ref2FontGlyphBatch,
    Ref2MenuQuad
};

struct BF1942D3D8DrawSignature
{
    std::uint32_t wrapperReturnAddress = 0;
    std::uint32_t rendererReturnAddress = 0;
    bool indexedPrimitive = false;
    bool perspective = false;
    std::uint32_t primitiveType = 0;
    std::uint32_t primitiveCount = 0;
    std::uint32_t vertexShaderOrFvf = 0;
    std::uint32_t zEnable = 0;
    std::uint32_t zWriteEnable = 0;
    std::uint32_t alphaBlendEnable = 0;
    std::uint32_t fogEnable = 0;
    std::uint32_t lighting = 0;
    std::uint32_t producerReturnAddress = 0;
    std::uint64_t originalVertexShaderHash = 0;
    std::uint32_t originalVertexShaderByteCount = 0;
    std::uint32_t originalVertexShaderCreationOrdinal = 0;
};

// This classifier is deliberately specific to the profiled BF1942 Win32
// executable. Every field is required so an unknown draw fails closed.
[[nodiscard]] D3D8SemanticDrawClass ClassifyBF1942Win32SemanticDraw(
    const BF1942D3D8DrawSignature& signature) noexcept;

} // namespace bfvr::stereo
