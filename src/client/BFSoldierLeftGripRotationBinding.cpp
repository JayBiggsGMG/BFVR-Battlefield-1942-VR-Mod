#include "client/BFSoldierLeftGripRotationBinding.h"

#include "stereo/WeaponFireAimMath.h"

#include <array>
#include <cwchar>

namespace bfvr
{

bool BFSoldierLeftGripRotationBinding::Update(
    void* soldier,
    void* skeleton,
    const void* activeItem,
    const LONG leftHandBone,
    const stereo::Matrix4& leftGrip,
    const stereo::Matrix4& nativeLeftHandLocal,
    stereo::Matrix4& target,
    bool& createdBinding) noexcept
{
    target = {};
    createdBinding = false;
    if (soldier == nullptr || skeleton == nullptr ||
        activeItem == nullptr || leftHandBone < 0)
    {
        return false;
    }

    stereo::Matrix4 handFromGripRotation = {};
    bool usable = true;
    AcquireSRWLockExclusive(&lock_);
    if (soldier_ != soldier || skeleton_ != skeleton ||
        leftHandBone_ != leftHandBone)
    {
        anatomicalHandFromGripRotation_ = {};
        fallbackHandFromGripRotation_ = {};
        soldier_ = soldier;
        skeleton_ = skeleton;
        fallbackActiveItem_ = nullptr;
        leftHandBone_ = leftHandBone;
        anatomicalValid_ = false;
        fallbackValid_ = false;
    }
    if (anatomicalValid_)
    {
        handFromGripRotation = anatomicalHandFromGripRotation_;
    }
    else if (!fallbackValid_ || fallbackActiveItem_ != activeItem)
    {
        const auto candidate =
            stereo::MakeD3D8NativeHandFromFireRotation(
                leftGrip,
                nativeLeftHandLocal);
        if (!candidate.has_value())
        {
            usable = false;
        }
        else
        {
            fallbackHandFromGripRotation_ = *candidate;
            fallbackActiveItem_ = activeItem;
            fallbackValid_ = true;
            createdBinding = true;
        }
    }
    if (usable && !anatomicalValid_ && fallbackValid_)
    {
        handFromGripRotation = fallbackHandFromGripRotation_;
    }
    ReleaseSRWLockExclusive(&lock_);
    if (!usable)
    {
        return false;
    }

    const auto directed =
        stereo::MakeD3D8ControllerDirectedNativeHandMatrix(
            leftGrip,
            handFromGripRotation);
    if (!directed.has_value())
    {
        return false;
    }
    target = *directed;
    return true;
}

void BFSoldierLeftGripRotationBinding::CaptureAnatomicalReference(
    void* soldier,
    void* skeleton,
    const void* activeItem,
    const LONG leftHandBone,
    const stereo::Matrix4& leftGrip,
    const stereo::Matrix4& authoredLeftHandTarget,
    void (*appendLog)(const wchar_t* message)) noexcept
{
    if (soldier == nullptr || skeleton == nullptr || activeItem == nullptr ||
        leftHandBone < 0)
    {
        return;
    }
    const auto candidate = stereo::MakeD3D8NativeHandFromFireRotation(
        leftGrip,
        authoredLeftHandTarget);
    if (!candidate.has_value())
    {
        return;
    }

    bool created = false;
    AcquireSRWLockExclusive(&lock_);
    if (soldier_ == soldier && skeleton_ == skeleton &&
        leftHandBone_ == leftHandBone && !anatomicalValid_)
    {
        anatomicalHandFromGripRotation_ = *candidate;
        anatomicalValid_ = true;
        created = true;
    }
    ReleaseSRWLockExclusive(&lock_);
    if (created && appendLog != nullptr)
    {
        std::array<wchar_t, 640> message = {};
        _snwprintf_s(
            message.data(),
            message.size(),
            _TRUNCATE,
            L"Native 1P left hand established one anatomical controller-grip-"
            L"to-wrist reference from successful authored primary support: "
            L"soldier=%p skeleton=%p item=%p bone=%ld. Later item switches no "
            L"longer rebase the wrist to each item's native zero pose.",
            soldier,
            skeleton,
            activeItem,
            leftHandBone);
        appendLog(message.data());
    }
}

void BFSoldierLeftGripRotationBinding::ResetTransient() noexcept
{
    AcquireSRWLockExclusive(&lock_);
    fallbackHandFromGripRotation_ = {};
    fallbackActiveItem_ = nullptr;
    fallbackValid_ = false;
    ReleaseSRWLockExclusive(&lock_);
}

void BFSoldierLeftGripRotationBinding::Reset() noexcept
{
    AcquireSRWLockExclusive(&lock_);
    anatomicalHandFromGripRotation_ = {};
    fallbackHandFromGripRotation_ = {};
    soldier_ = nullptr;
    skeleton_ = nullptr;
    fallbackActiveItem_ = nullptr;
    leftHandBone_ = -1;
    anatomicalValid_ = false;
    fallbackValid_ = false;
    ReleaseSRWLockExclusive(&lock_);
}

} // namespace bfvr
