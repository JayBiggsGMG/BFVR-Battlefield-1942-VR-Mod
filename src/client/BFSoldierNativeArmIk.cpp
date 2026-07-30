#include "client/BFSoldierNativeArmIk.h"

#include "client/BFSoldierVrMotionFilter.h"
#include "client/ControllerInputCache.h"
#include "client/WeaponPoseRuntimeCache.h"
#include "presenter/SharedPresentationProtocol.h"
#include "stereo/StereoMath.h"
#include "stereo/WeaponFireAimMath.h"
#include "stereo/WeaponPoseMath.h"

#include <MinHook.h>

#include <windows.h>

#include <array>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <utility>

namespace
{

constexpr wchar_t kEnableNativeArmIkEnvironment[] =
    L"BFVR_ENABLE_NATIVE_1P_ARMS_IK";
constexpr std::ptrdiff_t kSkeletonTransformRva = 0x00211690;
constexpr std::ptrdiff_t kSkeletonApplyIkRva = 0x002123B0;
constexpr std::ptrdiff_t kAnimatedBundleSetRelativeBoneTransformRva =
    0x0014ECC0;
constexpr std::ptrdiff_t kBFSoldierGetPoseRva = 0x000F6CA0;
constexpr std::ptrdiff_t kBFSoldierGetPoseCameraPositionRva = 0x000F6CC0;
constexpr std::ptrdiff_t kActiveItemAttachmentCallerReturnRva =
    0x000FBC4B;
constexpr std::size_t kAnimatedBundleInterfaceOffset = 0x11C;
constexpr std::size_t kSoldierTemplateOffset = 0x4C;
constexpr std::size_t kSoldierFirstPersonStateOffset = 0x290;
constexpr std::size_t kSoldierAnimationSkeletonOffset = 0x298;
constexpr std::ptrdiff_t kPlayerManagerGlobalRva = 0x0057D76C;
constexpr std::size_t kPlayerManagerLocalPlayerOffset = 0x54;
constexpr std::size_t kBFPlayerIsAliveOffset = 0xA9;
constexpr std::size_t kTemplateRightHandBoneOffset = 0x33C;
constexpr std::size_t kSkeletonBoneRecordsOffset = 0x0;
constexpr std::size_t kSkeletonBoneCountOffset = 0x4;
constexpr std::size_t kSkeletonIkHandleBeginOffset = 0xC;
constexpr std::size_t kSkeletonIkHandleEndOffset = 0x10;
constexpr std::size_t kBoneRecordStride = 0xE8;
constexpr std::size_t kBoneFinalMatrixOffset = 0x48;
// The final hand matrix begins at +0x48. Its translation row is therefore
// +0x78, rather than a distinct endpoint field. The two-bone solver reads
// that row from the final matrix of its end bone as the current hand point.
constexpr std::size_t kBoneFinalTranslationOffset = 0x78;
constexpr std::size_t kBoneIkHandleIndexOffset = 0xE0;
constexpr std::size_t kIkHandleStride = 0x50;
constexpr std::size_t kMaximumBones = 256;
constexpr std::size_t kMaximumIkHandles = 512;
constexpr DWORD kControllerSampleMaximumAgeMs = 125;
constexpr DWORD kRightControllerHand = 1;
constexpr float kBf1942WorldUnitsPerMeter = 1.0F;
// BF1942's authored floating-arm root sits too far behind the VR head. Move
// the entire native first-person arm skeleton forward together so the
// shoulder, solved hand, and game-attached weapon retain their relationships.
constexpr float kFirstPersonArmRootForwardOffset = 0.15F;
// PID 33220's owner-identified closest physical-over-virtual calibration:
// native hand (0.1392, 0.3777, 0.4687) minus raw grip
// (0.0419, -0.2990, 0.5986). This is one global tracking-to-Skeleton frame
// translation, never an arm/faction/weapon asset offset.
constexpr std::array<float, 3> kTrackingToSkeletonPositionOffset = {
    0.0973F,
    0.6767F,
    -0.1299F};
constexpr float kMotionProbeMinimumDelta = 0.050F;
constexpr float kMotionProbeRepeatDelta = 0.150F;
constexpr LONG kMaximumMotionProbeReports = 12;
constexpr LONG kStandingPose = 0;
constexpr LONG kLastSupportedPose = 2;
constexpr float kMaximumPoseCameraTranslation = 3.0F;

constexpr BYTE kSkeletonTransformPrefix[] = {
    0x81, 0xEC, 0x90, 0x00, 0x00, 0x00, 0x8B, 0xD1,
    0x8B, 0x42, 0x04, 0x33, 0xC9, 0x85, 0xC0, 0x89,
    0x54, 0x24, 0x04, 0x89, 0x4C, 0x24, 0x0C, 0x0F,
    0x8E, 0xDB, 0x01, 0x00, 0x00, 0x53, 0x55, 0x56};
constexpr BYTE kSkeletonApplyIkPrefix[] = {
    0x53, 0x55, 0x56, 0x57, 0x8B, 0xF9, 0x8B, 0x47,
    0x0C, 0x85, 0xC0, 0x8D, 0x4F, 0x08, 0x75, 0x04,
    0x33, 0xF6, 0xEB, 0x18};
constexpr BYTE kAnimatedBundleSetRelativeBoneTransformPrefix[] = {
    0x8B, 0x41, 0x10, 0x85, 0xC0, 0x74, 0x1F, 0x8B,
    0x4C, 0x24, 0x04, 0x8B, 0x10, 0x69, 0xC9, 0xE8,
    0x00, 0x00, 0x00, 0x56, 0x8B, 0x74, 0x24, 0x0C,
    0x57, 0x8D, 0x7C, 0x11, 0x08, 0xB9, 0x10, 0x00,
    0x00, 0x00, 0xF3, 0xA5, 0x5F, 0x5E, 0xC2, 0x08,
    0x00};
constexpr BYTE kBFSoldierGetPosePrefix[] = {
    0x81, 0xC1, 0xC0, 0x02, 0x00, 0x00, 0xE8, 0x95,
    0xC7, 0x11, 0x00, 0xA8, 0x20, 0x74, 0x06, 0xB8,
    0x01, 0x00, 0x00, 0x00, 0xC3, 0x0F, 0xBE, 0xC0,
    0x83, 0xE0, 0x40, 0xC1, 0xE8, 0x05, 0xC3};
constexpr BYTE kBFSoldierGetPoseCameraPositionPrefix[] = {
    0x8B, 0x44, 0x24, 0x04, 0x8B, 0x49, 0x4C, 0x8D,
    0x04, 0x40, 0x8D, 0x84, 0x81, 0x54, 0x02, 0x00,
    0x00, 0xC2, 0x04, 0x00};

using Matrix4 = bfvr::stereo::Matrix4;

bool IsFinite(const float value) noexcept
{
    return std::isfinite(value);
}

bool IsFinite(const Matrix4& matrix) noexcept
{
    for (const auto& row : matrix.values)
    {
        for (const float value : row)
        {
            if (!IsFinite(value))
            {
                return false;
            }
        }
    }
    return true;
}

bool HasExpectedPrefix(
    const void* target,
    const BYTE* expected,
    const std::size_t length) noexcept
{
    if (target == nullptr || expected == nullptr || length == 0)
    {
        return false;
    }
    __try
    {
        return std::memcmp(target, expected, length) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

Matrix4 Multiply(const Matrix4& left, const Matrix4& right) noexcept
{
    Matrix4 result = {};
    for (std::size_t row = 0; row < result.values.size(); ++row)
    {
        for (std::size_t column = 0; column < result.values[row].size(); ++column)
        {
            for (std::size_t inner = 0; inner < result.values.size(); ++inner)
            {
                result.values[row][column] +=
                    left.values[row][inner] * right.values[inner][column];
            }
        }
    }
    return result;
}

Matrix4 IdentityMatrix() noexcept
{
    Matrix4 result = {};
    for (std::size_t index = 0; index < 4; ++index)
    {
        result.values[index][index] = 1.0F;
    }
    return result;
}

std::optional<Matrix4> Invert(const Matrix4& matrix) noexcept
{
    if (!IsFinite(matrix))
    {
        return std::nullopt;
    }
    std::array<std::array<float, 8>, 4> augmented = {};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            augmented[row][column] = matrix.values[row][column];
            augmented[row][column + 4] = row == column ? 1.0F : 0.0F;
        }
    }

    constexpr float kPivotEpsilon = 0.000001F;
    for (std::size_t column = 0; column < 4; ++column)
    {
        std::size_t pivotRow = column;
        for (std::size_t candidate = column + 1; candidate < 4; ++candidate)
        {
            if (std::fabs(augmented[candidate][column]) >
                std::fabs(augmented[pivotRow][column]))
            {
                pivotRow = candidate;
            }
        }
        const float pivot = augmented[pivotRow][column];
        if (!IsFinite(pivot) || std::fabs(pivot) <= kPivotEpsilon)
        {
            return std::nullopt;
        }
        if (pivotRow != column)
        {
            std::swap(augmented[pivotRow], augmented[column]);
        }
        for (float& value : augmented[column])
        {
            value /= pivot;
        }
        for (std::size_t row = 0; row < 4; ++row)
        {
            if (row == column)
            {
                continue;
            }
            const float factor = augmented[row][column];
            for (std::size_t index = 0; index < augmented[row].size(); ++index)
            {
                augmented[row][index] -= factor * augmented[column][index];
            }
        }
    }

    Matrix4 inverse = {};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            inverse.values[row][column] = augmented[row][column + 4];
        }
    }
    return IsFinite(inverse) ? std::optional<Matrix4>(inverse) : std::nullopt;
}

bool IsTrackedGrip(const bfvr::D3D8RuntimeControllerHand& hand) noexcept
{
    constexpr DWORD kRequiredGripFlags =
        bfvr::shared::kControllerHandFlagGripActive |
        bfvr::shared::kControllerHandFlagGripPositionValid |
        bfvr::shared::kControllerHandFlagGripOrientationValid |
        bfvr::shared::kControllerHandFlagGripPositionTracked |
        bfvr::shared::kControllerHandFlagGripOrientationTracked;
    return (hand.flags & kRequiredGripFlags) == kRequiredGripFlags;
}

bool IsTrackedAim(const bfvr::D3D8RuntimeControllerHand& hand) noexcept
{
    constexpr DWORD kRequiredAimFlags =
        bfvr::shared::kControllerHandFlagAimActive |
        bfvr::shared::kControllerHandFlagAimPositionValid |
        bfvr::shared::kControllerHandFlagAimOrientationValid |
        bfvr::shared::kControllerHandFlagAimPositionTracked |
        bfvr::shared::kControllerHandFlagAimOrientationTracked;
    return (hand.flags & kRequiredAimFlags) == kRequiredAimFlags;
}

struct ArmIkRestore
{
    std::byte* boneRecord = nullptr;
    LONG previousHandleIndex = -1;
    std::array<float, 3> targetPosition = {};
    std::array<float, 3> targetDelta = {};
    std::array<float, 3> gripDelta = {};
    LONG controllerGeneration = 0;
    bool captureMotionProbe = false;
    bool active = false;
};

struct PendingLocalActiveItemAttachment
{
    void* soldier = nullptr;
    const Matrix4* handWorld = nullptr;
};

struct PoseCameraTranslation
{
    std::array<float, 3> localDelta = {};
    LONG pose = kStandingPose;
};

float DistanceSquared(
    const std::array<float, 3>& left,
    const std::array<float, 3>& right) noexcept
{
    const float x = left[0] - right[0];
    const float y = left[1] - right[1];
    const float z = left[2] - right[2];
    return x * x + y * y + z * z;
}

class NativeArmIk
{
public:
    using SkeletonTransformFn = void(__thiscall*)(void*, const Matrix4*, LONG);
    using ApplyIkFn = void(__thiscall*)(void*, LONG, const float*, const Matrix4*);
    using GetTransformationFn = const Matrix4*(__thiscall*)(void*);
    using GetSoldierPoseFn = LONG(__thiscall*)(void*);
    using GetPoseCameraPositionFn =
        const float*(__thiscall*)(void*, LONG);
    using SetRelativeBoneTransformFn =
        void(__thiscall*)(void*, LONG, const Matrix4*);

    bool Start(void* image, void (*log)(const wchar_t* message))
    {
        if (InterlockedCompareExchange(&started_, 1, 0) != 0)
        {
            return enabled_;
        }
        appendLog_ = log;
        wchar_t enabled[2] = {};
        if (GetEnvironmentVariableW(
                kEnableNativeArmIkEnvironment,
                enabled,
                static_cast<DWORD>(std::size(enabled))) != 1 ||
            enabled[0] != L'1')
        {
            InterlockedExchange(&started_, 0);
            return true;
        }

        gameImage_ = static_cast<std::byte*>(image);
        skeletonTransformTarget_ = gameImage_ == nullptr
            ? nullptr
            : gameImage_ + kSkeletonTransformRva;
        applyIkTarget_ = gameImage_ == nullptr
            ? nullptr
            : gameImage_ + kSkeletonApplyIkRva;
        setRelativeBoneTransformTarget_ = gameImage_ == nullptr
            ? nullptr
            : gameImage_ + kAnimatedBundleSetRelativeBoneTransformRva;
        getSoldierPoseTarget_ = gameImage_ == nullptr
            ? nullptr
            : gameImage_ + kBFSoldierGetPoseRva;
        getPoseCameraPositionTarget_ = gameImage_ == nullptr
            ? nullptr
            : gameImage_ + kBFSoldierGetPoseCameraPositionRva;
        if (!HasExpectedPrefix(
                skeletonTransformTarget_,
                kSkeletonTransformPrefix,
                sizeof(kSkeletonTransformPrefix)) ||
            !HasExpectedPrefix(
                applyIkTarget_,
                kSkeletonApplyIkPrefix,
                sizeof(kSkeletonApplyIkPrefix)) ||
            !HasExpectedPrefix(
                setRelativeBoneTransformTarget_,
                kAnimatedBundleSetRelativeBoneTransformPrefix,
                sizeof(kAnimatedBundleSetRelativeBoneTransformPrefix)) ||
            !HasExpectedPrefix(
                getSoldierPoseTarget_,
                kBFSoldierGetPosePrefix,
                sizeof(kBFSoldierGetPosePrefix)) ||
            !HasExpectedPrefix(
                getPoseCameraPositionTarget_,
                kBFSoldierGetPoseCameraPositionPrefix,
                sizeof(kBFSoldierGetPoseCameraPositionPrefix)))
        {
            WriteLog(
                L"Native 1P arm IK rejected the profiled WinPC targets: skeletonTransform=%p applyIk=%p setRelativeBoneTransform=%p getPose=%p getPoseCameraPosition=%p.",
                skeletonTransformTarget_,
                applyIkTarget_,
                setRelativeBoneTransformTarget_,
                getSoldierPoseTarget_,
                getPoseCameraPositionTarget_);
            Reset();
            return false;
        }

        const MH_STATUS createSkeletonStatus = MH_CreateHook(
            skeletonTransformTarget_,
            reinterpret_cast<LPVOID>(&NativeArmIk::SkeletonTransformHook),
            reinterpret_cast<LPVOID*>(&originalSkeletonTransform_));
        if (createSkeletonStatus != MH_OK ||
            originalSkeletonTransform_ == nullptr)
        {
            WriteLog(
                L"Native 1P arm IK could not create its Skeleton::transform hook (status=%d).",
                static_cast<int>(createSkeletonStatus));
            Reset();
            return false;
        }
        skeletonHookCreated_ = true;
        const MH_STATUS createAttachmentStatus = MH_CreateHook(
            setRelativeBoneTransformTarget_,
            reinterpret_cast<LPVOID>(
                &NativeArmIk::SetRelativeBoneTransformHook),
            reinterpret_cast<LPVOID*>(
                &originalSetRelativeBoneTransform_));
        if (createAttachmentStatus != MH_OK ||
            originalSetRelativeBoneTransform_ == nullptr)
        {
            WriteLog(
                L"Native 1P arm IK could not create its active-item attachment hook (status=%d).",
                static_cast<int>(createAttachmentStatus));
            RemoveHooks();
            Reset();
            return false;
        }
        attachmentHookCreated_ = true;
        active_ = this;
        const MH_STATUS enableAttachmentStatus =
            MH_EnableHook(setRelativeBoneTransformTarget_);
        const MH_STATUS enableSkeletonStatus =
            enableAttachmentStatus == MH_OK
            ? MH_EnableHook(skeletonTransformTarget_)
            : MH_ERROR_DISABLED;
        if (enableAttachmentStatus != MH_OK ||
            enableSkeletonStatus != MH_OK)
        {
            WriteLog(
                L"Native 1P arm IK could not enable both profiled hooks (attachment=%d skeleton=%d).",
                static_cast<int>(enableAttachmentStatus),
                static_cast<int>(enableSkeletonStatus));
            if (enableAttachmentStatus == MH_OK)
            {
                MH_DisableHook(setRelativeBoneTransformTarget_);
            }
            RemoveHooks();
            Reset();
            return false;
        }
        attachmentHookEnabled_ = true;
        skeletonHookEnabled_ = true;
        enabled_ = true;
        WriteLog(
            L"Native 1P right-arm IK armed at Skeleton::transform and the exact active-item AnimatedBundle attachment callback. OpenXR aim owns the held gun/fire basis continuously; BF1942's selected item supplies its own native hand-from-fire relation before IK is enabled, with no shot, spawn-camera, or process-global calibration. BF1942's authored crouch/prone camera translation is inherited without changing controller orientation. The complete native 1P arm root is shifted %.2f metres forward for VR shoulder placement. Existing authored IK targets are left untouched.",
            kFirstPersonArmRootForwardOffset);
        return true;
    }

    void Stop()
    {
        if (skeletonHookEnabled_)
        {
            MH_DisableHook(skeletonTransformTarget_);
            skeletonHookEnabled_ = false;
        }
        if (attachmentHookEnabled_)
        {
            MH_DisableHook(setRelativeBoneTransformTarget_);
            attachmentHookEnabled_ = false;
        }
        if (active_ == this)
        {
            active_ = nullptr;
        }
        while (InterlockedCompareExchange(&callbackEntrants_, 0, 0) != 0)
        {
            Sleep(0);
        }
        bfvr::ClearWeaponViewOffset();
        if (enabled_)
        {
            WriteLog(
                L"Native 1P arm IK stopped: localTransforms=%ld rootShifted=%ld injected=%ld lifetimeBindings=%ld nativePoseCaptures=%ld trackingRejected=%ld deadPlayerRejected=%ld nativeTargetPreserved=%ld matrixRejected=%ld callFailures=%ld restoreFailures=%ld motionProbes=%ld activeItemChanges=%ld activeItemAlignments=%ld activeItemAlignmentFailures=%ld stanceTranslatedFrames=%ld stanceTransitions=%ld stanceReadFailures=%ld.",
                localUpdates_,
                rootShiftedFrames_,
                injectedFrames_,
                calibrationCommits_,
                nativePoseCaptures_,
                trackingRejected_,
                deadPlayerRejected_,
                nativeTargetPreserved_,
                matrixRejected_,
                applyFailures_,
                restoreFailures_,
                motionProbeReports_,
                activeItemChanges_,
                activeItemAlignments_,
                activeItemAlignmentFailures_,
                stanceTranslatedFrames_,
                stanceTransitions_,
                stanceReadFailures_);
        }
        RemoveHooks();
        Reset();
    }

private:
    static void __fastcall SkeletonTransformHook(
        void* skeleton,
        void*,
        const Matrix4* rootTransform,
        LONG transformLimit)
    {
        NativeArmIk* const self = active_;
        if (self == nullptr || self->originalSkeletonTransform_ == nullptr)
        {
            return;
        }
        InterlockedIncrement(&self->callbackEntrants_);
        pendingLocalActiveItemAttachment_ = {};
        ArmIkRestore restore = {};
        __try
        {
            Matrix4 adjustedRoot = {};
            const Matrix4* effectiveRootTransform = rootTransform;
            if (self->TryMakeForwardShiftedRoot(
                    skeleton,
                    rootTransform,
                    adjustedRoot))
            {
                effectiveRootTransform = &adjustedRoot;
                InterlockedIncrement(&self->rootShiftedFrames_);
            }
            self->TryInject(skeleton, restore);
            self->originalSkeletonTransform_(
                skeleton,
                effectiveRootTransform,
                transformLimit);
            self->CaptureInjectedMotionProbe(restore);
        }
        __finally
        {
            self->Restore(restore);
            if (!restore.active)
            {
                self->CaptureNativePose(skeleton);
            }
            self->ArmLocalActiveItemAttachmentObservation(skeleton);
            InterlockedDecrement(&self->callbackEntrants_);
        }
    }

    static void __fastcall SetRelativeBoneTransformHook(
        void* animatedBundleInterface,
        void*,
        LONG boneIndex,
        const Matrix4* matrix)
    {
        NativeArmIk* const self = active_;
        if (self == nullptr ||
            self->originalSetRelativeBoneTransform_ == nullptr)
        {
            return;
        }
        InterlockedIncrement(&self->callbackEntrants_);
        __try
        {
            self->ObserveActiveItemAttachment(
                animatedBundleInterface,
                boneIndex,
                matrix,
                _ReturnAddress());
            self->originalSetRelativeBoneTransform_(
                animatedBundleInterface,
                boneIndex,
                matrix);
        }
        __finally
        {
            InterlockedDecrement(&self->callbackEntrants_);
        }
    }

    void ObserveActiveItemAttachment(
        void* animatedBundleInterface,
        LONG boneIndex,
        const Matrix4* handWorld,
        const void* callerReturn) noexcept
    {
        if (gameImage_ == nullptr || animatedBundleInterface == nullptr ||
            handWorld == nullptr || boneIndex != 0 ||
            callerReturn !=
                gameImage_ + kActiveItemAttachmentCallerReturnRva)
        {
            return;
        }
        const PendingLocalActiveItemAttachment pending =
            pendingLocalActiveItemAttachment_;
        pendingLocalActiveItemAttachment_ = {};
        void* const soldier = pending.soldier;
        if (soldier == nullptr || pending.handWorld != handWorld ||
            soldier != bfvr::ReadCurrentBFSoldierVrCameraSoldier() ||
            !IsLocalPlayerAlive())
        {
            return;
        }

        auto* const item = static_cast<std::byte*>(
            animatedBundleInterface) - kAnimatedBundleInterfaceOffset;
        bool shouldCapture = false;
        AcquireSRWLockExclusive(&activeItemAlignmentLock_);
        if (activeItemSoldier_ != soldier ||
            activeItemInterface_ != animatedBundleInterface)
        {
            activeItemSoldier_ = soldier;
            activeItemInterface_ = animatedBundleInterface;
            activeItem_ = item;
            activeItemNativeWarmupCallbacks_ = 0;
            activeItemAlignmentValid_ = false;
            activeItemHandFromFire_ = {};
            InterlockedIncrement(&activeItemChanges_);
            if (InterlockedIncrement(&loggedActiveItemChanges_) <= 8)
            {
                WriteLog(
                    L"Native 1P arm observed a new game-selected active item: soldier=%p item=%p interface=%p. Controller IK is withheld while BF1942 supplies a native attachment sample; no shot is required.",
                    soldier,
                    item,
                    animatedBundleInterface);
            }
        }
        else if (!activeItemAlignmentValid_)
        {
            ++activeItemNativeWarmupCallbacks_;
            // The first callback after a switch may still carry the prior
            // item's controller-driven hand. Leave two complete native
            // attachment updates in place before sampling this item's own
            // authored hand/fire relationship.
            shouldCapture = activeItemNativeWarmupCallbacks_ >= 2;
        }
        ReleaseSRWLockExclusive(&activeItemAlignmentLock_);
        if (!shouldCapture)
        {
            return;
        }

        Matrix4 nativeHandLocal = {};
        if (!SafeCopyMatrix(handWorld, nativeHandLocal))
        {
            InterlockedIncrement(&activeItemAlignmentFailures_);
            return;
        }
        const auto soldierTransform = ReadSoldierTransform(soldier);
        if (!soldierTransform.has_value())
        {
            InterlockedIncrement(&activeItemAlignmentFailures_);
            return;
        }
        const Matrix4 nativeHandWorld = Multiply(
            nativeHandLocal,
            *soldierTransform);
        const auto nativeFireWorld = ReadObjectTransform(item);
        if (!nativeFireWorld.has_value() || !IsFinite(nativeHandWorld))
        {
            InterlockedIncrement(&activeItemAlignmentFailures_);
            return;
        }
        const auto handFromFire =
            bfvr::stereo::MakeD3D8NativeHandFromFireRotation(
                *nativeFireWorld,
                nativeHandWorld);
        if (!handFromFire.has_value())
        {
            InterlockedIncrement(&activeItemAlignmentFailures_);
            return;
        }

        bool published = false;
        AcquireSRWLockExclusive(&activeItemAlignmentLock_);
        if (activeItemSoldier_ == soldier &&
            activeItemInterface_ == animatedBundleInterface &&
            activeItem_ == item && !activeItemAlignmentValid_)
        {
            activeItemHandFromFire_ = *handFromFire;
            activeItemAlignmentValid_ = true;
            published = true;
        }
        ReleaseSRWLockExclusive(&activeItemAlignmentLock_);
        if (published)
        {
            InterlockedIncrement(&activeItemAlignments_);
            WriteLog(
                L"Native 1P arm adopted the selected item's authored hand-from-fire rotation before firing: soldier=%p item=%p nativeHand=(%.4f,%.4f,%.4f) nativeFire=(%.4f,%.4f,%.4f). OpenXR aim now owns this item's gun and projectile basis.",
                soldier,
                item,
                nativeHandWorld.values[3][0],
                nativeHandWorld.values[3][1],
                nativeHandWorld.values[3][2],
                nativeFireWorld->values[3][0],
                nativeFireWorld->values[3][1],
                nativeFireWorld->values[3][2]);
        }
    }

    void ArmLocalActiveItemAttachmentObservation(void* skeleton) noexcept
    {
        pendingLocalActiveItemAttachment_ = {};
        void* const soldier = bfvr::ReadCurrentBFSoldierVrCameraSoldier();
        if (skeleton == nullptr || soldier == nullptr || !IsLocalPlayerAlive())
        {
            return;
        }
        __try
        {
            const auto* const soldierBytes =
                static_cast<const std::byte*>(soldier);
            if (soldierBytes[kSoldierFirstPersonStateOffset] == std::byte{0} ||
                *reinterpret_cast<void* const*>(
                    soldierBytes + kSoldierAnimationSkeletonOffset) !=
                    skeleton)
            {
                return;
            }
            const void* const soldierTemplate =
                *reinterpret_cast<void* const*>(
                    soldierBytes + kSoldierTemplateOffset);
            if (soldierTemplate == nullptr)
            {
                return;
            }
            const LONG handBone = *reinterpret_cast<const LONG*>(
                static_cast<const std::byte*>(soldierTemplate) +
                kTemplateRightHandBoneOffset);
            const LONG boneCount = *reinterpret_cast<const LONG*>(
                static_cast<const std::byte*>(skeleton) +
                kSkeletonBoneCountOffset);
            std::byte* const boneRecords =
                *reinterpret_cast<std::byte* const*>(
                    static_cast<const std::byte*>(skeleton) +
                    kSkeletonBoneRecordsOffset);
            if (handBone < 0 || handBone >= boneCount ||
                handBone >= static_cast<LONG>(kMaximumBones) ||
                boneRecords == nullptr)
            {
                return;
            }
            pendingLocalActiveItemAttachment_ = {
                soldier,
                reinterpret_cast<const Matrix4*>(
                    boneRecords +
                    static_cast<std::size_t>(handBone) * kBoneRecordStride +
                    kBoneFinalMatrixOffset)};
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            pendingLocalActiveItemAttachment_ = {};
        }
    }

    bool TryMakeForwardShiftedRoot(
        void* skeleton,
        const Matrix4* rootTransform,
        Matrix4& adjustedRoot) noexcept
    {
        adjustedRoot = {};
        void* const soldier = bfvr::ReadCurrentBFSoldierVrCameraSoldier();
        if (skeleton == nullptr || rootTransform == nullptr || soldier == nullptr ||
            !IsLocalPlayerAlive())
        {
            return false;
        }

        __try
        {
            const auto* const soldierBytes = static_cast<const std::byte*>(soldier);
            if (soldierBytes[kSoldierFirstPersonStateOffset] == std::byte{0} ||
                *reinterpret_cast<void* const*>(
                    soldierBytes + kSoldierAnimationSkeletonOffset) != skeleton)
            {
                return false;
            }

            const void* const soldierTemplate = *reinterpret_cast<void* const*>(
                soldierBytes + kSoldierTemplateOffset);
            if (soldierTemplate == nullptr)
            {
                return false;
            }
            const LONG handBone = *reinterpret_cast<const LONG*>(
                static_cast<const std::byte*>(soldierTemplate) +
                kTemplateRightHandBoneOffset);
            const LONG boneCount = *reinterpret_cast<const LONG*>(
                static_cast<const std::byte*>(skeleton) + kSkeletonBoneCountOffset);
            std::byte* const boneRecords = *reinterpret_cast<std::byte* const*>(
                static_cast<const std::byte*>(skeleton) +
                kSkeletonBoneRecordsOffset);
            if (handBone < 0 || handBone >= boneCount ||
                handBone >= static_cast<LONG>(kMaximumBones) ||
                boneRecords == nullptr)
            {
                return false;
            }
            const std::byte* const handRecord = boneRecords +
                static_cast<std::size_t>(handBone) * kBoneRecordStride;
            if (*reinterpret_cast<const LONG*>(
                    handRecord + kBoneIkHandleIndexOffset) != -1)
            {
                // Preserve vehicle steering and any mod-authored right-hand
                // target, just as the controller injection path does.
                return false;
            }

            Matrix4 nativeRoot = {};
            std::memcpy(&nativeRoot, rootTransform, sizeof(nativeRoot));
            if (!IsFinite(nativeRoot))
            {
                return false;
            }
            Matrix4 forwardOffset = {};
            forwardOffset.values[0][0] = 1.0F;
            forwardOffset.values[1][1] = 1.0F;
            forwardOffset.values[2][2] = 1.0F;
            forwardOffset.values[3][2] = kFirstPersonArmRootForwardOffset;
            forwardOffset.values[3][3] = 1.0F;
            adjustedRoot = Multiply(forwardOffset, nativeRoot);
            return IsFinite(adjustedRoot);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            adjustedRoot = {};
            return false;
        }
    }

    bool TryInject(void* skeleton, ArmIkRestore& restore) noexcept
    {
        restore = {};
        void* const soldier = bfvr::ReadCurrentBFSoldierVrCameraSoldier();
        if (skeleton == nullptr || soldier == nullptr)
        {
            return false;
        }

        // Skeleton::transform is global. Reject every non-local Skeleton
        // before touching controller, calibration, or owned-handle state;
        // other soldiers/items are interleaved with the local arm transform.
        __try
        {
            const auto* const soldierBytes = static_cast<const std::byte*>(soldier);
            if (*reinterpret_cast<void* const*>(
                    soldierBytes + kSoldierAnimationSkeletonOffset) != skeleton)
            {
                return false;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }

        InterlockedIncrement(&localUpdates_);
        if (!IsLocalPlayerAlive())
        {
            ResetLifetimeBinding();
            ResetObservedNativePose();
            bfvr::ClearWeaponViewOffset();
            InterlockedIncrement(&deadPlayerRejected_);
            return false;
        }
        bfvr::D3D8RuntimeControllerSample sample = {};
        LONG generation = 0;
        if (!bfvr::ReadFreshAcceptedControllerInput(
                sample,
                generation,
                kControllerSampleMaximumAgeMs) ||
            !IsTrackedGrip(sample.hands[kRightControllerHand]) ||
            !IsTrackedAim(sample.hands[kRightControllerHand]))
        {
            ResetLifetimeBinding();
            bfvr::ClearWeaponViewOffset();
            InterlockedIncrement(&trackingRejected_);
            return false;
        }
        const bfvr::stereo::Pose currentGripPose = {
            {
                sample.hands[kRightControllerHand].gripPose.positionX,
                sample.hands[kRightControllerHand].gripPose.positionY,
                sample.hands[kRightControllerHand].gripPose.positionZ},
            {
                sample.hands[kRightControllerHand].gripPose.orientationX,
                sample.hands[kRightControllerHand].gripPose.orientationY,
                    sample.hands[kRightControllerHand].gripPose.orientationZ,
                    sample.hands[kRightControllerHand].gripPose.orientationW}};
        const bfvr::stereo::Pose currentAimPose = {
            {
                sample.hands[kRightControllerHand].aimPose.positionX,
                sample.hands[kRightControllerHand].aimPose.positionY,
                sample.hands[kRightControllerHand].aimPose.positionZ},
            {
                sample.hands[kRightControllerHand].aimPose.orientationX,
                sample.hands[kRightControllerHand].aimPose.orientationY,
                sample.hands[kRightControllerHand].aimPose.orientationZ,
                sample.hands[kRightControllerHand].aimPose.orientationW}};
        // OpenXR grip is the physical hold point; aim is the runtime-defined
        // pointing direction. Keep those roles explicit instead of capturing
        // an arbitrary stock-hand angle at spawn.
        const bfvr::stereo::Pose controllerWeaponPose = {
            currentGripPose.position,
            currentAimPose.orientation};
        const auto grip =
            bfvr::stereo::MakeD3D8AbsoluteGripWeaponDelta(
                IdentityMatrix(),
                currentGripPose,
                kBf1942WorldUnitsPerMeter);
        if (!grip.has_value())
        {
            ResetLifetimeBinding();
            bfvr::ClearWeaponViewOffset();
            InterlockedIncrement(&trackingRejected_);
            return false;
        }

        __try
        {
            const auto* const soldierBytes = static_cast<const std::byte*>(soldier);
            if (soldierBytes[kSoldierFirstPersonStateOffset] == std::byte{0})
            {
                ResetLifetimeBinding();
                ResetObservedNativePose();
                bfvr::ClearWeaponViewOffset();
                return false;
            }
            void* const expectedSkeleton = *reinterpret_cast<void* const*>(
                soldierBytes + kSoldierAnimationSkeletonOffset);
            const void* const soldierTemplate = *reinterpret_cast<void* const*>(
                soldierBytes + kSoldierTemplateOffset);
            if (expectedSkeleton != skeleton || soldierTemplate == nullptr)
            {
                return false;
            }
            const LONG handBone = *reinterpret_cast<const LONG*>(
                static_cast<const std::byte*>(soldierTemplate) +
                kTemplateRightHandBoneOffset);
            const LONG boneCount = *reinterpret_cast<const LONG*>(
                static_cast<const std::byte*>(skeleton) + kSkeletonBoneCountOffset);
            std::byte* const boneRecords = *reinterpret_cast<std::byte* const*>(
                static_cast<const std::byte*>(skeleton) + kSkeletonBoneRecordsOffset);
            if (handBone < 0 || handBone >= boneCount ||
                handBone >= static_cast<LONG>(kMaximumBones) || boneRecords == nullptr)
            {
                ResetLifetimeBinding();
                InterlockedIncrement(&matrixRejected_);
                return false;
            }
            std::byte* const boneRecord = boneRecords +
                static_cast<std::size_t>(handBone) * kBoneRecordStride;
            const LONG priorHandleIndex = *reinterpret_cast<const LONG*>(
                boneRecord + kBoneIkHandleIndexOffset);
            if (priorHandleIndex != -1)
            {
                // Vehicle steering and any mod-authored target already own this
                // hand. Do not overwrite an engine-authored target to make an
                // infantry-only controller feature appear more general than it is.
                ResetLifetimeBinding();
                ResetObservedNativePose();
                ResetOwnedHandle();
                bfvr::ClearWeaponViewOffset();
                InterlockedIncrement(&nativeTargetPreserved_);
                return false;
            }

            if (!hasObservedNativePose_ || observedSoldier_ != soldier ||
                observedSkeleton_ != skeleton || observedHandBone_ != handBone)
            {
                ResetLifetimeBinding();
                return false;
            }
            const Matrix4 nativeHand = observedNativeHand_;
            const Matrix4 nativeTarget = nativeHand;
            const auto soldierTransform = ReadSoldierTransform(soldier);
            if (!soldierTransform.has_value() || !IsFinite(nativeHand) ||
                !IsFinite(nativeTarget))
            {
                ResetLifetimeBinding();
                InterlockedIncrement(&matrixRejected_);
                return false;
            }

            if (!hasCalibration_ || calibratedSoldier_ != soldier ||
                calibratedSkeleton_ != skeleton || calibratedHandBone_ != handBone)
            {
                calibratedSoldier_ = soldier;
                calibratedSkeleton_ = skeleton;
                calibratedHandBone_ = handBone;
                calibrationGripPosition_ = {
                    grip->values[3][0],
                    grip->values[3][1],
                    grip->values[3][2]};
                // Apply the measured tracking-to-Skeleton origin translation
                // directly. Controller movement remains 1:1 and no spawn-time
                // hand/controller difference becomes a new arbitrary anchor.
                calibrationTargetPosition_ = {
                    grip->values[3][0] +
                        kTrackingToSkeletonPositionOffset[0],
                    grip->values[3][1] +
                        kTrackingToSkeletonPositionOffset[1],
                    grip->values[3][2] +
                        kTrackingToSkeletonPositionOffset[2]};
                lastMotionProbeTargetPosition_ = calibrationTargetPosition_;
                hasLastMotionProbeTargetPosition_ = false;
                hasCalibration_ = true;
                InterlockedIncrement(&calibrationCommits_);
                WriteLog(
                    L"Native 1P arm IK bound the current game-selected right hand: soldier=%p skeleton=%p bone=%ld controllerGeneration=%ld orientationPolicy=direct tracked OpenXR aim nativeHand=(%.4f,%.4f,%.4f) rawGrip=(%.4f,%.4f,%.4f) automaticTarget=(%.4f,%.4f,%.4f) aimQuaternion=(%.5f,%.5f,%.5f,%.5f) measuredOriginOffset=(%.4f,%.4f,%.4f).",
                    soldier,
                    skeleton,
                    handBone,
                    generation,
                    nativeTarget.values[3][0],
                    nativeTarget.values[3][1],
                    nativeTarget.values[3][2],
                    calibrationGripPosition_[0],
                    calibrationGripPosition_[1],
                    calibrationGripPosition_[2],
                    calibrationTargetPosition_[0],
                    calibrationTargetPosition_[1],
                    calibrationTargetPosition_[2],
                    currentAimPose.orientation.x,
                    currentAimPose.orientation.y,
                    currentAimPose.orientation.z,
                    currentAimPose.orientation.w,
                    kTrackingToSkeletonPositionOffset[0],
                    kTrackingToSkeletonPositionOffset[1],
                        kTrackingToSkeletonPositionOffset[2]);
            }

            const auto controllerTarget =
                bfvr::stereo::MakeD3D8AbsoluteGripWeaponDelta(
                    IdentityMatrix(),
                    controllerWeaponPose,
                    kBf1942WorldUnitsPerMeter);
            if (!controllerTarget.has_value())
            {
                ResetLifetimeBinding();
                InterlockedIncrement(&matrixRejected_);
                return false;
            }
            Matrix4 controllerGunLocal = *controllerTarget;
            controllerGunLocal.values[3][0] = grip->values[3][0] +
                kTrackingToSkeletonPositionOffset[0];
            controllerGunLocal.values[3][1] = grip->values[3][1] +
                kTrackingToSkeletonPositionOffset[1];
            controllerGunLocal.values[3][2] = grip->values[3][2] +
                kTrackingToSkeletonPositionOffset[2];
            controllerGunLocal.values[3][3] = 1.0F;

            const auto poseCameraTranslation =
                ReadPoseCameraTranslation(soldier);
            if (poseCameraTranslation.has_value())
            {
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    controllerGunLocal.values[3][axis] +=
                        poseCameraTranslation->localDelta[axis];
                }
                if (poseCameraTranslation->pose != kStandingPose)
                {
                    InterlockedIncrement(&stanceTranslatedFrames_);
                }
                if (loggedStanceSoldier_ != soldier ||
                    loggedStancePose_ != poseCameraTranslation->pose)
                {
                    loggedStanceSoldier_ = soldier;
                    loggedStancePose_ = poseCameraTranslation->pose;
                    InterlockedIncrement(&stanceTransitions_);
                    WriteLog(
                        L"Native 1P arm inherited BF1942 pose-camera translation: soldier=%p pose=%ld localDelta=(%.4f,%.4f,%.4f). Controller aim orientation and active-item hand-from-fire alignment are unchanged.",
                        soldier,
                        poseCameraTranslation->pose,
                        poseCameraTranslation->localDelta[0],
                        poseCameraTranslation->localDelta[1],
                        poseCameraTranslation->localDelta[2]);
                }
            }
            else
            {
                InterlockedIncrement(&stanceReadFailures_);
            }

            // OpenXR's aim basis describes the gun/barrel, not BF1942's
            // anatomical right-hand bone. The current game-selected item
            // supplies its authored hand-from-fire rotation through the
            // native attachment callback before controller IK is allowed.
            const Matrix4 controllerGunWorld = Multiply(
                controllerGunLocal,
                *soldierTransform);
            Matrix4 nativeHandFromFireRotation = {};
            if (!ReadActiveItemAlignment(
                    soldier,
                    nativeHandFromFireRotation))
            {
                // Alignment warm-up affects only the anatomical hand/visual
                // item. Keep publishing direct OpenXR gun aim so an immediate
                // first shot or weapon-switch shot is still pointer-directed.
                const Matrix4 nativeTargetWorld = Multiply(
                    nativeTarget,
                    *soldierTransform);
                const Matrix4 identity = IdentityMatrix();
                bfvr::PublishNativeArmWeaponVisualPose(
                    identity,
                    identity,
                    nativeTargetWorld,
                    nativeTargetWorld,
                    controllerGunWorld,
                    soldier,
                    generation);
                return false;
            }
            const auto inverseSoldierTransform = Invert(*soldierTransform);
            if (!inverseSoldierTransform.has_value())
            {
                ResetLifetimeBinding();
                InterlockedIncrement(&matrixRejected_);
                return false;
            }
            const auto correctedHandWorld =
                bfvr::stereo::MakeD3D8ControllerDirectedNativeHandMatrix(
                controllerGunWorld,
                nativeHandFromFireRotation);
            if (!correctedHandWorld.has_value())
            {
                ResetLifetimeBinding();
                InterlockedIncrement(&matrixRejected_);
                return false;
            }
            Matrix4 target = Multiply(
                *correctedHandWorld,
                *inverseSoldierTransform);
            // The selected-item relation is rotation-only. Keep the exact
            // live grip location even under a translated soldier transform.
            target.values[3][0] = controllerGunLocal.values[3][0];
            target.values[3][1] = controllerGunLocal.values[3][1];
            target.values[3][2] = controllerGunLocal.values[3][2];
            target.values[3][3] = 1.0F;
            const Matrix4 nativeTargetWorld = Multiply(nativeTarget, *soldierTransform);
            const Matrix4 targetWorld = Multiply(target, *soldierTransform);
            const auto inverseNativeTargetWorld = Invert(nativeTargetWorld);
            if (!IsFinite(controllerGunWorld) || !IsFinite(target) ||
                !inverseNativeTargetWorld.has_value())
            {
                ResetLifetimeBinding();
                InterlockedIncrement(&matrixRejected_);
                return false;
            }
            const Matrix4 worldAttachment = Multiply(
                *inverseNativeTargetWorld,
                targetWorld);
            if (!IsFinite(worldAttachment))
            {
                InterlockedIncrement(&matrixRejected_);
                return false;
            }

            const std::byte* const skeletonBytes = static_cast<const std::byte*>(skeleton);
            const std::byte* const handleBegin = *reinterpret_cast<std::byte* const*>(
                skeletonBytes + kSkeletonIkHandleBeginOffset);
            const std::byte* const handleEnd = *reinterpret_cast<std::byte* const*>(
                skeletonBytes + kSkeletonIkHandleEndOffset);
            const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(handleBegin);
            const std::uintptr_t end = reinterpret_cast<std::uintptr_t>(handleEnd);
            if ((handleBegin != nullptr && handleEnd == nullptr) || end < begin ||
                end - begin > kMaximumIkHandles * kIkHandleStride ||
                (end - begin) % kIkHandleStride != 0)
            {
                InterlockedIncrement(&matrixRejected_);
                return false;
            }

            const std::size_t handleCount = static_cast<std::size_t>(
                (end - begin) / kIkHandleStride);
            if (ownedHandleSkeleton_ == skeleton &&
                ownedHandleBone_ == handBone && ownedHandleIndex_ >= 0)
            {
                const std::size_t ownedIndex = static_cast<std::size_t>(ownedHandleIndex_);
                if (handleBegin != nullptr && ownedIndex < handleCount &&
                    *reinterpret_cast<const LONG*>(
                        handleBegin + ownedIndex * kIkHandleStride + 0x4C) == handBone)
                {
                    // applyIk will now overwrite this known BFVR-owned record
                    // instead of allocating one transient record per transform.
                    *reinterpret_cast<LONG*>(
                        boneRecord + kBoneIkHandleIndexOffset) = ownedHandleIndex_;
                }
                else
                {
                    ResetOwnedHandle();
                }
            }

            const std::array<float, 3> targetPosition = {
                target.values[3][0], target.values[3][1], target.values[3][2]};
            auto applyIk = reinterpret_cast<ApplyIkFn>(applyIkTarget_);
            if (applyIk == nullptr)
            {
                InterlockedIncrement(&applyFailures_);
                return false;
            }
            applyIk(skeleton, handBone, targetPosition.data(), &target);
            const LONG activeHandleIndex = *reinterpret_cast<const LONG*>(
                boneRecord + kBoneIkHandleIndexOffset);
            if (activeHandleIndex < 0)
            {
                InterlockedIncrement(&applyFailures_);
                return false;
            }
            ownedHandleSkeleton_ = skeleton;
            ownedHandleBone_ = handBone;
            ownedHandleIndex_ = activeHandleIndex;
            restore.boneRecord = boneRecord;
            restore.previousHandleIndex = priorHandleIndex;
            restore.targetPosition = targetPosition;
            restore.targetDelta = {
                targetPosition[0] - calibrationTargetPosition_[0],
                targetPosition[1] - calibrationTargetPosition_[1],
                targetPosition[2] - calibrationTargetPosition_[2]};
            restore.gripDelta = {
                grip->values[3][0] - calibrationGripPosition_[0],
                grip->values[3][1] - calibrationGripPosition_[1],
                grip->values[3][2] - calibrationGripPosition_[2]};
            restore.controllerGeneration = generation;
            const std::array<float, 3> zero = {};
            const bool significantTargetMotion =
                DistanceSquared(restore.targetDelta, zero) >=
                kMotionProbeMinimumDelta * kMotionProbeMinimumDelta;
            const bool sufficientlyDifferentProbe =
                !hasLastMotionProbeTargetPosition_ ||
                DistanceSquared(
                    targetPosition,
                    lastMotionProbeTargetPosition_) >=
                kMotionProbeRepeatDelta * kMotionProbeRepeatDelta;
            if (significantTargetMotion && sufficientlyDifferentProbe &&
                InterlockedCompareExchange(
                    &motionProbeReports_, 0, 0) < kMaximumMotionProbeReports)
            {
                lastMotionProbeTargetPosition_ = targetPosition;
                hasLastMotionProbeTargetPosition_ = true;
                restore.captureMotionProbe = true;
            }
            restore.active = true;
            bfvr::PublishNativeArmWeaponVisualPose(
                worldAttachment,
                worldAttachment,
                nativeTargetWorld,
                targetWorld,
                controllerGunWorld,
                soldier,
                generation);
            InterlockedIncrement(&injectedFrames_);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ResetLifetimeBinding();
            bfvr::ClearWeaponViewOffset();
            InterlockedIncrement(&applyFailures_);
            return false;
        }
    }

    std::optional<Matrix4> ReadSoldierTransform(void* soldier) noexcept
    {
        return ReadObjectTransform(soldier);
    }

    std::optional<PoseCameraTranslation> ReadPoseCameraTranslation(
        void* soldier) noexcept
    {
        if (soldier == nullptr || getSoldierPoseTarget_ == nullptr ||
            getPoseCameraPositionTarget_ == nullptr)
        {
            return std::nullopt;
        }
        __try
        {
            const auto getPose =
                reinterpret_cast<GetSoldierPoseFn>(getSoldierPoseTarget_);
            const auto getPoseCameraPosition =
                reinterpret_cast<GetPoseCameraPositionFn>(
                    getPoseCameraPositionTarget_);
            const LONG pose = getPose(soldier);
            if (pose < kStandingPose || pose > kLastSupportedPose)
            {
                return std::nullopt;
            }
            const float* const standing =
                getPoseCameraPosition(soldier, kStandingPose);
            const float* const current =
                getPoseCameraPosition(soldier, pose);
            if (standing == nullptr || current == nullptr)
            {
                return std::nullopt;
            }
            PoseCameraTranslation result = {};
            result.pose = pose;
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                result.localDelta[axis] = current[axis] - standing[axis];
                if (!IsFinite(result.localDelta[axis]) ||
                    std::fabs(result.localDelta[axis]) >
                        kMaximumPoseCameraTranslation)
                {
                    return std::nullopt;
                }
            }
            return result;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return std::nullopt;
        }
    }

    bool SafeCopyMatrix(
        const Matrix4* source,
        Matrix4& destination) const noexcept
    {
        destination = {};
        if (source == nullptr)
        {
            return false;
        }
        __try
        {
            std::memcpy(&destination, source, sizeof(destination));
            return IsFinite(destination);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            destination = {};
            return false;
        }
    }

    std::optional<Matrix4> ReadObjectTransform(void* object) noexcept
    {
        if (object == nullptr)
        {
            return std::nullopt;
        }
        __try
        {
            void* const vtable = *reinterpret_cast<void* const*>(object);
            void* const target = vtable == nullptr
                ? nullptr
                : *reinterpret_cast<void* const*>(
                    static_cast<const std::byte*>(vtable) + 0x3C);
            const auto getter = reinterpret_cast<GetTransformationFn>(target);
            const Matrix4* const matrix =
                getter == nullptr ? nullptr : getter(object);
            if (matrix == nullptr)
            {
                return std::nullopt;
            }
            Matrix4 copy = {};
            std::memcpy(&copy, matrix, sizeof(copy));
            return IsFinite(copy) ? std::optional<Matrix4>(copy) : std::nullopt;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return std::nullopt;
        }
    }

    bool ReadActiveItemAlignment(
        const void* soldier,
        Matrix4& handFromFire) noexcept
    {
        handFromFire = {};
        AcquireSRWLockShared(&activeItemAlignmentLock_);
        const bool valid =
            activeItemAlignmentValid_ &&
            activeItemSoldier_ == soldier &&
            activeItem_ != nullptr &&
            activeItemInterface_ != nullptr;
        if (valid)
        {
            handFromFire = activeItemHandFromFire_;
        }
        ReleaseSRWLockShared(&activeItemAlignmentLock_);
        return valid && IsFinite(handFromFire);
    }

    void ResetActiveItemAlignment() noexcept
    {
        AcquireSRWLockExclusive(&activeItemAlignmentLock_);
        activeItemSoldier_ = nullptr;
        activeItemInterface_ = nullptr;
        activeItem_ = nullptr;
        activeItemHandFromFire_ = {};
        activeItemNativeWarmupCallbacks_ = 0;
        activeItemAlignmentValid_ = false;
        ReleaseSRWLockExclusive(&activeItemAlignmentLock_);
    }

    void Restore(const ArmIkRestore& restore) noexcept
    {
        if (!restore.active || restore.boneRecord == nullptr)
        {
            return;
        }
        __try
        {
            *reinterpret_cast<LONG*>(
                restore.boneRecord + kBoneIkHandleIndexOffset) =
                restore.previousHandleIndex;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            InterlockedIncrement(&restoreFailures_);
        }
    }

    void CaptureInjectedMotionProbe(const ArmIkRestore& restore) noexcept
    {
        if (!restore.active || !restore.captureMotionProbe ||
            restore.boneRecord == nullptr ||
            InterlockedIncrement(&motionProbeReports_) > kMaximumMotionProbeReports)
        {
            return;
        }
        __try
        {
            std::array<float, 3> solvedPosition = {};
            std::memcpy(
                solvedPosition.data(),
                restore.boneRecord + kBoneFinalTranslationOffset,
                sizeof(solvedPosition));
            if (!IsFinite(solvedPosition[0]) || !IsFinite(solvedPosition[1]) ||
                !IsFinite(solvedPosition[2]))
            {
                return;
            }
            const std::array<float, 3> solvedDelta = {
                solvedPosition[0] - calibrationTargetPosition_[0],
                solvedPosition[1] - calibrationTargetPosition_[1],
                solvedPosition[2] - calibrationTargetPosition_[2]};
            const std::array<float, 3> targetError = {
                solvedPosition[0] - restore.targetPosition[0],
                solvedPosition[1] - restore.targetPosition[1],
                solvedPosition[2] - restore.targetPosition[2]};
            WriteLog(
                L"Native 1P arm IK motion probe generation=%ld gripDelta=(%.4f,%.4f,%.4f) targetDelta=(%.4f,%.4f,%.4f) solvedDelta=(%.4f,%.4f,%.4f) targetError=(%.4f,%.4f,%.4f).",
                restore.controllerGeneration,
                restore.gripDelta[0],
                restore.gripDelta[1],
                restore.gripDelta[2],
                restore.targetDelta[0],
                restore.targetDelta[1],
                restore.targetDelta[2],
                solvedDelta[0],
                solvedDelta[1],
                solvedDelta[2],
                targetError[0],
                targetError[1],
                targetError[2]);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            InterlockedIncrement(&applyFailures_);
        }
    }

    void CaptureNativePose(void* skeleton) noexcept
    {
        void* const soldier = bfvr::ReadCurrentBFSoldierVrCameraSoldier();
        if (skeleton == nullptr || soldier == nullptr)
        {
            return;
        }
        __try
        {
            const auto* const soldierBytes = static_cast<const std::byte*>(soldier);
            if (soldierBytes[kSoldierFirstPersonStateOffset] == std::byte{0})
            {
                return;
            }
            void* const expectedSkeleton = *reinterpret_cast<void* const*>(
                soldierBytes + kSoldierAnimationSkeletonOffset);
            const void* const soldierTemplate = *reinterpret_cast<void* const*>(
                soldierBytes + kSoldierTemplateOffset);
            if (expectedSkeleton != skeleton || soldierTemplate == nullptr)
            {
                return;
            }
            const LONG handBone = *reinterpret_cast<const LONG*>(
                static_cast<const std::byte*>(soldierTemplate) +
                kTemplateRightHandBoneOffset);
            const LONG boneCount = *reinterpret_cast<const LONG*>(
                static_cast<const std::byte*>(skeleton) + kSkeletonBoneCountOffset);
            std::byte* const boneRecords = *reinterpret_cast<std::byte* const*>(
                static_cast<const std::byte*>(skeleton) + kSkeletonBoneRecordsOffset);
            if (handBone < 0 || handBone >= boneCount ||
                handBone >= static_cast<LONG>(kMaximumBones) || boneRecords == nullptr)
            {
                return;
            }
            std::byte* const boneRecord = boneRecords +
                static_cast<std::size_t>(handBone) * kBoneRecordStride;
            if (*reinterpret_cast<const LONG*>(
                    boneRecord + kBoneIkHandleIndexOffset) != -1)
            {
                // A native vehicle/mod target owns this hand. Its solved pose
                // must never become BFVR's controller calibration baseline.
                return;
            }
            Matrix4 nativeHand = {};
            std::memcpy(
                &nativeHand,
                boneRecord + kBoneFinalMatrixOffset,
                sizeof(nativeHand));
            if (!IsFinite(nativeHand))
            {
                return;
            }
            observedNativeHand_ = nativeHand;
            observedSoldier_ = soldier;
            observedSkeleton_ = skeleton;
            observedHandBone_ = handBone;
            hasObservedNativePose_ = true;
            InterlockedIncrement(&nativePoseCaptures_);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            // Preserve the original Skeleton path; this only delays a later
            // controller target until a safe native pose is observed.
        }
    }

    bool IsLocalPlayerAlive() const noexcept
    {
        if (gameImage_ == nullptr)
        {
            return false;
        }
        __try
        {
            void* const manager = *reinterpret_cast<void* const*>(
                gameImage_ + kPlayerManagerGlobalRva);
            void* const localPlayer = manager == nullptr
                ? nullptr
                : *reinterpret_cast<void* const*>(
                    static_cast<const std::byte*>(manager) +
                    kPlayerManagerLocalPlayerOffset);
            return localPlayer != nullptr &&
                std::to_integer<BYTE>(
                    static_cast<const std::byte*>(localPlayer)
                        [kBFPlayerIsAliveOffset]) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void ResetLifetimeBinding() noexcept
    {
        hasCalibration_ = false;
        calibratedSoldier_ = nullptr;
        calibratedSkeleton_ = nullptr;
        calibratedHandBone_ = -1;
        calibrationGripPosition_ = {};
        calibrationTargetPosition_ = {};
        lastMotionProbeTargetPosition_ = {};
        hasLastMotionProbeTargetPosition_ = false;
    }

    void ResetObservedNativePose() noexcept
    {
        observedNativeHand_ = {};
        observedSoldier_ = nullptr;
        observedSkeleton_ = nullptr;
        observedHandBone_ = -1;
        hasObservedNativePose_ = false;
    }

    void ResetOwnedHandle() noexcept
    {
        ownedHandleSkeleton_ = nullptr;
        ownedHandleBone_ = -1;
        ownedHandleIndex_ = -1;
    }

    void RemoveHooks() noexcept
    {
        if (skeletonHookCreated_ && skeletonTransformTarget_ != nullptr)
        {
            MH_RemoveHook(skeletonTransformTarget_);
            skeletonHookCreated_ = false;
        }
        if (attachmentHookCreated_ &&
            setRelativeBoneTransformTarget_ != nullptr)
        {
            MH_RemoveHook(setRelativeBoneTransformTarget_);
            attachmentHookCreated_ = false;
        }
    }

    void Reset() noexcept
    {
        if (active_ == this)
        {
            active_ = nullptr;
        }
        enabled_ = false;
        skeletonHookEnabled_ = false;
        attachmentHookEnabled_ = false;
        skeletonHookCreated_ = false;
        attachmentHookCreated_ = false;
        gameImage_ = nullptr;
        skeletonTransformTarget_ = nullptr;
        applyIkTarget_ = nullptr;
        setRelativeBoneTransformTarget_ = nullptr;
        getSoldierPoseTarget_ = nullptr;
        getPoseCameraPositionTarget_ = nullptr;
        originalSkeletonTransform_ = nullptr;
        originalSetRelativeBoneTransform_ = nullptr;
        ResetLifetimeBinding();
        ResetObservedNativePose();
        ResetOwnedHandle();
        ResetActiveItemAlignment();
        loggedStanceSoldier_ = nullptr;
        loggedStancePose_ = -1;
        InterlockedExchange(&started_, 0);
    }

    void WriteLog(const wchar_t* format, ...) const noexcept
    {
        if (appendLog_ == nullptr)
        {
            return;
        }
        std::array<wchar_t, 900> message = {};
        va_list arguments;
        va_start(arguments, format);
        _vsnwprintf_s(message.data(), message.size(), _TRUNCATE, format, arguments);
        va_end(arguments);
        appendLog_(message.data());
    }

    static NativeArmIk* active_;
    static thread_local PendingLocalActiveItemAttachment
        pendingLocalActiveItemAttachment_;
    std::byte* gameImage_ = nullptr;
    void* skeletonTransformTarget_ = nullptr;
    void* applyIkTarget_ = nullptr;
    void* setRelativeBoneTransformTarget_ = nullptr;
    void* getSoldierPoseTarget_ = nullptr;
    void* getPoseCameraPositionTarget_ = nullptr;
    SkeletonTransformFn originalSkeletonTransform_ = nullptr;
    SetRelativeBoneTransformFn originalSetRelativeBoneTransform_ = nullptr;
    void (*appendLog_)(const wchar_t* message) = nullptr;
    std::array<float, 3> calibrationGripPosition_ = {};
    std::array<float, 3> calibrationTargetPosition_ = {};
    std::array<float, 3> lastMotionProbeTargetPosition_ = {};
    void* calibratedSoldier_ = nullptr;
    void* calibratedSkeleton_ = nullptr;
    LONG calibratedHandBone_ = -1;
    volatile LONG started_ = 0;
    volatile LONG callbackEntrants_ = 0;
    volatile LONG localUpdates_ = 0;
    volatile LONG rootShiftedFrames_ = 0;
    volatile LONG injectedFrames_ = 0;
    volatile LONG calibrationCommits_ = 0;
    volatile LONG nativePoseCaptures_ = 0;
    volatile LONG trackingRejected_ = 0;
    volatile LONG deadPlayerRejected_ = 0;
    volatile LONG nativeTargetPreserved_ = 0;
    volatile LONG matrixRejected_ = 0;
    volatile LONG applyFailures_ = 0;
    volatile LONG restoreFailures_ = 0;
    volatile LONG motionProbeReports_ = 0;
    volatile LONG activeItemChanges_ = 0;
    volatile LONG activeItemAlignments_ = 0;
    volatile LONG activeItemAlignmentFailures_ = 0;
    volatile LONG stanceTranslatedFrames_ = 0;
    volatile LONG stanceTransitions_ = 0;
    volatile LONG stanceReadFailures_ = 0;
    volatile LONG loggedActiveItemChanges_ = 0;
    bool skeletonHookCreated_ = false;
    bool attachmentHookCreated_ = false;
    bool skeletonHookEnabled_ = false;
    bool attachmentHookEnabled_ = false;
    bool enabled_ = false;
    bool hasCalibration_ = false;
    SRWLOCK activeItemAlignmentLock_ = SRWLOCK_INIT;
    Matrix4 activeItemHandFromFire_ = {};
    const void* activeItemSoldier_ = nullptr;
    const void* activeItemInterface_ = nullptr;
    const void* activeItem_ = nullptr;
    LONG activeItemNativeWarmupCallbacks_ = 0;
    bool activeItemAlignmentValid_ = false;
    void* loggedStanceSoldier_ = nullptr;
    LONG loggedStancePose_ = -1;
    Matrix4 observedNativeHand_ = {};
    void* observedSoldier_ = nullptr;
    void* observedSkeleton_ = nullptr;
    LONG observedHandBone_ = -1;
    bool hasObservedNativePose_ = false;
    bool hasLastMotionProbeTargetPosition_ = false;
    void* ownedHandleSkeleton_ = nullptr;
    LONG ownedHandleBone_ = -1;
    LONG ownedHandleIndex_ = -1;
};

NativeArmIk* NativeArmIk::active_ = nullptr;
thread_local PendingLocalActiveItemAttachment
    NativeArmIk::pendingLocalActiveItemAttachment_ = {};
NativeArmIk g_nativeArmIk = {};

} // namespace

namespace bfvr
{

bool StartBFSoldierNativeArmIk(
    void* gameImage,
    void (*appendLog)(const wchar_t* message))
{
    return g_nativeArmIk.Start(gameImage, appendLog);
}

void StopBFSoldierNativeArmIk()
{
    g_nativeArmIk.Stop();
}

} // namespace bfvr
