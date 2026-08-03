#pragma once

#include "stereo/StereoMath.h"

#include <windows.h>

namespace bfvr
{

struct TrackedScopeAimSample
{
    stereo::Matrix4 controllerGunWorld = {};
    stereo::Matrix4 leftGripWorld = {};
    const void* soldier = nullptr;
    LONG controllerGeneration = 0;
    float leftSqueezeValue = 0.0F;
    bool sessionFocused = false;
    bool leftGripTracked = false;
    bool leftSqueezeActive = false;
};

// Reconstructs the same raw right-controller aim * current soldier-world basis
// used by native 1P arm IK, but does not depend on BF1942 updating or rendering
// that arm. It is read-only and intended only for an already-verified scope.
[[nodiscard]] bool ReadFreshTrackedScopeAim(
    const void* expectedSoldier,
    TrackedScopeAimSample& result,
    DWORD maximumAgeMs) noexcept;

} // namespace bfvr
