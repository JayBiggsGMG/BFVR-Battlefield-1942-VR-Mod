#pragma once

#include "stereo/StereoMath.h"

#include <windows.h>

namespace bfvr
{

// Preserves the first complete authored primary left-from-right relation for
// one soldier/item lifetime. BF1942's later equip transition is diagnostic
// input only and cannot steer the gun/fire basis away from this support socket.
class BFSoldierPrimarySupportPoseCache final
{
public:
    void Resolve(
        void* soldier,
        void* skeleton,
        const void* activeItem,
        LONG activeItemIndex,
        stereo::Matrix4& leftHandFromRightHand,
        void (*appendLog)(const wchar_t* message)) noexcept;
    void Reset() noexcept;

private:
    SRWLOCK lock_ = SRWLOCK_INIT;
    stereo::Matrix4 relation_ = {};
    void* soldier_ = nullptr;
    void* skeleton_ = nullptr;
    const void* activeItem_ = nullptr;
    bool valid_ = false;
};

} // namespace bfvr
