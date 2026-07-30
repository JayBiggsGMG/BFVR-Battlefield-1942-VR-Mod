#include "client/BFSoldierNativeArmPole.h"

#include "stereo/ArmPoleVectorMath.h"

#include <MinHook.h>

#include <windows.h>

#include <array>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace
{

constexpr std::ptrdiff_t kMayaApplyIk2BoneSolverRva = 0x0026B300;
constexpr std::size_t kSkeletonIkHandleBeginOffset = 0xC;
constexpr std::size_t kSkeletonIkHandleEndOffset = 0x10;
constexpr std::size_t kIkHandleStride = 0x50;
constexpr std::size_t kIkHandleBoneOffset = 0x4C;
constexpr std::size_t kBoneFinalTranslationOffset = 0x78;
constexpr std::size_t kMaximumIkHandles = 512;
constexpr std::int32_t kPrimaryItemIndex = 3;
constexpr float kNativePoleEpsilon = 1.0e-5F;
constexpr float kEndpointErrorTolerance = 1.0e-3F;
constexpr long kMaximumEndpointReports = 12;

constexpr BYTE kMayaApplyIk2BoneSolverPrefix[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x81, 0xEC,
    0xE4, 0x01, 0x00, 0x00, 0xD9, 0x01, 0x8B, 0x45,
    0x10, 0xDD, 0x9C, 0x24, 0xCC, 0x00, 0x00, 0x00};

struct ArmPoleFrameContext
{
    bfvr::BFSoldierNativeArmPole* owner = nullptr;
    const float* rightTarget = nullptr;
    const float* leftTarget = nullptr;
    std::int32_t activeItemIndex = -1;
    std::array<float, 3> rightPole = {};
    std::array<float, 3> leftPole = {};
    bool rightApplied = false;
    bool leftApplied = false;
};

thread_local ArmPoleFrameContext g_frame = {};

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

bool IsNativePoleZero(const float* pole) noexcept
{
    return pole != nullptr &&
        std::isfinite(pole[0]) &&
        std::isfinite(pole[1]) &&
        std::isfinite(pole[2]) &&
        std::fabs(pole[0]) <= kNativePoleEpsilon &&
        std::fabs(pole[1]) <= kNativePoleEpsilon &&
        std::fabs(pole[2]) <= kNativePoleEpsilon;
}

const float* ResolveHandleTarget(
    void* skeleton,
    const std::int32_t handBone,
    const std::int32_t handleIndex) noexcept
{
    if (skeleton == nullptr || handBone < 0 || handleIndex < 0)
    {
        return nullptr;
    }
    __try
    {
        const auto* const bytes = static_cast<const std::byte*>(skeleton);
        const std::byte* const begin =
            *reinterpret_cast<std::byte* const*>(
                bytes + kSkeletonIkHandleBeginOffset);
        const std::byte* const end =
            *reinterpret_cast<std::byte* const*>(
                bytes + kSkeletonIkHandleEndOffset);
        const std::uintptr_t beginValue =
            reinterpret_cast<std::uintptr_t>(begin);
        const std::uintptr_t endValue =
            reinterpret_cast<std::uintptr_t>(end);
        if (begin == nullptr || end == nullptr || endValue < beginValue ||
            endValue - beginValue >
                kMaximumIkHandles * kIkHandleStride ||
            (endValue - beginValue) % kIkHandleStride != 0)
        {
            return nullptr;
        }
        const std::size_t handleCount =
            static_cast<std::size_t>(
                (endValue - beginValue) / kIkHandleStride);
        const std::size_t index =
            static_cast<std::size_t>(handleIndex);
        if (index >= handleCount)
        {
            return nullptr;
        }
        const std::byte* const record =
            begin + index * kIkHandleStride;
        if (*reinterpret_cast<const std::int32_t*>(
                record + kIkHandleBoneOffset) != handBone)
        {
            return nullptr;
        }
        return reinterpret_cast<const float*>(record);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

} // namespace

namespace bfvr
{

BFSoldierNativeArmPole* BFSoldierNativeArmPole::active_ = nullptr;

bool BFSoldierNativeArmPole::Start(
    void* gameImage,
    void (*appendLog)(const wchar_t* message)) noexcept
{
    appendLog_ = appendLog;
    target_ = gameImage == nullptr
        ? nullptr
        : static_cast<std::byte*>(gameImage) +
            kMayaApplyIk2BoneSolverRva;
    if (!HasExpectedPrefix(
            target_,
            kMayaApplyIk2BoneSolverPrefix,
            sizeof(kMayaApplyIk2BoneSolverPrefix)))
    {
        WriteLog(
            L"Native 1P arm pole rejected maya::applyIK2BoneSolver target %p because its retail prefix differs. Accepted hand IK remains unchanged.",
            target_);
        return false;
    }

    const MH_STATUS createStatus = MH_CreateHook(
        target_,
        reinterpret_cast<LPVOID>(
            &BFSoldierNativeArmPole::MayaApplyIk2BoneSolverHook),
        reinterpret_cast<LPVOID*>(&original_));
    if (createStatus != MH_OK || original_ == nullptr)
    {
        WriteLog(
            L"Native 1P arm pole could not create the Maya solver hook (status=%d). Accepted hand IK remains unchanged.",
            static_cast<int>(createStatus));
        original_ = nullptr;
        target_ = nullptr;
        return false;
    }
    hookCreated_ = true;
    active_ = this;
    const MH_STATUS enableStatus = MH_EnableHook(target_);
    if (enableStatus != MH_OK)
    {
        active_ = nullptr;
        MH_RemoveHook(target_);
        hookCreated_ = false;
        original_ = nullptr;
        target_ = nullptr;
        WriteLog(
            L"Native 1P arm pole could not enable the Maya solver hook (status=%d). Accepted hand IK remains unchanged.",
            static_cast<int>(enableStatus));
        return false;
    }
    hookEnabled_ = true;
    WriteLog(
        L"Native 1P arm pole enabled the exact Maya rotate-plane argument. Only the zero pole of pointer-matched BFVR-owned hand targets may change; shoulder, elbow, wrist, hand target, and output pointers are forwarded unchanged. Right primary slot 3 remains native.");
    return true;
}

void BFSoldierNativeArmPole::Stop() noexcept
{
    EndFrame();
    if (hookEnabled_ && target_ != nullptr)
    {
        MH_DisableHook(target_);
        hookEnabled_ = false;
    }
    while (InterlockedCompareExchange(&callbackEntrants_, 0, 0) != 0)
    {
        Sleep(0);
    }
    if (hookCreated_ && target_ != nullptr)
    {
        MH_RemoveHook(target_);
        hookCreated_ = false;
    }
    if (active_ == this)
    {
        active_ = nullptr;
    }
    if (target_ != nullptr)
    {
        WriteLog(
            L"Native 1P arm pole stopped: right=%ld left=%ld nativePolePreserved=%ld rejected=%ld previousContinuity=%ld fallbackAxis=%ld endpointSamples=%ld endpointViolations=%ld.",
            appliedRight_,
            appliedLeft_,
            preservedNativePole_,
            rejected_,
            previousContinuity_,
            fallbackAxis_,
            endpointSamples_,
            endpointViolations_);
    }
    target_ = nullptr;
    original_ = nullptr;
    priorRightTarget_ = nullptr;
    priorLeftTarget_ = nullptr;
    priorActiveItemIndex_ = -1;
    previousRightPole_[0] = previousRightPole_[1] =
        previousRightPole_[2] = 0.0F;
    previousLeftPole_[0] = previousLeftPole_[1] =
        previousLeftPole_[2] = 0.0F;
    hasPreviousRightPole_ = false;
    hasPreviousLeftPole_ = false;
}

void BFSoldierNativeArmPole::BeginFrame(
    void* skeleton,
    const std::int32_t rightHandBone,
    const std::int32_t rightHandleIndex,
    const std::int32_t leftHandBone,
    const std::int32_t leftHandleIndex,
    const std::int32_t activeItemIndex) noexcept
{
    EndFrame();
    if (!hookEnabled_ || active_ != this)
    {
        return;
    }
    const float* const rightTarget = ResolveHandleTarget(
        skeleton, rightHandBone, rightHandleIndex);
    const float* const leftTarget = ResolveHandleTarget(
        skeleton, leftHandBone, leftHandleIndex);
    if (rightTarget == nullptr && leftTarget == nullptr)
    {
        return;
    }
    if (rightTarget != priorRightTarget_ ||
        activeItemIndex != priorActiveItemIndex_)
    {
        hasPreviousRightPole_ = false;
    }
    if (leftTarget != priorLeftTarget_ ||
        activeItemIndex != priorActiveItemIndex_)
    {
        hasPreviousLeftPole_ = false;
    }
    priorRightTarget_ = rightTarget;
    priorLeftTarget_ = leftTarget;
    priorActiveItemIndex_ = activeItemIndex;
    g_frame = {this, rightTarget, leftTarget, activeItemIndex};
}

void BFSoldierNativeArmPole::CaptureSolvedEndpoints(
    const void* rightBoneRecord,
    const void* leftBoneRecord) noexcept
{
    if (g_frame.owner != this)
    {
        return;
    }
    const void* const boneRecords[] = {
        rightBoneRecord, leftBoneRecord};
    const float* const targets[] = {
        g_frame.rightTarget, g_frame.leftTarget};
    const std::array<float, 3>* const poles[] = {
        &g_frame.rightPole, &g_frame.leftPole};
    const bool applied[] = {
        g_frame.rightApplied, g_frame.leftApplied};
    for (std::size_t arm = 0; arm < 2; ++arm)
    {
        if (!applied[arm] || boneRecords[arm] == nullptr ||
            targets[arm] == nullptr)
        {
            continue;
        }
        __try
        {
            const auto* const solved = reinterpret_cast<const float*>(
                static_cast<const std::byte*>(boneRecords[arm]) +
                kBoneFinalTranslationOffset);
            const float errorX = solved[0] - targets[arm][0];
            const float errorY = solved[1] - targets[arm][1];
            const float errorZ = solved[2] - targets[arm][2];
            const float errorLength = std::sqrt(
                errorX * errorX + errorY * errorY + errorZ * errorZ);
            if (!std::isfinite(errorLength))
            {
                InterlockedIncrement(&endpointViolations_);
                continue;
            }
            const long sample =
                InterlockedIncrement(&endpointSamples_);
            if (errorLength > kEndpointErrorTolerance)
            {
                InterlockedIncrement(&endpointViolations_);
            }
            if (sample <= kMaximumEndpointReports)
            {
                WriteLog(
                    L"Native 1P arm pole endpoint probe arm=%ls pole=(%.4f,%.4f,%.4f) targetError=(%.6f,%.6f,%.6f) length=%.6f.",
                    arm == 0 ? L"right" : L"left",
                    (*poles[arm])[0],
                    (*poles[arm])[1],
                    (*poles[arm])[2],
                    errorX,
                    errorY,
                    errorZ,
                    errorLength);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            InterlockedIncrement(&endpointViolations_);
        }
    }
}

void BFSoldierNativeArmPole::EndFrame() noexcept
{
    if (g_frame.owner == this)
    {
        g_frame = {};
    }
}

void __fastcall BFSoldierNativeArmPole::MayaApplyIk2BoneSolverHook(
    const float* pole,
    const float* shoulder,
    const float* elbow,
    const float* wrist,
    const float* handTarget,
    void* upperRotation,
    void* forearmRotation)
{
    BFSoldierNativeArmPole* const self = active_;
    if (self == nullptr || self->original_ == nullptr)
    {
        return;
    }
    InterlockedIncrement(&self->callbackEntrants_);

    const bool leftArm =
        g_frame.owner == self &&
        handTarget != nullptr &&
        handTarget == g_frame.leftTarget;
    const bool rightArm =
        g_frame.owner == self &&
        handTarget != nullptr &&
        handTarget == g_frame.rightTarget;
    const bool preserveRightPrimary =
        rightArm &&
        g_frame.activeItemIndex == kPrimaryItemIndex;

    const float* effectivePole = pole;
    std::array<float, 3> replacement = {};
    if ((leftArm || rightArm) && !preserveRightPrimary)
    {
        if (!IsNativePoleZero(pole))
        {
            InterlockedIncrement(&self->preservedNativePole_);
        }
        else if (shoulder != nullptr)
        {
            stereo::ArmPoleVectorInput input = {};
            std::memcpy(
                input.shoulder.data(),
                shoulder,
                sizeof(input.shoulder));
            std::memcpy(
                input.handTarget.data(),
                handTarget,
                sizeof(input.handTarget));
            input.leftArm = leftArm;
            input.hasPreviousPole = leftArm
                ? self->hasPreviousLeftPole_
                : self->hasPreviousRightPole_;
            std::memcpy(
                input.previousPole.data(),
                leftArm
                    ? self->previousLeftPole_
                    : self->previousRightPole_,
                sizeof(input.previousPole));
            const auto computed = stereo::ComputeArmPoleVector(input);
            if (computed.has_value())
            {
                replacement = computed->pole;
                effectivePole = replacement.data();
                float* const previous = leftArm
                    ? self->previousLeftPole_
                    : self->previousRightPole_;
                std::memcpy(
                    previous,
                    replacement.data(),
                    sizeof(replacement));
                if (leftArm)
                {
                    self->hasPreviousLeftPole_ = true;
                    g_frame.leftPole = replacement;
                    g_frame.leftApplied = true;
                    InterlockedIncrement(&self->appliedLeft_);
                }
                else
                {
                    self->hasPreviousRightPole_ = true;
                    g_frame.rightPole = replacement;
                    g_frame.rightApplied = true;
                    InterlockedIncrement(&self->appliedRight_);
                }
                if (computed->usedPreviousPole)
                {
                    InterlockedIncrement(&self->previousContinuity_);
                }
                if (computed->usedFallbackAxis)
                {
                    InterlockedIncrement(&self->fallbackAxis_);
                }
            }
            else
            {
                InterlockedIncrement(&self->rejected_);
            }
        }
    }

    self->original_(
        effectivePole,
        shoulder,
        elbow,
        wrist,
        handTarget,
        upperRotation,
        forearmRotation);
    InterlockedDecrement(&self->callbackEntrants_);
}

void BFSoldierNativeArmPole::WriteLog(
    const wchar_t* format,
    ...) const noexcept
{
    if (appendLog_ == nullptr)
    {
        return;
    }
    std::array<wchar_t, 700> message = {};
    va_list arguments;
    va_start(arguments, format);
    _vsnwprintf_s(
        message.data(),
        message.size(),
        _TRUNCATE,
        format,
        arguments);
    va_end(arguments);
    appendLog_(message.data());
}

} // namespace bfvr
