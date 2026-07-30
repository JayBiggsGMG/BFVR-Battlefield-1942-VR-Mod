#pragma once

#include <cstdint>
#include <optional>

namespace bfvr
{

// Resolves named bones against the live, game-owned Skeleton instead of
// assuming the stock soldier's numeric indices. The implementation only uses
// prefix-validated BF1942 accessors and performs no Skeleton or BoneManager
// writes.
class BFSoldierBoneResolver final
{
public:
    [[nodiscard]] bool Initialize(void* gameImage) noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool IsReady() const noexcept;
    [[nodiscard]] std::optional<std::int32_t> ResolveBoneIndex(
        void* skeleton,
        const char* canonicalName) const noexcept;

private:
    void* getBoneNameTarget_ = nullptr;
    void* getBoneManagerInstanceTarget_ = nullptr;
    void* gameStringCStrTarget_ = nullptr;
};

} // namespace bfvr
