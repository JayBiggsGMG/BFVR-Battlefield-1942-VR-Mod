#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace bfvr::stereo
{

enum class D3D8FirstPersonPartKind : std::uint32_t
{
    UnknownOrCombined = 0,
    SeparateHand
};

struct D3D8FirstPersonPartTemplateCacheKey
{
    std::uintptr_t templateAddress = 0;
    std::array<std::uint32_t, 4> nameStorageIdentity = {};
};

// A small allocation-free, direct-mapped cache for the immutable mesh-template
// names selected by Battlefield. A hit requires both the template address and
// its name-storage identity, guarding against straightforward address reuse.
class D3D8FirstPersonPartClassificationCache
{
public:
    [[nodiscard]] bool Find(
        const D3D8FirstPersonPartTemplateCacheKey& key,
        D3D8FirstPersonPartKind& partKind) const noexcept;
    void Store(
        const D3D8FirstPersonPartTemplateCacheKey& key,
        D3D8FirstPersonPartKind partKind) noexcept;
    void Clear() noexcept;

private:
    static constexpr std::size_t kCapacity = 256;

    struct Entry
    {
        D3D8FirstPersonPartTemplateCacheKey key = {};
        D3D8FirstPersonPartKind partKind =
            D3D8FirstPersonPartKind::UnknownOrCombined;
    };

    [[nodiscard]] static std::size_t Index(
        const D3D8FirstPersonPartTemplateCacheKey& key) noexcept;

    std::array<Entry, kCapacity> entries_ = {};
};

// Recognizes only explicit left/right-hand template labels selected by the
// game. Combined and unfamiliar mod meshes remain UnknownOrCombined so Hands
// Only fails closed without an asset-name allowlist.
[[nodiscard]] D3D8FirstPersonPartKind
ClassifyD3D8FirstPersonPartTemplateName(
    std::string_view templateName) noexcept;

// Template-name inspection is useful only for a proven first-person draw in
// Hands Only mode. Full visibility and full suppression do not depend on the
// individual part kind.
[[nodiscard]] bool ShouldClassifyBF1942FirstPersonPartDraw(
    bool presentationMode,
    bool firstPersonPartDraw,
    bool armsAndCombinedEnabled,
    bool separateHandsEnabled) noexcept;

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
