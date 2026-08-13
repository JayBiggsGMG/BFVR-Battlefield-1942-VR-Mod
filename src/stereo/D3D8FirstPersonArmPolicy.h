#pragma once

#include <cstdint>
#include <string_view>

namespace bfvr::stereo
{

enum class D3D8FirstPersonPartKind : std::uint32_t
{
    UnknownOrCombined = 0,
    SeparateHand
};

// Recognizes only explicit left/right-hand template labels selected by the
// game. Combined and unfamiliar mod meshes remain UnknownOrCombined so Hands
// Only fails closed without an asset-name allowlist.
[[nodiscard]] D3D8FirstPersonPartKind
ClassifyD3D8FirstPersonPartTemplateName(
    std::string_view templateName) noexcept;

// The native first-person arms share BF1942's AnimatedMesh skinning route.
// This policy governs only whether an already-classified arm draw is omitted
// from the stereo replay. It does not identify game objects or alter them.
[[nodiscard]] bool ShouldSuppressBF1942FirstPersonArmDraw(
    bool presentationMode,
    bool firstPersonPartDraw,
    D3D8FirstPersonPartKind partKind,
    bool armsAndCombinedEnabled,
    bool separateHandsEnabled) noexcept;

} // namespace bfvr::stereo
