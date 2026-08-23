#include "client/BFSoldierVrArmFoundation.h"

#include "client/BFSoldierBoneResolver.h"
#include "client/BFSoldierNativeArmMath.h"

#include <windows.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{

constexpr std::size_t kSkeletonBoneRecordsOffset = 0x0;
constexpr std::size_t kSkeletonBoneCountOffset = 0x4;
constexpr std::size_t kBoneRecordStride = 0xE8;
constexpr std::size_t kBoneParentPointerOffset = 0x4;
constexpr std::size_t kBoneLocalMatrixOffset = 0x8;
constexpr std::size_t kBoneFinalMatrixOffset = 0x48;
constexpr std::size_t kMatrixTranslationOffset = 0x30;
constexpr std::int32_t kMaximumBones = 256;
constexpr std::size_t kSoldierTemplateOffset = 0x4C;
constexpr std::size_t kSoldierFirstPersonStateOffset = 0x290;
constexpr std::size_t kSoldierAnimationSkeletonOffset = 0x298;
constexpr std::size_t kTemplateRightHandBoneOffset = 0x33C;
constexpr std::size_t kBoneIkHandleIndexOffset = 0xE0;
constexpr float kMinimumSegmentLength = 0.05F;
constexpr float kMaximumSegmentLength = 0.60F;
constexpr float kReachTolerance = 0.08F;
constexpr char kRightHandName[] = "Bip01 R Hand";
constexpr char kRightForearmName[] = "Bip01 R Forearm";
constexpr char kRightUpperArmName[] = "Bip01 R UpperArm";
constexpr char kLeftHandName[] = "Bip01 L Hand";
constexpr char kLeftForearmName[] = "Bip01 L Forearm";
constexpr char kLeftUpperArmName[] = "Bip01 L UpperArm";

using Matrix4 = bfvr::stereo::Matrix4;

bool IsFinite(const std::array<float, 3>& value) noexcept
{
    return std::isfinite(value[0]) &&
        std::isfinite(value[1]) &&
        std::isfinite(value[2]);
}

std::array<float, 3> ReadTranslation(const std::byte* record) noexcept
{
    std::array<float, 3> result = {};
    std::memcpy(
        result.data(),
        record + kBoneFinalMatrixOffset + kMatrixTranslationOffset,
        sizeof(result));
    return result;
}

float Distance(
    const std::array<float, 3>& left,
    const std::array<float, 3>& right) noexcept
{
    const float x = left[0] - right[0];
    const float y = left[1] - right[1];
    const float z = left[2] - right[2];
    return std::sqrt(x * x + y * y + z * z);
}

bool ValidSegment(float length) noexcept
{
    return std::isfinite(length) &&
        length >= kMinimumSegmentLength &&
        length <= kMaximumSegmentLength;
}

bool PrepareArm(
    std::byte* boneRecords,
    std::int32_t boneCount,
    std::int32_t handBone,
    std::int32_t upperArmBone,
    const std::array<float, 3>& handTarget,
    const std::array<float, 3>& shoulderTarget,
    void*& restoredRecord,
    std::array<float, 3>& restoredTranslation) noexcept
{
    if (boneRecords == nullptr || handBone < 2 || handBone >= boneCount ||
        upperArmBone != handBone - 2 || !IsFinite(handTarget) ||
        !IsFinite(shoulderTarget))
    {
        return false;
    }

    std::byte* const upper = boneRecords +
        static_cast<std::size_t>(upperArmBone) * kBoneRecordStride;
    std::byte* const forearm = upper + kBoneRecordStride;
    std::byte* const hand = forearm + kBoneRecordStride;
    std::byte* const parent = *reinterpret_cast<std::byte**>(
        upper + kBoneParentPointerOffset);
    const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(boneRecords);
    const std::uintptr_t end = begin +
        static_cast<std::size_t>(boneCount) * kBoneRecordStride;
    const std::uintptr_t parentValue = reinterpret_cast<std::uintptr_t>(parent);
    if (parent == nullptr || parentValue < begin || parentValue >= end ||
        (parentValue - begin) % kBoneRecordStride != 0)
    {
        return false;
    }

    const std::array<float, 3> nativeShoulder = ReadTranslation(upper);
    const std::array<float, 3> nativeElbow = ReadTranslation(forearm);
    const std::array<float, 3> nativeWrist = ReadTranslation(hand);
    const float upperLength = Distance(nativeShoulder, nativeElbow);
    const float forearmLength = Distance(nativeElbow, nativeWrist);
    const float desiredReach = Distance(shoulderTarget, handTarget);
    if (!ValidSegment(upperLength) || !ValidSegment(forearmLength) ||
        !std::isfinite(desiredReach) || desiredReach < 0.03F ||
        desiredReach > upperLength + forearmLength + kReachTolerance)
    {
        return false;
    }

    Matrix4 parentFinal = {};
    std::memcpy(
        &parentFinal,
        parent + kBoneFinalMatrixOffset,
        sizeof(parentFinal));
    const auto inverseParent = bfvr::native_arm_math::Invert(parentFinal);
    if (!inverseParent.has_value())
    {
        return false;
    }
    Matrix4 desiredFinal = bfvr::native_arm_math::IdentityMatrix();
    desiredFinal.values[3][0] = shoulderTarget[0];
    desiredFinal.values[3][1] = shoulderTarget[1];
    desiredFinal.values[3][2] = shoulderTarget[2];
    const Matrix4 desiredLocal = bfvr::native_arm_math::Multiply(
        desiredFinal, *inverseParent);
    const std::array<float, 3> local = {
        desiredLocal.values[3][0],
        desiredLocal.values[3][1],
        desiredLocal.values[3][2]};
    if (!IsFinite(local) || Distance(local, {}) > 3.0F)
    {
        return false;
    }

    float* const localTranslation = reinterpret_cast<float*>(
        upper + kBoneLocalMatrixOffset + kMatrixTranslationOffset);
    std::memcpy(
        restoredTranslation.data(), localTranslation,
        sizeof(restoredTranslation));
    std::memcpy(localTranslation, local.data(), sizeof(local));
    restoredRecord = upper;
    return true;
}

} // namespace

namespace bfvr
{

bool TryMakeForwardShiftedBFSoldierVrArmRoot(
    void* skeleton,
    const stereo::Matrix4* rootTransform,
    void* soldier,
    const bool localPlayerAlive,
    const float forwardOffset,
    stereo::Matrix4& adjustedRoot) noexcept
{
    adjustedRoot = {};
    if (skeleton == nullptr || rootTransform == nullptr || soldier == nullptr ||
        !localPlayerAlive || !std::isfinite(forwardOffset))
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
        const std::int32_t handBone = *reinterpret_cast<const std::int32_t*>(
            static_cast<const std::byte*>(soldierTemplate) +
            kTemplateRightHandBoneOffset);
        const std::int32_t boneCount = *reinterpret_cast<const std::int32_t*>(
            static_cast<const std::byte*>(skeleton) +
            kSkeletonBoneCountOffset);
        std::byte* const boneRecords = *reinterpret_cast<std::byte* const*>(
            static_cast<const std::byte*>(skeleton) +
            kSkeletonBoneRecordsOffset);
        if (handBone < 0 || handBone >= boneCount ||
            handBone >= kMaximumBones || boneRecords == nullptr)
        {
            return false;
        }
        const std::byte* const handRecord = boneRecords +
            static_cast<std::size_t>(handBone) * kBoneRecordStride;
        if (*reinterpret_cast<const std::int32_t*>(
                handRecord + kBoneIkHandleIndexOffset) != -1)
        {
            return false;
        }

        stereo::Matrix4 nativeRoot = {};
        std::memcpy(&nativeRoot, rootTransform, sizeof(nativeRoot));
        if (!native_arm_math::IsFinite(nativeRoot))
        {
            return false;
        }
        stereo::Matrix4 offset = native_arm_math::IdentityMatrix();
        offset.values[3][2] = forwardOffset;
        adjustedRoot = native_arm_math::Multiply(offset, nativeRoot);
        return native_arm_math::IsFinite(adjustedRoot);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        adjustedRoot = {};
        return false;
    }
}

bool BFSoldierVrArmFoundation::ResolveChains(
    void* skeleton,
    const BFSoldierBoneResolver& resolver,
    const std::int32_t rightHandBone,
    const std::int32_t leftHandBone) noexcept
{
    if (cachedSkeleton_ == skeleton &&
        cachedRightHand_ == rightHandBone &&
        cachedLeftHand_ == leftHandBone)
    {
        return true;
    }
    Reset();
    if (skeleton == nullptr || !resolver.IsReady())
    {
        return false;
    }

    const auto namedRightHand = resolver.ResolveBoneIndex(
        skeleton, kRightHandName);
    const auto rightForearm = resolver.ResolveBoneIndex(
        skeleton, kRightForearmName);
    const auto rightUpperArm = resolver.ResolveBoneIndex(
        skeleton, kRightUpperArmName);
    if (!namedRightHand.has_value() || !rightForearm.has_value() ||
        !rightUpperArm.has_value() || *namedRightHand != rightHandBone ||
        *rightForearm != rightHandBone - 1 ||
        *rightUpperArm != rightHandBone - 2)
    {
        return false;
    }

    cachedSkeleton_ = skeleton;
    cachedRightHand_ = rightHandBone;
    cachedRightUpperArm_ = *rightUpperArm;
    cachedLeftHand_ = leftHandBone;
    if (leftHandBone >= 0)
    {
        const auto namedLeftHand = resolver.ResolveBoneIndex(
            skeleton, kLeftHandName);
        const auto leftForearm = resolver.ResolveBoneIndex(
            skeleton, kLeftForearmName);
        const auto leftUpperArm = resolver.ResolveBoneIndex(
            skeleton, kLeftUpperArmName);
        if (!namedLeftHand.has_value() || !leftForearm.has_value() ||
            !leftUpperArm.has_value() || *namedLeftHand != leftHandBone ||
            *leftForearm != leftHandBone - 1 ||
            *leftUpperArm != leftHandBone - 2)
        {
            cachedLeftHand_ = -1;
            cachedLeftUpperArm_ = -1;
        }
        else
        {
            cachedLeftUpperArm_ = *leftUpperArm;
        }
    }
    return true;
}

bool BFSoldierVrArmFoundation::PrepareAfterNativeTransform(
    const BFSoldierVrArmFoundationInput& input,
    const BFSoldierBoneResolver& resolver,
    BFSoldierVrArmFoundationRestore& restore) noexcept
{
    restore = {};
    if (input.skeleton == nullptr || input.controllerGeneration <= 0 ||
        !input.rightActive ||
        !ResolveChains(input.skeleton, resolver, input.rightHandBone,
            input.leftHandBone))
    {
        return false;
    }

    __try
    {
        auto* const skeletonBytes = static_cast<std::byte*>(input.skeleton);
        std::byte* const boneRecords = *reinterpret_cast<std::byte**>(
            skeletonBytes + kSkeletonBoneRecordsOffset);
        const std::int32_t boneCount = *reinterpret_cast<std::int32_t*>(
            skeletonBytes + kSkeletonBoneCountOffset);
        if (boneRecords == nullptr || boneCount <= 0 ||
            boneCount > kMaximumBones)
        {
            return false;
        }

        restore.rightApplied = PrepareArm(
            boneRecords, boneCount, input.rightHandBone,
            cachedRightUpperArm_, input.rightHandTarget,
            input.shoulderAnchors.right, restore.upperArmRecords[0],
            restore.localTranslations[0]);
        if (input.leftActive && cachedLeftHand_ == input.leftHandBone)
        {
            restore.leftApplied = PrepareArm(
                boneRecords, boneCount, input.leftHandBone,
                cachedLeftUpperArm_, input.leftHandTarget,
                input.shoulderAnchors.left, restore.upperArmRecords[1],
                restore.localTranslations[1]);
        }
        return restore.rightApplied || restore.leftApplied;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Restore(restore);
        return false;
    }
}

void BFSoldierVrArmFoundation::Restore(
    BFSoldierVrArmFoundationRestore& restore) noexcept
{
    __try
    {
        for (std::size_t arm = 0; arm < restore.upperArmRecords.size(); ++arm)
        {
            if (restore.upperArmRecords[arm] == nullptr)
            {
                continue;
            }
            auto* const record = static_cast<std::byte*>(
                restore.upperArmRecords[arm]);
            std::memcpy(
                record + kBoneLocalMatrixOffset + kMatrixTranslationOffset,
                restore.localTranslations[arm].data(),
                sizeof(restore.localTranslations[arm]));
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    restore = {};
}

void BFSoldierVrArmFoundation::Reset() noexcept
{
    cachedSkeleton_ = nullptr;
    cachedRightHand_ = -1;
    cachedRightUpperArm_ = -1;
    cachedLeftHand_ = -1;
    cachedLeftUpperArm_ = -1;
}

} // namespace bfvr
