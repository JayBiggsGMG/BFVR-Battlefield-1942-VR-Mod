#pragma once

#include <cstdint>
#include <span>

namespace bfvr
{
enum class OpenXRControllerAction : std::uint32_t
{
    AimPose,
    GripPose,
    Trigger,
    Squeeze,
    MovementTurnAxis,
    AxisClick,
    PrimaryFace,
    SecondaryFace,
    Menu,
    Haptic
};

enum class OpenXRControllerHand : std::uint32_t
{
    Left,
    Right
};

struct OpenXRControllerBindingSeed
{
    OpenXRControllerAction action = OpenXRControllerAction::AimPose;
    OpenXRControllerHand hand = OpenXRControllerHand::Left;
    const char* componentPath = nullptr;
};

struct OpenXRControllerProfileBindings
{
    const char* interactionProfile = nullptr;
    std::span<const OpenXRControllerBindingSeed> bindings = {};
};

[[nodiscard]] std::span<const OpenXRControllerProfileBindings>
OpenXRControllerBindingProfiles() noexcept;
} // namespace bfvr
