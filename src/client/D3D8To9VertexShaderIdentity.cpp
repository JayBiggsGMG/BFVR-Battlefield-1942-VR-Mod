#include "client/D3D8To9VertexShaderIdentity.h"

namespace bfvr
{

bool D3D8To9VertexShaderIdentityResolver::Resolve() noexcept
{
    getIdentity_ = nullptr;
    ClearCache();
    const HMODULE translator = GetModuleHandleW(L"BFVRD3D8To9.dll");
    if (translator == nullptr)
    {
        return false;
    }

    const auto getVersion =
        reinterpret_cast<BFVRD3D8To9GetSharedBridgeVersionFn>(
            GetProcAddress(
                translator,
                "BFVRD3D8To9GetSharedBridgeVersion"));
    getIdentity_ =
        reinterpret_cast<BFVRD3D8To9GetVertexShaderIdentityFn>(
            GetProcAddress(
                translator,
                "BFVRD3D8To9GetVertexShaderIdentity"));
    if (getVersion == nullptr ||
        getVersion() != BFVR_D3D8TO9_SHARED_BRIDGE_VERSION ||
        getIdentity_ == nullptr)
    {
        getIdentity_ = nullptr;
        return false;
    }
    return true;
}

bool D3D8To9VertexShaderIdentityResolver::TryGet(
    void* d3d8Device,
    DWORD d3d8Handle,
    BFVRD3D8To9VertexShaderIdentity& identity) noexcept
{
    identity = {};
    if (!IsAvailable() ||
        d3d8Device == nullptr ||
        (d3d8Handle & 0x80000000UL) == 0)
    {
        return false;
    }

    for (std::size_t index = 0; index < cacheSize_; ++index)
    {
        const CacheEntry& cached = cache_[index];
        if (cached.device == d3d8Device &&
            cached.identity.d3d8Handle == d3d8Handle)
        {
            identity = cached.identity;
            return true;
        }
    }

    BFVRD3D8To9VertexShaderIdentity queried = {};
    queried.size = sizeof(queried);
    if (FAILED(getIdentity_(
            d3d8Device,
            d3d8Handle,
            &queried)) ||
        queried.version != BFVR_D3D8TO9_SHARED_BRIDGE_VERSION ||
        queried.d3d8Handle != d3d8Handle)
    {
        return false;
    }

    identity = queried;
    if (cacheSize_ < cache_.size())
    {
        CacheEntry& cached = cache_[cacheSize_++];
        cached.device = d3d8Device;
        cached.identity = queried;
    }
    return true;
}

void D3D8To9VertexShaderIdentityResolver::ClearCache() noexcept
{
    cache_ = {};
    cacheSize_ = 0;
}

bool D3D8To9VertexShaderIdentityResolver::IsAvailable() const noexcept
{
    return getIdentity_ != nullptr;
}

} // namespace bfvr
