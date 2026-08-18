#include "stereo/D3D8FirstPersonArmPolicy.h"

#include <array>

namespace bfvr::stereo
{

namespace
{

bool CacheKeysMatch(
    const D3D8FirstPersonPartTemplateCacheKey& left,
    const D3D8FirstPersonPartTemplateCacheKey& right) noexcept
{
    return left.templateAddress == right.templateAddress &&
        left.nameStorageIdentity == right.nameStorageIdentity;
}

} // namespace

std::size_t D3D8FirstPersonPartClassificationCache::Index(
    const D3D8FirstPersonPartTemplateCacheKey& key) noexcept
{
    std::size_t hash = static_cast<std::size_t>(key.templateAddress);
    for (const std::uint32_t value : key.nameStorageIdentity)
    {
        hash ^= static_cast<std::size_t>(value) + 0x9E3779B9U +
            (hash << 6U) + (hash >> 2U);
    }
    return hash % kCapacity;
}

bool D3D8FirstPersonPartClassificationCache::Find(
    const D3D8FirstPersonPartTemplateCacheKey& key,
    D3D8FirstPersonPartKind& partKind) const noexcept
{
    if (key.templateAddress == 0)
    {
        return false;
    }
    const Entry& entry = entries_[Index(key)];
    if (!CacheKeysMatch(entry.key, key))
    {
        return false;
    }
    partKind = entry.partKind;
    return true;
}

void D3D8FirstPersonPartClassificationCache::Store(
    const D3D8FirstPersonPartTemplateCacheKey& key,
    const D3D8FirstPersonPartKind partKind) noexcept
{
    if (key.templateAddress == 0)
    {
        return;
    }
    entries_[Index(key)] = {key, partKind};
}

void D3D8FirstPersonPartClassificationCache::Clear() noexcept
{
    entries_.fill({});
}

D3D8FirstPersonPartKind ClassifyD3D8FirstPersonPartTemplateName(
    const std::string_view templateName) noexcept
{
    std::array<char, 128> normalized = {};
    std::size_t length = 0;
    for (const unsigned char value : templateName)
    {
        char normalizedValue = 0;
        if (value >= 'A' && value <= 'Z')
        {
            normalizedValue = static_cast<char>(value - 'A' + 'a');
        }
        else if ((value >= 'a' && value <= 'z') ||
                 (value >= '0' && value <= '9'))
        {
            normalizedValue = static_cast<char>(value);
        }
        if (normalizedValue != 0)
        {
            if (length == normalized.size())
            {
                return D3D8FirstPersonPartKind::UnknownOrCombined;
            }
            normalized[length++] = normalizedValue;
        }
    }
    const std::string_view compact(normalized.data(), length);
    return compact.find("lefthand") != std::string_view::npos ||
            compact.find("righthand") != std::string_view::npos
        ? D3D8FirstPersonPartKind::SeparateHand
        : D3D8FirstPersonPartKind::UnknownOrCombined;
}

bool ShouldClassifyBF1942FirstPersonPartDraw(
    const bool presentationMode,
    const bool firstPersonPartDraw,
    const bool armsAndCombinedEnabled,
    const bool separateHandsEnabled) noexcept
{
    return presentationMode && firstPersonPartDraw &&
        !armsAndCombinedEnabled && separateHandsEnabled;
}

bool ShouldSuppressBF1942FirstPersonArmDraw(
    const bool presentationMode,
    const bool firstPersonPartDraw,
    const D3D8FirstPersonPartKind partKind,
    const bool armsAndCombinedEnabled,
    const bool separateHandsEnabled) noexcept
{
    if (!presentationMode || !firstPersonPartDraw)
    {
        return false;
    }
    return partKind == D3D8FirstPersonPartKind::SeparateHand
        ? !separateHandsEnabled
        : !armsAndCombinedEnabled;
}

} // namespace bfvr::stereo
