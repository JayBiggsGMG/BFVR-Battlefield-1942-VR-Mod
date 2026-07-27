#include "client/WeaponPoseRuntimeCache.h"

namespace
{

struct WeaponViewOffsetState
{
    bfvr::stereo::Matrix4 viewOffset = {};
    LONG controllerGeneration = 0;
    DWORD publishedAt = 0;
    bool valid = false;
};

SRWLOCK g_lock = SRWLOCK_INIT;
WeaponViewOffsetState g_state = {};

void Publish(
    const bfvr::stereo::Matrix4& viewOffset,
    LONG controllerGeneration,
    bool valid) noexcept
{
    AcquireSRWLockExclusive(&g_lock);
    g_state.viewOffset = viewOffset;
    g_state.controllerGeneration = controllerGeneration;
    g_state.publishedAt = GetTickCount();
    g_state.valid = valid;
    ReleaseSRWLockExclusive(&g_lock);
}

} // namespace

namespace bfvr
{

void PublishWeaponViewOffset(
    const stereo::Matrix4& viewOffset,
    LONG controllerGeneration) noexcept
{
    Publish(viewOffset, controllerGeneration, true);
}

void ClearWeaponViewOffset() noexcept
{
    Publish({}, 0, false);
}

bool ReadFreshWeaponViewOffset(
    stereo::Matrix4& viewOffset,
    LONG& controllerGeneration,
    DWORD maximumAgeMs) noexcept
{
    viewOffset = {};
    controllerGeneration = 0;
    AcquireSRWLockShared(&g_lock);
    const stereo::Matrix4 snapshot = g_state.viewOffset;
    const LONG snapshotGeneration = g_state.controllerGeneration;
    const DWORD publishedAt = g_state.publishedAt;
    const bool valid = g_state.valid;
    ReleaseSRWLockShared(&g_lock);
    if (!valid || GetTickCount() - publishedAt > maximumAgeMs)
    {
        return false;
    }
    viewOffset = snapshot;
    controllerGeneration = snapshotGeneration;
    return true;
}

} // namespace bfvr
