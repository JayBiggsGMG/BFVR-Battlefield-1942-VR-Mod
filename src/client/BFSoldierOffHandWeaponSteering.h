#pragma once

#include "client/BFSoldierOffHandSupportBinding.h"
#include "client/D3D8SharedPresentationBridge.h"

#include <array>
#include <cstdint>
#include <optional>

namespace bfvr
{

[[nodiscard]] std::uint64_t MakeBFSoldierOffHandBindingId(
    const void* soldier,
    const void* activeItem) noexcept;

struct BFSoldierOffHandWeaponSteeringInput
{
    std::uint64_t bindingId = 0;
    bool sessionFocused = false;
    bool leftGripTracked = false;
    bool leftSqueezeActive = false;
    bool nativeLeftHandTargetActive = false;
    BFSoldierOffHandSupportMode mode =
        BFSoldierOffHandSupportMode::Disabled;
    D3D8RuntimeControllerHand leftHand = {};
    stereo::Matrix4 soldierWorld = {};
    stereo::Matrix4 controllerGunWorld = {};
    stereo::Matrix4 controllerRightHandWorld = {};
    stereo::Matrix4 leftHandFromRightHand = {};
    std::array<float, 3> trackingOriginOffset = {};
    std::array<float, 3> stanceTranslation = {};
    float maximumSwingRadians = 0.0F;
    float worldUnitsPerMetre = 1.0F;
};

// Builds the tracked left-hand world point and asks the support binding for a
// fixed-pivot swing. The binding permanently rejects CapturedClose mode.
[[nodiscard]] std::optional<stereo::OffHandWeaponSteeringResult>
TryComputeBFSoldierOffHandWeaponSteering(
    BFSoldierOffHandSupportBinding& binding,
    const BFSoldierOffHandWeaponSteeringInput& input) noexcept;

} // namespace bfvr
