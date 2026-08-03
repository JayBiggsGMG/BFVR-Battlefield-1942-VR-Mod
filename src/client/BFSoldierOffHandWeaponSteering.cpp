#include "client/BFSoldierOffHandWeaponSteering.h"

#include "client/BFSoldierTrackedHandPose.h"
#include "client/ScopedOffHandSupportPoseCache.h"

namespace bfvr
{

namespace
{

stereo::Matrix4 Multiply(
    const stereo::Matrix4& left,
    const stereo::Matrix4& right) noexcept
{
    stereo::Matrix4 result = {};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            for (std::size_t inner = 0; inner < 4; ++inner)
            {
                result.values[row][column] +=
                    left.values[row][inner] *
                    right.values[inner][column];
            }
        }
    }
    return result;
}

} // namespace

std::uint64_t MakeBFSoldierOffHandBindingId(
    const void* soldier,
    const void* activeItem) noexcept
{
    const std::uint64_t result =
        (static_cast<std::uint64_t>(
             reinterpret_cast<std::uintptr_t>(soldier)) << 32U) |
        static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(activeItem));
    return result == 0 ? 1 : result;
}

std::optional<stereo::OffHandWeaponSteeringResult>
TryComputeBFSoldierOffHandWeaponSteering(
    BFSoldierOffHandSupportBinding& binding,
    const BFSoldierOffHandWeaponSteeringInput& input) noexcept
{
    if (input.mode !=
        BFSoldierOffHandSupportMode::AuthoredHandSpan)
    {
        return std::nullopt;
    }
    const auto trackedLeft =
        MakeBFSoldierTrackedHandPose(
            input.leftHand,
            input.soldierWorld,
            input.trackingOriginOffset,
            input.stanceTranslation,
            input.worldUnitsPerMetre);
    if (!trackedLeft.has_value())
    {
        return std::nullopt;
    }

    BFSoldierOffHandSteeringInput steeringInput = {};
    steeringInput.bindingId = input.bindingId;
    steeringInput.squeezeValue = input.leftHand.squeezeValue;
    steeringInput.sessionFocused = input.sessionFocused;
    steeringInput.leftGripTracked = input.leftGripTracked;
    steeringInput.leftSqueezeActive = input.leftSqueezeActive;
    steeringInput.nativeLeftHandTargetActive =
        input.nativeLeftHandTargetActive;
    steeringInput.mode = input.mode;
    steeringInput.controllerGunWorld = input.controllerGunWorld;
    steeringInput.predictedSupportWorld = Multiply(
        input.leftHandFromRightHand,
        input.controllerRightHandWorld);
    steeringInput.trackedLeftHandWorld = trackedLeft->world;
    steeringInput.maximumSwingRadians =
        input.maximumSwingRadians;
    steeringInput.worldUnitsPerMetre =
        input.worldUnitsPerMetre;
    stereo::OffHandWeaponSteeringResult output = {};
    const bool supported = binding.TryComputeSupportedWeaponSteering(
        steeringInput,
        output);
    PublishScopedOffHandSupportPose(
        input.bindingId,
        input.controllerGunWorld,
        steeringInput.predictedSupportWorld,
        supported);
    return supported
        ? std::optional<stereo::OffHandWeaponSteeringResult>(output)
        : std::nullopt;
}

} // namespace bfvr
