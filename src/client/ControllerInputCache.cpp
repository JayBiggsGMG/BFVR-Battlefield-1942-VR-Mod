#include "client/ControllerInputCache.h"

#include <array>
#include <climits>

namespace
{
struct ControllerInputCacheSlot
{
    bfvr::D3D8RuntimeControllerSample sample = {};
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
    const D3D8RuntimeControllerSample& sample) noexcept
{
    const LONG next = NextGeneration(
        InterlockedCompareExchange(&g_publishedGeneration, 0, 0));
    ControllerInputCacheSlot& destination =
        g_slots[static_cast<std::size_t>(next) % g_slots.size()];
    destination.sample = sample;
    destination.publishedAt = GetTickCount();
    MemoryBarrier();
    InterlockedExchange(&g_publishedGeneration, next);
}

void ClearAcceptedControllerInput() noexcept
{
    PublishAcceptedControllerInput({});
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
} // namespace bfvr
