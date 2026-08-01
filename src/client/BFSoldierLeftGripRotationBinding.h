#pragma once

#include "stereo/StereoMath.h"

#include <windows.h>

namespace bfvr
{

// Maintains one learned primary-grip anatomical wrist relation plus the prior
// per-item native fallback until that relation exists. It owns no game object
// and performs no Skeleton writes.
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
    void CaptureAnatomicalReference(
        void* soldier,
        void* skeleton,
        const void* activeItem,
        LONG leftHandBone,
        const stereo::Matrix4& leftGrip,
        const stereo::Matrix4& authoredLeftHandTarget,
        void (*appendLog)(const wchar_t* message)) noexcept;
    void ResetTransient() noexcept;
    void Reset() noexcept;

private:
    SRWLOCK lock_ = SRWLOCK_INIT;
    stereo::Matrix4 anatomicalHandFromGripRotation_ = {};
    stereo::Matrix4 fallbackHandFromGripRotation_ = {};
    void* soldier_ = nullptr;
    void* skeleton_ = nullptr;
    const void* fallbackActiveItem_ = nullptr;
    LONG leftHandBone_ = -1;
    bool anatomicalValid_ = false;
    bool fallbackValid_ = false;
};

} // namespace bfvr
