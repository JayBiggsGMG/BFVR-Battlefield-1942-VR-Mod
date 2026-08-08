#include "client/ScopedOffHandSupportPoseCache.h"

namespace
{

SRWLOCK g_lock = SRWLOCK_INIT;
bfvr::ScopedOffHandSupportPose g_pose = {};
DWORD g_publishedAt = 0;
bool g_valid = false;
DWORD g_supportPublishedAt = 0;
std::uint64_t g_supportBindingId = 0;
bool g_supportHeld = false;

} // namespace

namespace bfvr
{

void PublishScopedOffHandSupportPose(
    const std::uint64_t bindingId,
    const stereo::Matrix4& oneHandGunWorld,
    const stereo::Matrix4& predictedSupportWorld,
    const bool supported) noexcept
{
    AcquireSRWLockExclusive(&g_lock);
    g_pose = {
        oneHandGunWorld,
        predictedSupportWorld,
        bindingId,
        supported};
    g_publishedAt = GetTickCount();
    g_valid = bindingId != 0;
    ReleaseSRWLockExclusive(&g_lock);
}

bool ReadFreshScopedOffHandSupportPose(
    const std::uint64_t expectedBindingId,
    ScopedOffHandSupportPose& pose,
    const DWORD maximumAgeMs) noexcept
{
    pose = {};
    AcquireSRWLockShared(&g_lock);
    const ScopedOffHandSupportPose snapshot = g_pose;
    const DWORD publishedAt = g_publishedAt;
    const bool valid = g_valid && expectedBindingId != 0 &&
        snapshot.bindingId == expectedBindingId;
    ReleaseSRWLockShared(&g_lock);
    if (!valid || GetTickCount() - publishedAt > maximumAgeMs)
    {
        return false;
    }
    pose = snapshot;
    return true;
}

void PublishCurrentOffHandSupportState(
    const std::uint64_t bindingId,
    const bool supported) noexcept
{
    AcquireSRWLockExclusive(&g_lock);
    g_supportBindingId = bindingId;
    g_supportHeld = bindingId != 0 && supported;
    g_supportPublishedAt = GetTickCount();
    ReleaseSRWLockExclusive(&g_lock);
}

bool IsFreshCurrentOffHandSupportHeld(const DWORD maximumAgeMs) noexcept
{
    AcquireSRWLockShared(&g_lock);
    const bool held = g_supportBindingId != 0 && g_supportHeld;
    const DWORD publishedAt = g_supportPublishedAt;
    ReleaseSRWLockShared(&g_lock);
    return held && GetTickCount() - publishedAt <= maximumAgeMs;
}

} // namespace bfvr
