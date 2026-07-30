#pragma once

#include "stereo/OffHandSupportPolicy.h"
#include "stereo/OffHandWeaponSteeringMath.h"

#include <windows.h>

#include <cstdint>

namespace bfvr
{

enum class BFSoldierOffHandSupportMode
{
    Disabled,
    AuthoredHandSpan,
    CapturedClose,
};

struct BFSoldierOffHandSupportInput
{
    std::uint64_t bindingId = 0;
    double timeSeconds = 0.0;
    float squeezeValue = 0.0F;
    bool sessionFocused = false;
    bool leftGripTracked = false;
    bool leftSqueezeActive = false;
    bool nativeLeftHandTargetActive = false;
    BFSoldierOffHandSupportMode mode =
        BFSoldierOffHandSupportMode::Disabled;
    stereo::Matrix4 leftHandFromRightHand = {};
    stereo::Matrix4 controllerRightHandWorld = {};
    stereo::Matrix4 inverseSoldierWorld = {};
    stereo::Matrix4 controllerLeftHandLocal = {};
};

struct BFSoldierOffHandSupportOutput
{
    stereo::OffHandSupportState state =
        stereo::OffHandSupportState::Free;
    stereo::Matrix4 targetLocal = {};
    float controllerDistanceMetres = 0.0F;
    bool supported = false;
    bool enteredSupport = false;
    bool exitedSupport = false;
};

struct BFSoldierOffHandSteeringInput
{
    std::uint64_t bindingId = 0;
    float squeezeValue = 0.0F;
    bool sessionFocused = false;
    bool leftGripTracked = false;
    bool leftSqueezeActive = false;
    bool nativeLeftHandTargetActive = false;
    BFSoldierOffHandSupportMode mode =
        BFSoldierOffHandSupportMode::Disabled;
    stereo::Matrix4 controllerGunWorld = {};
    stereo::Matrix4 predictedSupportWorld = {};
    stereo::Matrix4 trackedLeftHandWorld = {};
    float maximumSwingRadians = 0.0F;
    float worldUnitsPerMetre = 1.0F;
};

// Owns squeeze hysteresis and pure support state. It never reads game memory
// or writes a Skeleton. Only an already-supported AuthoredHandSpan may request
// bounded weapon steering; CapturedClose is permanently visual-only.
class BFSoldierOffHandSupportBinding final
{
public:
    [[nodiscard]] BFSoldierOffHandSupportOutput Update(
        const BFSoldierOffHandSupportInput& input) noexcept;
    [[nodiscard]] bool TryComputeSupportedWeaponSteering(
        const BFSoldierOffHandSteeringInput& input,
        stereo::OffHandWeaponSteeringResult& output) noexcept;
    void Reset() noexcept;

private:
    SRWLOCK lock_ = SRWLOCK_INIT;
    stereo::OffHandSupportPolicy policy_{
        stereo::OffHandSupportConfiguration{}};
    stereo::Matrix4 closeLeftHandFromRightHandLocal_ = {};
    std::uint64_t bindingId_ = 0;
    BFSoldierOffHandSupportMode mode_ =
        BFSoldierOffHandSupportMode::Disabled;
    bool closeRelationValid_ = false;
    bool squeezeHeld_ = false;
};

} // namespace bfvr
