#include "client/WeaponPoseRuntimeCache.h"

namespace
{

struct WeaponViewOffsetState
{
    bfvr::stereo::Matrix4 viewOffset = {};
    bfvr::stereo::Matrix4 worldAttachment = {};
    bfvr::stereo::Matrix4 nativeHandWorld = {};
    bfvr::stereo::Matrix4 targetHandWorld = {};
    bfvr::stereo::Matrix4 controllerGunWorld = {};
    bfvr::stereo::Matrix4 controllerAimPointerWorld = {};
    const void* soldier = nullptr;
    const void* activeItem = nullptr;
    int activeItemIndex = -1;
    LONG controllerGeneration = 0;
    DWORD publishedAt = 0;
    bool viewOffsetValid = false;
    bool worldAttachmentValid = false;
    bool nativeArmPoseValid = false;
};

SRWLOCK g_lock = SRWLOCK_INIT;
WeaponViewOffsetState g_state = {};

void Publish(
    const bfvr::stereo::Matrix4& viewOffset,
    const bfvr::stereo::Matrix4& worldAttachment,
    LONG controllerGeneration,
    bool viewOffsetValid,
    bool worldAttachmentValid,
    const bfvr::stereo::Matrix4& nativeHandWorld = {},
    const bfvr::stereo::Matrix4& targetHandWorld = {},
    const bfvr::stereo::Matrix4& controllerGunWorld = {},
    const bfvr::stereo::Matrix4& controllerAimPointerWorld = {},
    const void* soldier = nullptr,
    const void* activeItem = nullptr,
    int activeItemIndex = -1,
    bool nativeArmPoseValid = false) noexcept
{
    AcquireSRWLockExclusive(&g_lock);
    g_state.viewOffset = viewOffset;
    g_state.worldAttachment = worldAttachment;
    g_state.nativeHandWorld = nativeHandWorld;
    g_state.targetHandWorld = targetHandWorld;
    g_state.controllerGunWorld = controllerGunWorld;
    g_state.controllerAimPointerWorld = controllerAimPointerWorld;
    g_state.soldier = soldier;
    g_state.activeItem = activeItem;
    g_state.activeItemIndex = activeItemIndex;
    g_state.controllerGeneration = controllerGeneration;
    g_state.publishedAt = GetTickCount();
    g_state.viewOffsetValid = viewOffsetValid;
    g_state.worldAttachmentValid = worldAttachmentValid;
    g_state.nativeArmPoseValid = nativeArmPoseValid;
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

void PublishNativeArmWeaponVisualPose(
    const stereo::Matrix4& viewOffset,
    const stereo::Matrix4& worldAttachment,
    const stereo::Matrix4& nativeHandWorld,
    const stereo::Matrix4& targetHandWorld,
    const stereo::Matrix4& controllerGunWorld,
    const stereo::Matrix4& controllerAimPointerWorld,
    const void* soldier,
    const void* activeItem,
    int activeItemIndex,
    LONG controllerGeneration) noexcept
{
    Publish(
        viewOffset,
        worldAttachment,
        controllerGeneration,
        true,
        true,
        nativeHandWorld,
        targetHandWorld,
        controllerGunWorld,
        controllerAimPointerWorld,
        soldier,
        activeItem,
        activeItemIndex,
        soldier != nullptr);
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

bool ReadFreshNativeArmWeaponVisualPose(
    NativeArmWeaponVisualPose& pose,
    DWORD maximumAgeMs) noexcept
{
    pose = {};
    AcquireSRWLockShared(&g_lock);
    const NativeArmWeaponVisualPose snapshot = {
        g_state.worldAttachment,
        g_state.nativeHandWorld,
        g_state.targetHandWorld,
        g_state.controllerGunWorld,
        g_state.controllerAimPointerWorld,
        g_state.soldier,
        g_state.activeItem,
        g_state.activeItemIndex,
        g_state.controllerGeneration};
    const DWORD publishedAt = g_state.publishedAt;
    const bool valid = g_state.nativeArmPoseValid;
    ReleaseSRWLockShared(&g_lock);
    if (!valid || GetTickCount() - publishedAt > maximumAgeMs)
    {
        return false;
    }
    pose = snapshot;
    return true;
}

} // namespace bfvr
