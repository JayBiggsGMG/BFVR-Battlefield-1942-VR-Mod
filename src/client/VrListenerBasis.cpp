#include "client/VrListenerBasis.h"

#include <windows.h>

namespace
{
// Even means stable, odd means a write is in progress. Zero additionally means
// nothing has ever been published.
volatile LONG g_sequence = 0;
volatile LONG g_publishedTick = 0;

bfvr::VrListenerBasis g_basis = {};

constexpr int kReadAttempts = 4;

} // namespace

namespace bfvr
{

void PublishVrListenerBasis(const VrListenerBasis& basis) noexcept
{
    InterlockedIncrement(&g_sequence);
    MemoryBarrier();

    g_basis = basis;

    InterlockedExchange(
        &g_publishedTick,
        static_cast<LONG>(GetTickCount()));

    MemoryBarrier();
    InterlockedIncrement(&g_sequence);
}

bool ReadVrListenerBasis(
    VrListenerBasis& basis,
    unsigned long maxAgeMs) noexcept
{
    for (int attempt = 0; attempt < kReadAttempts; ++attempt)
    {
        LONG before = InterlockedCompareExchange(&g_sequence, 0, 0);

        if (before == 0 || (before & 1) != 0)
        {
            if (before == 0)
                return false;

            continue;
        }

        LONG tick = InterlockedCompareExchange(&g_publishedTick, 0, 0);

        VrListenerBasis candidate = g_basis;

        MemoryBarrier();

        if (InterlockedCompareExchange(&g_sequence, 0, 0) != before)
            continue;

        if (GetTickCount() - static_cast<DWORD>(tick) > static_cast<DWORD>(maxAgeMs))
            return false;

        basis = candidate;

        return true;
    }

    return false;
}

} // namespace bfvr
