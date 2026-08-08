#pragma once

#include "stereo/StereoMath.h"

#include <windows.h>

#include <cstdint>

namespace bfvr
{

struct ScopedOffHandSupportPose
{
    stereo::Matrix4 oneHandGunWorld = {};
    stereo::Matrix4 predictedSupportWorld = {};
    std::uint64_t bindingId = 0;
    bool supported = false;
};

// Publishes the pure geometry already evaluated by the native-arm primary
// support path. Scope view may retain it after BF1942 hides arm callbacks, but
// this cache neither owns nor changes grip acquisition or weapon/fire state.
void PublishScopedOffHandSupportPose(
    std::uint64_t bindingId,
    const stereo::Matrix4& oneHandGunWorld,
    const stereo::Matrix4& predictedSupportWorld,
    bool supported) noexcept;

[[nodiscard]] bool ReadFreshScopedOffHandSupportPose(
    std::uint64_t expectedBindingId,
    ScopedOffHandSupportPose& pose,
    DWORD maximumAgeMs) noexcept;

// The native-arm binding publishes this for both primary and close sidearm
// support. It lets per-shot feedback use the actual acquired two-hand state
// without interpreting raw squeeze buttons independently.
void PublishCurrentOffHandSupportState(
    std::uint64_t bindingId,
    bool supported) noexcept;
[[nodiscard]] bool IsFreshCurrentOffHandSupportHeld(
    DWORD maximumAgeMs) noexcept;

} // namespace bfvr
