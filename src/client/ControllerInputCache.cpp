#include "client/ControllerInputCache.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>

namespace
{
struct ControllerInputCacheSlot
{
    bfvr::D3D8RuntimeControllerSample sample = {};
    bfvr::D3D8RuntimeView matchingHead = {};
    bool matchingHeadTracked = false;
    DWORD publishedAt = 0;
};

std::array<ControllerInputCacheSlot, 2> g_slots = {};
volatile LONG g_publishedGeneration = 0;
volatile LONG g_controllerInfantryTurnMillidegrees = 0;

LONG NextGeneration(LONG current) noexcept
{
    return current == LONG_MAX ? 1 : current + 1;
}
} // namespace

namespace bfvr
{
void PublishAcceptedControllerInput(
    const D3D8RuntimeControllerSample& sample,
    const D3D8RuntimeView& matchingHead,
    bool matchingHeadTracked) noexcept
{
    const LONG next = NextGeneration(
        InterlockedCompareExchange(&g_publishedGeneration, 0, 0));
    ControllerInputCacheSlot& destination =
        g_slots[static_cast<std::size_t>(next) % g_slots.size()];
    destination.sample = sample;
    destination.matchingHead = matchingHead;
    destination.matchingHeadTracked = matchingHeadTracked;
    destination.publishedAt = GetTickCount();
    MemoryBarrier();
    InterlockedExchange(&g_publishedGeneration, next);
}

void ClearAcceptedControllerInput() noexcept
{
    PublishAcceptedControllerInput({}, {}, false);
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

void PublishControllerInfantryTurnIntent(float degrees) noexcept
{
    if (!std::isfinite(degrees) || degrees == 0.0F)
    {
        return;
    }
    // A single comfort-turn request is capped at 90 degrees, but cancelling a
    // queued multiplayer turn can legitimately unwind several queued requests.
    constexpr float kMaximumPublishedTurnDegrees = 720.0F;
    const LONG millidegrees = static_cast<LONG>(std::lround(
        std::clamp(
            degrees,
            -kMaximumPublishedTurnDegrees,
            kMaximumPublishedTurnDegrees) *
        1000.0F));
    if (millidegrees != 0)
    {
        InterlockedExchangeAdd(
            &g_controllerInfantryTurnMillidegrees,
            millidegrees);
    }
}

LONG ReadControllerInfantryTurnIntentMillidegrees() noexcept
{
    return InterlockedCompareExchange(
        &g_controllerInfantryTurnMillidegrees,
        0,
        0);
}
} // namespace bfvr
