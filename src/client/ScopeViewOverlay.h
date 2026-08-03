#pragma once

#include "stereo/StereoMath.h"

#include <cstdint>

namespace bfvr
{

struct ScopeViewFrameState
{
    // One validated item/soldier-scoped gun basis is shared by camera replay
    // and the scoped WeaponFire_Core fallback. Consumers must verify both
    // lifetime pointers before using it.
    stereo::Matrix4 controllerGunWorld = {};
    const void* weapon = nullptr;
    const void* soldier = nullptr;
    std::int32_t controllerGeneration = 0;
    float normalFov = -1.0F;
    float projectionScale = 1.0F;
};

// Observes the profiled FireArms::setZoom boundary. Only an actual local
// active-item receiver whose template enables BF1942's scope overlay may arm
// BFVR's weapon-directed stereo scope view.
void StartScopeViewOverlay(
    void* gameImage,
    void (*appendLog)(const wchar_t* message));
void StopScopeViewOverlay();

[[nodiscard]] bool ReadScopeViewFrameState(
    ScopeViewFrameState& state) noexcept;
void InvalidateScopeViewFrameState(const void* weapon) noexcept;
[[nodiscard]] bool IsScopeViewActive() noexcept;
[[nodiscard]] float ReadScopeViewProjectionScale() noexcept;

} // namespace bfvr
