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
    WaterSurface,
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

// The profiled first-person arms use the exact AnimatedMeshSkinning family
// above, but unlike remote soldiers they are rendered through BF1942's narrow
// viewmodel projection. This fail-closed conjunction lets the VR replay omit
// those assets globally without changing the game's flat render or content.
[[nodiscard]] bool IsBF1942FirstPersonArmDraw(
    D3D8SemanticDrawClass semanticClass,
    bool perspective,
    float projectionM00,
    float projectionM11) noexcept;

} // namespace bfvr::stereo
