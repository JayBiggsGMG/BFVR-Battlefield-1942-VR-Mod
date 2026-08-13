#include "openxr/OpenXRControllerBindingPolicy.h"

#include <algorithm>
#include <cstdio>
#include <string_view>

namespace
{
using Action = bfvr::OpenXRControllerAction;
using Binding = bfvr::OpenXRControllerBindingSeed;
using Hand = bfvr::OpenXRControllerHand;
using Profile = bfvr::OpenXRControllerProfileBindings;

bool Expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(
            stderr,
            "OpenXR controller binding policy test failed: %s\n",
            message);
    }
    return condition;
}

const Profile* FindProfile(std::string_view path)
{
    const auto profiles = bfvr::OpenXRControllerBindingProfiles();
    const auto found = std::find_if(
        profiles.begin(),
        profiles.end(),
        [&](const Profile& profile) {
            return profile.interactionProfile != nullptr &&
                path == profile.interactionProfile;
        });
    return found == profiles.end() ? nullptr : &*found;
}

bool HasBinding(
    const Profile* profile,
    Action action,
    Hand hand,
    std::string_view componentPath)
{
    return profile != nullptr && std::any_of(
        profile->bindings.begin(),
        profile->bindings.end(),
        [&](const Binding& binding) {
            return binding.action == action && binding.hand == hand &&
                binding.componentPath != nullptr &&
                componentPath == binding.componentPath;
        });
}
} // namespace

int main()
{
    bool passed = true;
    const auto profiles = bfvr::OpenXRControllerBindingProfiles();
    passed &= Expect(profiles.size() == 5, "exactly five profiles expected");

    const Profile* touch = FindProfile(
        "/interaction_profiles/oculus/touch_controller");
    passed &= Expect(
        touch != nullptr && touch->bindings.size() == 19,
        "Touch bindings must remain complete");
    passed &= Expect(
        HasBinding(touch, Action::PrimaryFace, Hand::Left, "/input/x/click") &&
            HasBinding(
                touch,
                Action::SecondaryFace,
                Hand::Left,
                "/input/y/click") &&
            HasBinding(
                touch,
                Action::PrimaryFace,
                Hand::Right,
                "/input/a/click") &&
            HasBinding(
                touch,
                Action::SecondaryFace,
                Hand::Right,
                "/input/b/click"),
        "Touch X/Y/A/B semantics must not change");

    const Profile* index = FindProfile(
        "/interaction_profiles/valve/index_controller");
    passed &= Expect(
        index != nullptr && index->bindings.size() == 18,
        "Index must bind the complete non-menu layout");
    for (const Hand hand : {Hand::Left, Hand::Right})
    {
        passed &= Expect(
            HasBinding(index, Action::PrimaryFace, hand, "/input/a/click") &&
                HasBinding(
                    index,
                    Action::SecondaryFace,
                    hand,
                    "/input/b/click"),
            "Index A/B must preserve per-hand primary/secondary semantics");
    }

    const Profile* vive = FindProfile(
        "/interaction_profiles/htc/vive_controller");
    passed &= Expect(
        vive != nullptr && vive->bindings.size() == 16,
        "Vive Wand must expose its bounded fallback layout");
    passed &= Expect(
        HasBinding(
            vive,
            Action::MovementTurnAxis,
            Hand::Left,
            "/input/trackpad") &&
            HasBinding(
                vive,
                Action::MovementTurnAxis,
                Hand::Right,
                "/input/trackpad") &&
            HasBinding(
                vive,
                Action::Squeeze,
                Hand::Right,
                "/input/squeeze/click") &&
            HasBinding(
                vive,
                Action::Menu,
                Hand::Left,
                "/input/menu/click") &&
            HasBinding(
                vive,
                Action::PrimaryFace,
                Hand::Right,
                "/input/menu/click"),
        "Vive Wand trackpad, squeeze, map, and Quick Menu fallbacks required");

    const Profile* microsoft = FindProfile(
        "/interaction_profiles/microsoft/motion_controller");
    passed &= Expect(
        microsoft != nullptr && microsoft->bindings.size() == 18,
        "Microsoft motion controllers must expose the Odyssey/WMR layout");
    for (const Hand hand : {Hand::Left, Hand::Right})
    {
        passed &= Expect(
            HasBinding(
                microsoft,
                Action::MovementTurnAxis,
                hand,
                "/input/thumbstick") &&
                HasBinding(
                    microsoft,
                    Action::PrimaryFace,
                    hand,
                    "/input/trackpad/click") &&
                HasBinding(
                    microsoft,
                    Action::Squeeze,
                    hand,
                    "/input/squeeze/click"),
            "Microsoft thumbstick, trackpad, and squeeze bindings required");
    }

    if (!passed)
    {
        return 1;
    }
    std::puts("OpenXR controller binding policy tests passed.");
    return 0;
}
