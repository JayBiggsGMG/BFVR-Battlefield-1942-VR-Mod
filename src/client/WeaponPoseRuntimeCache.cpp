#include "client/WeaponPoseRuntimeCache.h"

namespace
{

struct WeaponViewOffsetState
{
    bfvr::stereo::Matrix4 viewOffset = {};
    bfvr::stereo::Matrix4 worldAttachment = {};
    LONG controllerGeneration = 0;
    DWORD publishedAt = 0;
    bool viewOffsetValid = false;
    bool worldAttachmentValid = false;
};

SRWLOCK g_lock = SRWLOCK_INIT;
WeaponViewOffsetState g_state = {};

void Publish(
    const bfvr::stereo::Matrix4& viewOffset,
    const bfvr::stereo::Matrix4& worldAttachment,
    LONG controllerGeneration,
    bool viewOffsetValid,
    bool worldAttachmentValid) noexcept
{
    AcquireSRWLockExclusive(&g_lock);
    g_state.viewOffset = viewOffset;
    g_state.worldAttachment = worldAttachment;
    g_state.controllerGeneration = controllerGeneration;
    g_state.publishedAt = GetTickCount();
    g_state.viewOffsetValid = viewOffsetValid;
    g_state.worldAttachmentValid = worldAttachmentValid;
    ReleaseSRWLockExclusive(&g_lock);
}

} // namespace

namespace bfvr
{

void PublishWeaponVisualPose(
    const stereo::Matrix4& viewOffset,
    const stereo::Matrix4& worldAttachment,
    LONG controllerGeneration) noexcept
{
    Publish(
        viewOffset,
        worldAttachment,
        controllerGeneration,
        true,
        true);
}

void PublishWeaponViewOffset(
    const stereo::Matrix4& viewOffset,
    LONG controllerGeneration) noexcept
{
    // A view-space offset alone is insufficient to align a fire matrix with a
    // source-view-conjugated visual attachment. Preserve the legacy read for
    // callers that need only the offset, but make exact world consumers wait
    // for a paired PublishWeaponVisualPose call.
    Publish(viewOffset, {}, controllerGeneration, true, false);
}

void ClearWeaponViewOffset() noexcept
{
    Publish({}, {}, 0, false, false);
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
    const bool valid = g_state.viewOffsetValid;
    ReleaseSRWLockShared(&g_lock);
    if (!valid || GetTickCount() - publishedAt > maximumAgeMs)
    {
        return false;
    }
    viewOffset = snapshot;
    controllerGeneration = snapshotGeneration;
    return true;
}

bool ReadFreshWeaponWorldAttachment(
    stereo::Matrix4& worldAttachment,
    LONG& controllerGeneration,
    DWORD maximumAgeMs) noexcept
{
    worldAttachment = {};
    controllerGeneration = 0;
    AcquireSRWLockShared(&g_lock);
    const stereo::Matrix4 snapshot = g_state.worldAttachment;
    const LONG snapshotGeneration = g_state.controllerGeneration;
    const DWORD publishedAt = g_state.publishedAt;
    const bool valid = g_state.worldAttachmentValid;
    ReleaseSRWLockShared(&g_lock);
    if (!valid || GetTickCount() - publishedAt > maximumAgeMs)
    {
        return false;
    }
    worldAttachment = snapshot;
    controllerGeneration = snapshotGeneration;
    return true;
}

} // namespace bfvr
