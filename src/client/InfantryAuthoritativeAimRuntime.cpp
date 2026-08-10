#include "client/InfantryAuthoritativeAimRuntime.h"

#include "client/MountedWeaponAimResolver.h"
#include "client/ScopeViewOverlay.h"
#include "client/WeaponPoseRuntimeCache.h"

#include <cmath>
#include <cstddef>

namespace
{

constexpr std::size_t kBFSoldierPitchDegreesOffset = 0x2B0;
constexpr std::size_t kBFSoldierRelativeYawDegreesOffset = 0x2B4;
constexpr DWORD kNativeArmPoseMaximumAgeMs = 125;
constexpr DWORD kNativeCameraMaximumAgeMs = 125;
constexpr float kMaximumPlausibleNativeAngleDegrees = 720.0F;

struct NativeAimCameraState
{
    bfvr::stereo::Matrix4 sourceCameraWorld = {};
    const void* soldier = nullptr;
    LONG renderSequence = 0;
    DWORD publishedAt = 0;
    bool valid = false;
};

SRWLOCK g_nativeAimCameraLock = SRWLOCK_INIT;
NativeAimCameraState g_nativeAimCamera = {};

bool ReadNativeAimAngles(
    const void* soldier,
    float& pitchDegrees,
    float& relativeYawDegrees) noexcept
{
    pitchDegrees = 0.0F;
    relativeYawDegrees = 0.0F;
    if (soldier == nullptr)
    {
        return false;
    }
    __try
    {
        const auto* const bytes = static_cast<const std::byte*>(soldier);
        pitchDegrees = *reinterpret_cast<const float*>(
            bytes + kBFSoldierPitchDegreesOffset);
        relativeYawDegrees = *reinterpret_cast<const float*>(
            bytes + kBFSoldierRelativeYawDegreesOffset);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        pitchDegrees = 0.0F;
        relativeYawDegrees = 0.0F;
        return false;
    }
    return std::isfinite(pitchDegrees) &&
        std::isfinite(relativeYawDegrees) &&
        std::fabs(pitchDegrees) <= kMaximumPlausibleNativeAngleDegrees &&
        std::fabs(relativeYawDegrees) <=
            kMaximumPlausibleNativeAngleDegrees;
}

bool ReadForward(
    const bfvr::stereo::Matrix4& matrix,
    bfvr::stereo::Vec3& forward) noexcept
{
    forward = {
        matrix.values[2][0],
        matrix.values[2][1],
        matrix.values[2][2]};
    const float lengthSquared =
        forward.x * forward.x + forward.y * forward.y +
        forward.z * forward.z;
    return std::isfinite(lengthSquared) &&
        lengthSquared >= 0.25F && lengthSquared <= 2.25F;
}

bool ReadFreshNativeAimCamera(
    const void* expectedSoldier,
    bfvr::stereo::Vec3& forward,
    LONG& renderSequence,
    DWORD& ageMs) noexcept
{
    forward = {};
    renderSequence = 0;
    ageMs = 0;
    AcquireSRWLockShared(&g_nativeAimCameraLock);
    const NativeAimCameraState snapshot = g_nativeAimCamera;
    ReleaseSRWLockShared(&g_nativeAimCameraLock);
    const DWORD now = GetTickCount();
    const DWORD age = now - snapshot.publishedAt;
    if (!snapshot.valid || snapshot.soldier != expectedSoldier ||
        snapshot.renderSequence <= 0 || age > kNativeCameraMaximumAgeMs ||
        !ReadForward(snapshot.sourceCameraWorld, forward))
    {
        return false;
    }
    renderSequence = snapshot.renderSequence;
    ageMs = age;
    return true;
}

bool IsGadgetPointerSlot(const int activeItemIndex) noexcept
{
    return activeItemIndex == 4 || activeItemIndex == 5 ||
        activeItemIndex == 6;
}

} // namespace

namespace bfvr
{

void PublishInfantryNativeAimCamera(
    const void* soldier,
    const stereo::Matrix4& sourceCameraWorld,
    const LONG renderSequence) noexcept
{
    stereo::Vec3 forward = {};
    const bool valid = soldier != nullptr && renderSequence > 0 &&
        ReadForward(sourceCameraWorld, forward);
    AcquireSRWLockExclusive(&g_nativeAimCameraLock);
    g_nativeAimCamera.sourceCameraWorld = sourceCameraWorld;
    g_nativeAimCamera.soldier = soldier;
    g_nativeAimCamera.renderSequence = renderSequence;
    g_nativeAimCamera.publishedAt = GetTickCount();
    g_nativeAimCamera.valid = valid;
    ReleaseSRWLockExclusive(&g_nativeAimCameraLock);
}

void ClearInfantryNativeAimCamera() noexcept
{
    AcquireSRWLockExclusive(&g_nativeAimCameraLock);
    g_nativeAimCamera = {};
    ReleaseSRWLockExclusive(&g_nativeAimCameraLock);
}

bool ReadInfantryAuthoritativeAimRuntimeSample(
    const void* expectedSoldier,
    InfantryAuthoritativeAimRuntimeSample& sample) noexcept
{
    sample = {};
    if (expectedSoldier == nullptr)
    {
        return false;
    }

    LocalInfantryBodyPose body = {};
    if (!ReadLocalInfantryBodyPose(body) ||
        body.controlObject != expectedSoldier)
    {
        return false;
    }
    const float bodyForwardX = body.world.values[2][0];
    const float bodyForwardZ = body.world.values[2][2];
    const float bodyHorizontalLength =
        std::hypot(bodyForwardX, bodyForwardZ);
    if (!std::isfinite(bodyHorizontalLength) ||
        bodyHorizontalLength < 0.5F)
    {
        return false;
    }

    float nativePitchDegrees = 0.0F;
    float nativeYawOffsetDegrees = 0.0F;
    if (!ReadNativeAimAngles(
            expectedSoldier,
            nativePitchDegrees,
            nativeYawOffsetDegrees))
    {
        return false;
    }

    stereo::Vec3 nativeCameraForward = {};
    LONG nativeCameraRenderSequence = 0;
    DWORD nativeCameraAgeMs = 0;
    if (!ReadFreshNativeAimCamera(
            expectedSoldier,
            nativeCameraForward,
            nativeCameraRenderSequence,
            nativeCameraAgeMs))
    {
        return false;
    }

    stereo::Matrix4 target = {};
    const void* item = nullptr;
    LONG targetControllerGeneration = 0;
    InfantryAuthoritativeAimTargetKind targetKind =
        InfantryAuthoritativeAimTargetKind::FunctionalWeapon;
    ScopeViewFrameState scope = {};
    if (ReadScopeViewFrameState(scope) &&
        scope.soldier == expectedSoldier && scope.weapon != nullptr &&
        scope.controllerGeneration > 0)
    {
        target = scope.controllerGunWorld;
        item = scope.weapon;
        targetControllerGeneration = scope.controllerGeneration;
        targetKind = InfantryAuthoritativeAimTargetKind::ScopedWeapon;
    }
    else
    {
        NativeArmWeaponVisualPose nativePose = {};
        if (!ReadFreshNativeArmWeaponVisualPose(
                nativePose,
                kNativeArmPoseMaximumAgeMs) ||
            nativePose.soldier != expectedSoldier ||
            nativePose.activeItem == nullptr ||
            nativePose.activeItemIndex < 0 ||
            nativePose.controllerGeneration <= 0)
        {
            return false;
        }
        const bool gadgetPointer =
            IsGadgetPointerSlot(nativePose.activeItemIndex);
        target = gadgetPointer
            ? nativePose.controllerAimPointerWorld
            : nativePose.controllerGunWorld;
        item = nativePose.activeItem;
        targetControllerGeneration = nativePose.controllerGeneration;
        targetKind = gadgetPointer
            ? InfantryAuthoritativeAimTargetKind::GadgetPointer
            : InfantryAuthoritativeAimTargetKind::FunctionalWeapon;
    }

    stereo::Vec3 targetForward = {};
    if (!ReadForward(target, targetForward))
    {
        return false;
    }

    sample.targetForwardWorld = targetForward;
    sample.nativeCameraForwardWorld = nativeCameraForward;
    sample.soldier = expectedSoldier;
    sample.item = item;
    sample.targetControllerGeneration = targetControllerGeneration;
    sample.nativeCameraRenderSequence = nativeCameraRenderSequence;
    sample.nativeCameraAgeMs = nativeCameraAgeMs;
    sample.bodyYawRadians = std::atan2(bodyForwardX, bodyForwardZ);
    sample.nativePitchDegrees = nativePitchDegrees;
    sample.nativeYawOffsetDegrees = nativeYawOffsetDegrees;
    const float nativeCameraLength = std::sqrt(
        nativeCameraForward.x * nativeCameraForward.x +
        nativeCameraForward.y * nativeCameraForward.y +
        nativeCameraForward.z * nativeCameraForward.z);
    const float currentForwardX =
        nativeCameraForward.x / nativeCameraLength;
    const float currentForwardY =
        nativeCameraForward.y / nativeCameraLength;
    const float currentForwardZ =
        nativeCameraForward.z / nativeCameraLength;
    sample.currentYawRadians = std::atan2(
        currentForwardX,
        currentForwardZ);
    sample.currentPitchRadians = std::atan2(
        currentForwardY,
        std::hypot(currentForwardX, currentForwardZ));
    sample.targetKind = targetKind;
    return std::isfinite(sample.currentYawRadians) &&
        std::isfinite(sample.currentPitchRadians);
}

const wchar_t* InfantryAuthoritativeAimTargetKindName(
    const InfantryAuthoritativeAimTargetKind kind) noexcept
{
    switch (kind)
    {
    case InfantryAuthoritativeAimTargetKind::FunctionalWeapon:
        return L"functional-weapon";
    case InfantryAuthoritativeAimTargetKind::GadgetPointer:
        return L"gadget-pointer";
    case InfantryAuthoritativeAimTargetKind::ScopedWeapon:
        return L"scoped-weapon";
    default:
        return L"unknown";
    }
}

} // namespace bfvr
