#pragma once

#include "stereo/StereoMath.h"

#include <windows.h>

#include <array>

namespace bfvr
{

struct BFSoldierOffHandCalibrationInput
{
    void* soldier = nullptr;
    void* skeleton = nullptr;
    const void* activeItem = nullptr;
    LONG activeItemIndex = -1;
    bool leftStickDown = false;
    bool leftGripTracked = false;
    bool leftSqueezeHeld = false;
    bool supportHeld = false;
    stereo::Matrix4 freeLeftHandLocal = {};
    stereo::Matrix4 controllerRightHandWorld = {};
    stereo::Matrix4 inverseSoldierWorld = {};
    stereo::Matrix4 nativeLeftHandFromRightHand = {};
};

// Development socket recorder plus the runtime gateway for separately defined
// exact-template accepted overrides. A left-stick press while the primary hand
// is free stores the resolved visual wrist relative to the solved right wrist;
// that experimental value remains scoped to the exact live item lifetime.
class BFSoldierOffHandCalibration final
{
public:
    void ConfigureFromEnvironment(
        void (*appendLog)(const wchar_t* message)) noexcept;
    void ConfigureForTesting(
        bool enabled,
        const wchar_t* auditPath = nullptr) noexcept;

    void UpdateCapture(
        const BFSoldierOffHandCalibrationInput& input) noexcept;

    [[nodiscard]] stereo::Matrix4 Resolve(
        void* soldier,
        void* skeleton,
        const void* activeItem,
        LONG activeItemIndex,
        const stereo::Matrix4& nativeLeftHandFromRightHand) noexcept;

    void Reset() noexcept;

private:
    SRWLOCK lock_ = SRWLOCK_INIT;
    void (*appendLog_)(const wchar_t* message) = nullptr;
    std::array<wchar_t, MAX_PATH> auditPath_ = {};
    void* soldier_ = nullptr;
    void* skeleton_ = nullptr;
    const void* activeItem_ = nullptr;
    const void* lastProbedPersistentItem_ = nullptr;
    const void* lastLoggedPersistentItem_ = nullptr;
    stereo::Matrix4 calibratedLeftHandFromRightHand_ = {};
    LONG captureSequence_ = 0;
    bool enabled_ = false;
    bool leftStickWasDown_ = false;
    bool calibrationValid_ = false;
};

} // namespace bfvr
