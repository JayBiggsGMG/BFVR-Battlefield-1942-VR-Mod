#pragma once

#include "bfvr_shared_bridge.hpp"

#include <array>
#include <cstddef>

namespace bfvr
{

class D3D8To9VertexShaderIdentityResolver
{
public:
    bool Resolve() noexcept;
    bool TryGet(
        void* d3d8Device,
        DWORD d3d8Handle,
        BFVRD3D8To9VertexShaderIdentity& identity) noexcept;
    void ClearCache() noexcept;

    [[nodiscard]] bool IsAvailable() const noexcept;

private:
    struct CacheEntry
    {
        void* device = nullptr;
        BFVRD3D8To9VertexShaderIdentity identity = {};
    };

    static constexpr std::size_t kMaximumCachedShaders = 64;
    BFVRD3D8To9GetVertexShaderIdentityFn getIdentity_ = nullptr;
    std::array<CacheEntry, kMaximumCachedShaders> cache_ = {};
    std::size_t cacheSize_ = 0;
};

} // namespace bfvr
