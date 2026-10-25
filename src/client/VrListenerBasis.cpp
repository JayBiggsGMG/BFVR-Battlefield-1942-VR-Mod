#include "client/VrListenerBasis.h"

#include <windows.h>

namespace
{
SRWLOCK g_basisLock = SRWLOCK_INIT;
bool g_hasBasis = false;
DWORD g_publishedTick = 0;
bfvr::VrListenerBasis g_basis = {};

} // namespace

namespace bfvr
{

void PublishVrListenerBasis(const VrListenerBasis& basis) noexcept
{
    AcquireSRWLockExclusive(&g_basisLock);
    g_basis = basis;
    g_publishedTick = GetTickCount();
    g_hasBasis = true;
    ReleaseSRWLockExclusive(&g_basisLock);
}

bool ReadVrListenerBasis(
    VrListenerBasis& basis,
    unsigned long maxAgeMs) noexcept
{
    AcquireSRWLockShared(&g_basisLock);
    if (!g_hasBasis ||
        GetTickCount() - g_publishedTick >
            static_cast<DWORD>(maxAgeMs))
    {
        ReleaseSRWLockShared(&g_basisLock);
        return false;
    }

    basis = g_basis;
    ReleaseSRWLockShared(&g_basisLock);
    return true;
}

} // namespace bfvr
