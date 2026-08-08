#include "openxr/OpenXRControllerBindingPolicy.h"

#include <array>

namespace
{
using Action = bfvr::OpenXRControllerAction;
using Binding = bfvr::OpenXRControllerBindingSeed;
using Hand = bfvr::OpenXRControllerHand;
using Profile = bfvr::OpenXRControllerProfileBindings;

constexpr std::array kTouchBindings = {
    Binding{Action::AimPose, Hand::Left, "/input/aim/pose"},
    Binding{Action::GripPose, Hand::Left, "/input/grip/pose"},
    Binding{Action::Trigger, Hand::Left, "/input/trigger/value"},
    Binding{Action::Squeeze, Hand::Left, "/input/squeeze/value"},
    Binding{Action::MovementTurnAxis, Hand::Left, "/input/thumbstick"},
    Binding{Action::AxisClick, Hand::Left, "/input/thumbstick/click"},
    Binding{Action::PrimaryFace, Hand::Left, "/input/x/click"},
    Binding{Action::SecondaryFace, Hand::Left, "/input/y/click"},
    Binding{Action::Menu, Hand::Left, "/input/menu/click"},
    Binding{Action::Haptic, Hand::Left, "/output/haptic"},
    Binding{Action::AimPose, Hand::Right, "/input/aim/pose"},
    Binding{Action::GripPose, Hand::Right, "/input/grip/pose"},
    Binding{Action::Trigger, Hand::Right, "/input/trigger/value"},
    Binding{Action::Squeeze, Hand::Right, "/input/squeeze/value"},
    Binding{Action::MovementTurnAxis, Hand::Right, "/input/thumbstick"},
    Binding{Action::AxisClick, Hand::Right, "/input/thumbstick/click"},
    Binding{Action::PrimaryFace, Hand::Right, "/input/a/click"},
    Binding{Action::SecondaryFace, Hand::Right, "/input/b/click"},
    Binding{Action::Haptic, Hand::Right, "/output/haptic"},
};

constexpr std::array kIndexBindings = {
    Binding{Action::AimPose, Hand::Left, "/input/aim/pose"},
    Binding{Action::GripPose, Hand::Left, "/input/grip/pose"},
    Binding{Action::Trigger, Hand::Left, "/input/trigger/value"},
    Binding{Action::Squeeze, Hand::Left, "/input/squeeze/value"},
    Binding{Action::MovementTurnAxis, Hand::Left, "/input/thumbstick"},
    Binding{Action::AxisClick, Hand::Left, "/input/thumbstick/click"},
    Binding{Action::PrimaryFace, Hand::Left, "/input/a/click"},
    Binding{Action::SecondaryFace, Hand::Left, "/input/b/click"},
    Binding{Action::Haptic, Hand::Left, "/output/haptic"},
    Binding{Action::AimPose, Hand::Right, "/input/aim/pose"},
    Binding{Action::GripPose, Hand::Right, "/input/grip/pose"},
    Binding{Action::Trigger, Hand::Right, "/input/trigger/value"},
    Binding{Action::Squeeze, Hand::Right, "/input/squeeze/value"},
    Binding{Action::MovementTurnAxis, Hand::Right, "/input/thumbstick"},
    Binding{Action::AxisClick, Hand::Right, "/input/thumbstick/click"},
    Binding{Action::PrimaryFace, Hand::Right, "/input/a/click"},
    Binding{Action::SecondaryFace, Hand::Right, "/input/b/click"},
    Binding{Action::Haptic, Hand::Right, "/output/haptic"},
};

constexpr std::array kViveWandBindings = {
    Binding{Action::AimPose, Hand::Left, "/input/aim/pose"},
    Binding{Action::GripPose, Hand::Left, "/input/grip/pose"},
    Binding{Action::Trigger, Hand::Left, "/input/trigger/value"},
    Binding{Action::Squeeze, Hand::Left, "/input/squeeze/click"},
    Binding{Action::MovementTurnAxis, Hand::Left, "/input/trackpad"},
    Binding{Action::AxisClick, Hand::Left, "/input/trackpad/click"},
    Binding{Action::Menu, Hand::Left, "/input/menu/click"},
    Binding{Action::Haptic, Hand::Left, "/output/haptic"},
    Binding{Action::AimPose, Hand::Right, "/input/aim/pose"},
    Binding{Action::GripPose, Hand::Right, "/input/grip/pose"},
    Binding{Action::Trigger, Hand::Right, "/input/trigger/value"},
    Binding{Action::Squeeze, Hand::Right, "/input/squeeze/click"},
    Binding{Action::MovementTurnAxis, Hand::Right, "/input/trackpad"},
    Binding{Action::AxisClick, Hand::Right, "/input/trackpad/click"},
    Binding{Action::PrimaryFace, Hand::Right, "/input/menu/click"},
    Binding{Action::Haptic, Hand::Right, "/output/haptic"},
};

constexpr std::array kSimpleBindings = {
    Binding{Action::AimPose, Hand::Left, "/input/aim/pose"},
    Binding{Action::PrimaryFace, Hand::Left, "/input/select/click"},
    Binding{Action::Haptic, Hand::Left, "/output/haptic"},
    Binding{Action::AimPose, Hand::Right, "/input/aim/pose"},
    Binding{Action::PrimaryFace, Hand::Right, "/input/select/click"},
    Binding{Action::Haptic, Hand::Right, "/output/haptic"},
};

const std::array kProfiles = {
    Profile{
        "/interaction_profiles/oculus/touch_controller",
        kTouchBindings},
    Profile{
        "/interaction_profiles/valve/index_controller",
        kIndexBindings},
    Profile{
        "/interaction_profiles/htc/vive_controller",
        kViveWandBindings},
    Profile{
        "/interaction_profiles/khr/simple_controller",
        kSimpleBindings},
};
} // namespace

namespace bfvr
{
std::span<const OpenXRControllerProfileBindings>
OpenXRControllerBindingProfiles() noexcept
{
    return kProfiles;
}
} // namespace bfvr
