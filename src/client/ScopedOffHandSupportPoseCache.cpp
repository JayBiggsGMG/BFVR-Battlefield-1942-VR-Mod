#include "client/ScopedOffHandSupportPoseCache.h"

namespace
{

SRWLOCK g_lock = SRWLOCK_INIT;
bfvr::ScopedOffHandSupportPose g_pose = {};
DWORD g_publishedAt = 0;
bool g_valid = false;

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

} // namespace bfvr
