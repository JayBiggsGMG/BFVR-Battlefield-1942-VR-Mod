#include "client/BFSoldierRightGripRotationBinding.h"

#include "stereo/WeaponFireAimMath.h"

#include <array>
#include <cwchar>

namespace
{

constexpr LONG kPrimaryItemIndex = 3;

void LogBinding(
    void (*appendLog)(const wchar_t* message),
    const wchar_t* description,
    void* soldier,
    const void* activeItem,
    const LONG activeItemIndex) noexcept
{
    if (appendLog == nullptr)
    {
        return;
    }
    std::array<wchar_t, 768> message = {};
    _snwprintf_s(
        message.data(),
        message.size(),
        _TRUNCATE,
        L"Native 1P arm %ls: soldier=%p item=%p activeItemIndex=%ld. "
        L"The primary weapon supplies one automatic controller-grip-to-wrist "
        L"reference; no per-item user calibration is used.",
        description,
        soldier,
        activeItem,
        activeItemIndex);
    appendLog(message.data());
}

void CopyPosition(
    const bfvr::stereo::Matrix4& source,
    bfvr::stereo::Matrix4& destination) noexcept
{
    destination.values[3][0] = source.values[3][0];
    destination.values[3][1] = source.values[3][1];
    destination.values[3][2] = source.values[3][2];
    destination.values[3][3] = 1.0F;
}

} // namespace

namespace bfvr
{

std::optional<BFSoldierRightHandPose>
BFSoldierRightGripRotationBinding::Update(
    void* soldier,
    void* skeleton,
    const void* activeItem,
    const LONG handBone,
    const LONG activeItemIndex,
    const stereo::Matrix4& controllerGripWorld,
    const stereo::Matrix4& directControllerFunctionalWorld,
    const stereo::Matrix4& handFromFunctionalTransform,
    void (*appendLog)(const wchar_t* message)) noexcept
{
    if (soldier == nullptr || skeleton == nullptr || activeItem == nullptr ||
        handBone < 0)
    {
        return std::nullopt;
    }
    const auto directHand =
        stereo::MakeD3D8ControllerDirectedNativeHandMatrix(
            directControllerFunctionalWorld,
            handFromFunctionalTransform);
    if (!directHand.has_value())
    {
        return std::nullopt;
    }

    stereo::Matrix4 handFromGripRotation = {};
    bool createdBinding = false;
    bool useAnatomicalBinding = false;
    bool logAnatomicalItem = false;
    AcquireSRWLockExclusive(&lock_);
    if (soldier_ != soldier || skeleton_ != skeleton || handBone_ != handBone)
    {
        handFromGripRotation_ = {};
        soldier_ = soldier;
        skeleton_ = skeleton;
        loggedAnatomicalItem_ = nullptr;
        handBone_ = handBone;
        valid_ = false;
    }
    if (!valid_ && activeItemIndex == kPrimaryItemIndex)
    {
        const auto candidate = stereo::MakeD3D8NativeHandFromFireRotation(
            controllerGripWorld,
            *directHand);
        if (candidate.has_value())
        {
            handFromGripRotation_ = *candidate;
            valid_ = true;
            createdBinding = true;
        }
    }
    if (valid_ && activeItemIndex != kPrimaryItemIndex)
    {
        handFromGripRotation = handFromGripRotation_;
        useAnatomicalBinding = true;
        logAnatomicalItem = loggedAnatomicalItem_ != activeItem;
        loggedAnatomicalItem_ = activeItem;
    }
    ReleaseSRWLockExclusive(&lock_);

    BFSoldierRightHandPose result = {
        *directHand,
        directControllerFunctionalWorld,
        false,
        createdBinding};
    if (useAnatomicalBinding)
    {
        const auto anatomicalHand =
            stereo::MakeD3D8ControllerDirectedNativeHandMatrix(
                controllerGripWorld,
                handFromGripRotation);
        if (!anatomicalHand.has_value())
        {
            return std::nullopt;
        }
        result.handWorld = *anatomicalHand;
        CopyPosition(directControllerFunctionalWorld, result.handWorld);
        const auto functional =
            stereo::MakeD3D8FunctionalFromNativeHandTransform(
                result.handWorld,
                handFromFunctionalTransform);
        if (!functional.has_value())
        {
            return std::nullopt;
        }
        result.functionalWorld = *functional;
        result.anatomicalGripBindingUsed = true;
    }
    if (createdBinding)
    {
        LogBinding(
            appendLog,
            L"established its anatomical right-wrist reference from slot 3",
            soldier,
            activeItem,
            activeItemIndex);
    }
    if (logAnatomicalItem)
    {
        LogBinding(
            appendLog,
            L"applied anatomical grip ownership and reconstructed the selected "
            L"item's authored functional basis",
            soldier,
            activeItem,
            activeItemIndex);
    }
    return result;
}

void BFSoldierRightGripRotationBinding::Reset() noexcept
{
    AcquireSRWLockExclusive(&lock_);
    handFromGripRotation_ = {};
    soldier_ = nullptr;
    skeleton_ = nullptr;
    loggedAnatomicalItem_ = nullptr;
    handBone_ = -1;
    valid_ = false;
    ReleaseSRWLockExclusive(&lock_);
}

} // namespace bfvr
