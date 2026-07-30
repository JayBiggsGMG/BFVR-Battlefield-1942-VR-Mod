#pragma once

#include "stereo/StereoMath.h"

#include <windows.h>

namespace bfvr
{

// Maintains the reversible per-item relation between OpenXR's left grip basis
// and BF1942's native anatomical wrist basis. It owns no game object and
// performs no Skeleton writes.
class BFSoldierLeftGripRotationBinding final
{
public:
    [[nodiscard]] bool Update(
        void* soldier,
        void* skeleton,
        const void* activeItem,
        LONG leftHandBone,
        const stereo::Matrix4& leftGrip,
        const stereo::Matrix4& nativeLeftHandLocal,
        stereo::Matrix4& target,
        bool& createdBinding) noexcept;
    void Reset() noexcept;

private:
    SRWLOCK lock_ = SRWLOCK_INIT;
    stereo::Matrix4 handFromGripRotation_ = {};
    void* soldier_ = nullptr;
    void* skeleton_ = nullptr;
    const void* activeItem_ = nullptr;
    LONG leftHandBone_ = -1;
    bool valid_ = false;
};

} // namespace bfvr
