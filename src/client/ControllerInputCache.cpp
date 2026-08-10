#include "client/ControllerInputCache.h"

#include <array>
#include <climits>

namespace
{
struct ControllerInputCacheSlot
{
    bfvr::D3D8RuntimeControllerSample sample = {};
    bfvr::D3D8RuntimeControllerSample nativeAimSample = {};
    bfvr::D3D8RuntimeView matchingHead = {};
    bool matchingHeadTracked = false;
    float presentationBodyYawRadians = 0.0F;
    bool presentationBodyYawValid = false;
    DWORD publishedAt = 0;
};

std::array<ControllerInputCacheSlot, 2> g_slots = {};
volatile LONG g_publishedGeneration = 0;

LONG NextGeneration(LONG current) noexcept
{
    return current == LONG_MAX ? 1 : current + 1;
}
} // namespace

namespace bfvr
{
void PublishAcceptedControllerInput(
    const D3D8RuntimeControllerSample& sample,
    const D3D8RuntimeControllerSample& nativeAimSample,
    const D3D8RuntimeView& matchingHead,
    const bool matchingHeadTracked,
    const float presentationBodyYawRadians,
    const bool presentationBodyYawValid) noexcept
{
    const LONG next = NextGeneration(
        InterlockedCompareExchange(&g_publishedGeneration, 0, 0));
    ControllerInputCacheSlot& destination =
        g_slots[static_cast<std::size_t>(next) % g_slots.size()];
    destination.sample = sample;
    destination.nativeAimSample = nativeAimSample;
    destination.matchingHead = matchingHead;
    destination.matchingHeadTracked = matchingHeadTracked;
    destination.presentationBodyYawRadians = presentationBodyYawRadians;
    destination.presentationBodyYawValid = presentationBodyYawValid;
    destination.publishedAt = GetTickCount();
    MemoryBarrier();
    InterlockedExchange(&g_publishedGeneration, next);
}

void ClearAcceptedControllerInput() noexcept
{
    PublishAcceptedControllerInput({}, {}, {}, false, 0.0F, false);
}

bool ReadFreshAcceptedControllerInput(
    D3D8RuntimeControllerSample& sample,
    LONG& generation,
    DWORD maximumAgeMs) noexcept
{
    sample = {};
    generation = 0;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const LONG firstGeneration =
            InterlockedCompareExchange(&g_publishedGeneration, 0, 0);
        if (firstGeneration == 0)
        {
            return false;
        }
        const ControllerInputCacheSlot& source =
            g_slots[static_cast<std::size_t>(firstGeneration) % g_slots.size()];
        const D3D8RuntimeControllerSample snapshot = source.sample;
        const DWORD publishedAt = source.publishedAt;
        MemoryBarrier();
        if (InterlockedCompareExchange(&g_publishedGeneration, 0, 0) !=
            firstGeneration)
        {
            continue;
        }
        if (!snapshot.valid || !snapshot.sessionFocused ||
            GetTickCount() - publishedAt > maximumAgeMs)
        {
            return false;
        }
        sample = snapshot;
        generation = firstGeneration;
        return true;
    }
    return false;
}

bool ReadFreshAcceptedNativeInfantryAimInput(
    D3D8RuntimeControllerSample& sample,
    LONG& generation,
    DWORD maximumAgeMs) noexcept
{
    sample = {};
    generation = 0;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const LONG firstGeneration =
            InterlockedCompareExchange(&g_publishedGeneration, 0, 0);
        if (firstGeneration == 0)
        {
            return false;
        }
        const ControllerInputCacheSlot& source =
            g_slots[static_cast<std::size_t>(firstGeneration) % g_slots.size()];
        const D3D8RuntimeControllerSample snapshot = source.nativeAimSample;
        const DWORD publishedAt = source.publishedAt;
        MemoryBarrier();
        if (InterlockedCompareExchange(&g_publishedGeneration, 0, 0) !=
            firstGeneration)
        {
            continue;
        }
        if (!snapshot.valid || !snapshot.sessionFocused ||
            GetTickCount() - publishedAt > maximumAgeMs)
        {
            return false;
        }
        sample = snapshot;
        generation = firstGeneration;
        return true;
    }
    return false;
}

bool ReadFreshAcceptedInfantryPresentationInput(
    D3D8RuntimeControllerSample& sample,
    float& presentationBodyYawRadians,
    bool& presentationBodyYawValid,
    LONG& generation,
    const DWORD maximumAgeMs) noexcept
{
    sample = {};
    presentationBodyYawRadians = 0.0F;
    presentationBodyYawValid = false;
    generation = 0;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const LONG firstGeneration =
            InterlockedCompareExchange(&g_publishedGeneration, 0, 0);
        if (firstGeneration == 0)
        {
            return false;
        }
        const ControllerInputCacheSlot& source =
            g_slots[static_cast<std::size_t>(firstGeneration) % g_slots.size()];
        const D3D8RuntimeControllerSample sampleSnapshot = source.sample;
        const float bodyYawSnapshot = source.presentationBodyYawRadians;
        const bool bodyYawValidSnapshot = source.presentationBodyYawValid;
        const DWORD publishedAt = source.publishedAt;
        MemoryBarrier();
        if (InterlockedCompareExchange(&g_publishedGeneration, 0, 0) !=
            firstGeneration)
        {
            continue;
        }
        if (!sampleSnapshot.valid || !sampleSnapshot.sessionFocused ||
            GetTickCount() - publishedAt > maximumAgeMs)
        {
            return false;
        }
        sample = sampleSnapshot;
        presentationBodyYawRadians = bodyYawSnapshot;
        presentationBodyYawValid = bodyYawValidSnapshot;
        generation = firstGeneration;
        return true;
    }
    return false;
}

bool ReadFreshAcceptedWeaponTracking(
    D3D8RuntimeControllerSample& sample,
    D3D8RuntimeView& matchingHead,
    LONG& generation,
    DWORD maximumAgeMs) noexcept
{
    sample = {};
    matchingHead = {};
    generation = 0;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const LONG firstGeneration =
            InterlockedCompareExchange(&g_publishedGeneration, 0, 0);
        if (firstGeneration == 0)
        {
            return false;
        }
        const ControllerInputCacheSlot& source =
            g_slots[static_cast<std::size_t>(firstGeneration) % g_slots.size()];
        const D3D8RuntimeControllerSample sampleSnapshot = source.sample;
        const D3D8RuntimeView headSnapshot = source.matchingHead;
        const bool headTrackedSnapshot = source.matchingHeadTracked;
        const DWORD publishedAt = source.publishedAt;
        MemoryBarrier();
        if (InterlockedCompareExchange(&g_publishedGeneration, 0, 0) !=
            firstGeneration)
        {
            continue;
        }
        if (!sampleSnapshot.valid || !sampleSnapshot.sessionFocused ||
            !headTrackedSnapshot ||
            GetTickCount() - publishedAt > maximumAgeMs)
        {
            return false;
        }
        sample = sampleSnapshot;
        matchingHead = headSnapshot;
        generation = firstGeneration;
        return true;
    }
    return false;
}

} // namespace bfvr
