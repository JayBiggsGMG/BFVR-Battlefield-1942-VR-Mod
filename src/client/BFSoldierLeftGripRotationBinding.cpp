#include "client/BFSoldierLeftGripRotationBinding.h"

#include "stereo/WeaponFireAimMath.h"

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
    if (!valid_ || soldier_ != soldier || skeleton_ != skeleton ||
        activeItem_ != activeItem || leftHandBone_ != leftHandBone)
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
            handFromGripRotation_ = *candidate;
            soldier_ = soldier;
            skeleton_ = skeleton;
            activeItem_ = activeItem;
            leftHandBone_ = leftHandBone;
            valid_ = true;
            createdBinding = true;
        }
    }
    if (usable && valid_)
    {
        handFromGripRotation = handFromGripRotation_;
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

void BFSoldierLeftGripRotationBinding::Reset() noexcept
{
    AcquireSRWLockExclusive(&lock_);
    handFromGripRotation_ = {};
    soldier_ = nullptr;
    skeleton_ = nullptr;
    activeItem_ = nullptr;
    leftHandBone_ = -1;
    valid_ = false;
    ReleaseSRWLockExclusive(&lock_);
}

} // namespace bfvr
