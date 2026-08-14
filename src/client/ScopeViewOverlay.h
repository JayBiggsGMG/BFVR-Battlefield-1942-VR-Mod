#pragma once

#include "stereo/StereoMath.h"

#include <cstdint>

namespace bfvr
{

struct ScopeViewFrameState
{
    // Validated item/soldier-scoped aim target. When enabled, a bounded
    // delta-time-aware angular stabilizer attenuates only micro-motion and
    // catches up to raw at 0.40 degrees. This same ray drives scoped
    // presentation and native-authority convergence.
    stereo::Matrix4 controllerGunWorld = {};
    const void* weapon = nullptr;
    const void* soldier = nullptr;
    std::int32_t controllerGeneration = 0;
    float normalFov = -1.0F;
    float projectionScale = 1.0F;
    bool offHandSupported = false;
};

// Observes the profiled FireArms::setZoom boundary. Only an actual local
// active-item receiver whose template enables BF1942's scope overlay may arm
// BFVR's weapon-directed stereo scope view.
void StartScopeViewOverlay(
    void* gameImage,
    void (*appendLog)(const wchar_t* message));
void StopScopeViewOverlay();

// Polls the profiled local BFPlayer alive byte from the game Present thread.
// Death releases both BFVR's scope camera/frustum state and the native weapon
// zoom before the deploy menu is presented.
void PollScopeViewPlayerLifecycle() noexcept;

// Consumed by the RenderView owner after death. FireArms::setZoom(false)
// restores the weapon target, but its dead-owner update no longer advances the
// active RenderView back from the scope FOV.
[[nodiscard]] bool ConsumeScopeViewNormalFovRestore(
    float& normalFov) noexcept;

// The multiplayer compact-action route can replay conflicting native soldier
// zoom bits after one alt-fire edge. Controller and native mouse notifications
// share one exact-local useScope toggle lifetime, keeping the FireArms request
// and local BFSoldier 0x20 state bit consistent until the next input edge.
void NotifyMultiplayerInfantryAltFirePulse() noexcept;
void NotifyMultiplayerNativeAltFireInput() noexcept;

// Updated from the x86 process's live UserConfig polling path. Disabling the
// feature immediately clears stabilization history; re-enabling starts from
// fresh exact scoped samples without carrying an old direction across it.
void SetSniperScopeSmoothingEnabled(bool enabled) noexcept;

// Arms observation of BF1942's native post-fire zoom decision for the exact
// owned scope lifetime. BFVR retains its input latch until the weapon/gameplay
// state itself requests zoom-off, so a synthetic alt-fire release cannot force
// a modded weapon out of a scope it natively retains.
void NotifyScopeViewLocalWeaponFired(
    const void* soldier,
    const void* weapon) noexcept;

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
