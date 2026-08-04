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

// The multiplayer compact-action route can replay conflicting native soldier
// zoom bits after one alt-fire edge. Controller and native mouse notifications
// share one exact-local useScope toggle lifetime, keeping the FireArms request
// and local BFSoldier 0x20 state bit consistent until the next input edge.
void NotifyMultiplayerInfantryAltFirePulse() noexcept;
void NotifyMultiplayerNativeAltFireInput() noexcept;

[[nodiscard]] bool ReadScopeViewFrameState(
    ScopeViewFrameState& state) noexcept;
void InvalidateScopeViewFrameState(const void* weapon) noexcept;
[[nodiscard]] bool IsScopeViewActive() noexcept;
// True only while an exact owned multiplayer scope is entering native
// FireArms::setZoom(true). Nested HUD calls must treat this as scope-visible
// even though the native call has not yet populated its saved normal FOV.
[[nodiscard]] bool IsScopeViewActivationPending() noexcept;
[[nodiscard]] float ReadScopeViewProjectionScale() noexcept;

// Records the projection matrices that will actually be used by a mirrored
// perspective draw, then correlates them with a successfully published frame.
// Transition/presenter flags alone do not prove visible world magnification.
void RecordScopeViewProjectionReplay(
    std::int32_t frameSequence,
    const stereo::Matrix4& sourceProjection,
    const stereo::Matrix4& leftProjection,
    const stereo::Matrix4& rightProjection) noexcept;
void NotifyScopeViewFramePublished(std::int32_t frameSequence) noexcept;

} // namespace bfvr
