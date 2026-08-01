#pragma once

#include "stereo/StereoMath.h"

#include <optional>
#include <windows.h>

namespace bfvr
{

struct BFSoldierRightHandPose
{
    stereo::Matrix4 handWorld = {};
    stereo::Matrix4 functionalWorld = {};
    bool anatomicalGripBindingUsed = false;
    bool createdAnatomicalGripBinding = false;
};

// Learns one controller-grip-to-anatomical-wrist rotation from the primary
// weapon path that already looks correct. Non-primary items then inherit that
// wrist rotation while their authored hand-to-functional relation reconstructs
// the item's own fire, placement, viewing, or interaction basis.
class BFSoldierRightGripRotationBinding final
{
public:
    [[nodiscard]] std::optional<BFSoldierRightHandPose> Update(
        void* soldier,
        void* skeleton,
        const void* activeItem,
        LONG handBone,
        LONG activeItemIndex,
        const stereo::Matrix4& controllerGripWorld,
        const stereo::Matrix4& directControllerFunctionalWorld,
        const stereo::Matrix4& handFromFunctionalTransform,
        void (*appendLog)(const wchar_t* message)) noexcept;
    void Reset() noexcept;

private:
    SRWLOCK lock_ = SRWLOCK_INIT;
    stereo::Matrix4 handFromGripRotation_ = {};
    void* soldier_ = nullptr;
    void* skeleton_ = nullptr;
    const void* loggedAnatomicalItem_ = nullptr;
    LONG handBone_ = -1;
    bool valid_ = false;
};

} // namespace bfvr
