#pragma once

#include "stereo/StereoMath.h"

#include <optional>

namespace bfvr::stereo
{

struct WeaponRecoilAngles
{
    float pitch = 0.0F;
    float yaw = 0.0F;
};

// Produces the post-multiplied rigid attachment transform from a calibrated
// controller grip pose to the current controller grip pose, expressed in the
// current HMD's view space. All poses are OpenXR local-space poses at their
// matching predicted display time. The resulting matrix is exactly
// inverse(referenceGripInHead) * currentGripInHead in BFVR's D3D8 row-vector
// convention, so rotation occurs about the tracked grip rather than the HMD or
// the weapon model's origin. Translation is scaled to BF1942 world units.
//
// A first-person weapon path can apply this delta to its original view-model
// placement: the game owns recoil, reload, weapon switching, and its original
// anchor, while valid controller motion supplies only a local visual offset.
// The function fails closed for invalid tracking, an invalid scale, or a
// discontinuous controller translation greater than maximumTranslationMeters.
[[nodiscard]] std::optional<Matrix4> MakeD3D8ViewSpaceWeaponDelta(
    const Pose& referenceHead,
    const Pose& referenceGrip,
    const Pose& currentHead,
    const Pose& currentGrip,
    float worldUnitsPerMeter,
    float maximumTranslationMeters) noexcept;

// Produces the same rigid attachment delta in the HMD coordinate basis captured
// at calibration, not the HMD pose from the current tracking sample. Later HMD
// movement alone therefore cannot create controller-driven weapon motion. The
// current grip remains a LOCAL-space OpenXR pose at the matching predicted
// display time; only its motion relative to the calibration-time basis drives
// the viewmodel offset.
[[nodiscard]] std::optional<Matrix4> MakeD3D8CalibrationSpaceWeaponDelta(
    const Pose& calibrationHead,
    const Pose& referenceGrip,
    const Pose& currentGrip,
    float worldUnitsPerMeter,
    float maximumTranslationMeters) noexcept;

// Composes a captured controller-to-weapon reference with motion measured
// after that reference. Both matrices use the D3D8 row-vector convention, so
// a vertex experiences baseOffset first and controllerMotion second. This is
// pure math; it does not supply an arbitrary model scale or alter game state.
[[nodiscard]] std::optional<Matrix4> ComposeD3D8ViewSpaceWeaponDeltas(
    const Matrix4& baseOffset,
    const Matrix4& controllerMotion) noexcept;

// Captures a portable grip-to-weapon attachment. targetViewDelta is the
// desired weapon delta at commit; the returned A satisfies
// A * gripAtCommit == targetViewDelta in D3D8 row-vector order.
[[nodiscard]] std::optional<Matrix4> MakeD3D8ControllerToWeaponAttachment(
    const Pose& calibrationHead,
    const Pose& gripAtCommit,
    const Matrix4& targetViewDelta,
    float worldUnitsPerMeter) noexcept;

// Reconstructs the current weapon view delta from a portable controller
// attachment and the current grip relative to the current HMD pose. No prior
// spawn, menu, or controller sample participates in this reconstruction.
[[nodiscard]] std::optional<Matrix4> MakeD3D8AttachedWeaponViewDelta(
    const Matrix4& controllerToWeaponAttachment,
    const Pose& currentHead,
    const Pose& currentGrip,
    float worldUnitsPerMeter) noexcept;

// Removes only the current OpenXR LOCAL head View from a classified D3D8 View.
// The result is the current player/body frame; it is never captured or reused
// by a later frame.
[[nodiscard]] std::optional<Matrix4> MakeD3D8CurrentBodyView(
    const Matrix4& sourceView,
    const Pose& currentHead,
    float worldUnitsPerMeter) noexcept;

// Captures a portable absolute-LOCAL-grip-to-weapon attachment for a body-frame
// target. Unlike the legacy calibration helper, this deliberately takes no HMD
// pose: the current grip is already expressed in OpenXR LOCAL.
[[nodiscard]] std::optional<Matrix4> MakeD3D8AbsoluteGripToWeaponAttachment(
    const Pose& gripAtCommit,
    const Matrix4& targetBodyViewDelta,
    float worldUnitsPerMeter) noexcept;

// Reconstructs a body-frame weapon delta from a fixed absolute-LOCAL-grip
// attachment and the current grip. Headset motion cannot enter this delta.
[[nodiscard]] std::optional<Matrix4> MakeD3D8AbsoluteGripWeaponDelta(
    const Matrix4& absoluteGripToWeaponAttachment,
    const Pose& currentGrip,
    float worldUnitsPerMeter) noexcept;

// Reconstructs the current weapon delta with native recoil rotated around the
// tracked grip pivot. This composes attachment * recoil * grip, rather than
// rotating attachment itself: the controller grip stays fixed while the gun
// tilts around it. The direct BFSoldier degree-angle values are converted once
// and inverted because a camera View and physical gun use opposite senses.
[[nodiscard]] std::optional<Matrix4>
MakeD3D8AbsoluteGripWeaponRecoilDelta(
    const Matrix4& absoluteGripToWeaponAttachment,
    const Pose& currentGrip,
    float worldUnitsPerMeter,
    float pitch,
    float yaw) noexcept;

// The flat camera consumes a time-ordered sequence of native recoil impulses.
// BFVR accumulates those exact finite impulses into a weapon-relative angle so
// the player counteracts the weapon with the controller rather than moving the
// HMD. No generic recoil curve is introduced.
[[nodiscard]] std::optional<WeaponRecoilAngles>
AccumulateD3D8WeaponRecoil(
    const WeaponRecoilAngles& current,
    float pitchImpulse,
    float yawImpulse) noexcept;

// Converts a view-space delta through the supplied frame View into the exact
// World-space attachment needed for that draw. For a held weapon the frame is
// the body View derived from the exact current classified source View/current
// head pair; it must not be a sampled body/head frame, immutable calibration
// View, or eye View.
[[nodiscard]] std::optional<Matrix4> MakeD3D8WorldSpaceWeaponDelta(
    const Matrix4& frameView,
    const Matrix4& frameViewDelta) noexcept;

// Re-expresses a World-space attachment in its supplied current frame View.
// Development calibration uses this inverse conversion so the frozen target
// does not jump.
[[nodiscard]] std::optional<Matrix4> MakeD3D8CalibrationViewWeaponOffset(
    const Matrix4& frameView,
    const Matrix4& worldSpaceDelta) noexcept;

// Derives the legacy first-person perspective morph relative to BF1942's
// ordinary world projection. The viewmodel projection contributes only its
// X/Y focal terms and projection centre; the ordinary projection retains the
// depth mapping. Applying the returned correction before the controller
// attachment preserves the authored viewmodel shape without multiplying
// metric controller translation.
[[nodiscard]] std::optional<Matrix4>
MakeD3D8ViewModelPerspectiveCorrection(
    const Matrix4& viewModelProjection,
    const Matrix4& ordinaryWorldProjection) noexcept;

// Applies a controller-derived view-space delta to a D3D8 shader WVP matrix.
// This is deliberately restricted to presentation math: it neither changes
// BF1942's camera, weapon object, projectile origin, nor player input. The
// caller must provide the exact per-eye projection used for the draw, so the
// offset remains spatially correct in both eye images.
[[nodiscard]] std::optional<Matrix4> ApplyViewSpaceWeaponDeltaToD3D8Wvp(
    const Matrix4& sourceWvp,
    const Matrix4& eyeProjection,
    const Matrix4& viewSpaceDelta) noexcept;

// Produces a temporary fixed-function World transform whose World * View
// product applies viewSpaceDelta immediately before the supplied game View.
// This is the fixed-function equivalent of the WVP helper above. It is pure
// presentation math; callers must still prove the draw is the shared local
// first-person weapon path and restore BF1942's original World transform.
[[nodiscard]] std::optional<Matrix4> ApplyViewSpaceWeaponDeltaToD3D8World(
    const Matrix4& sourceWorld,
    const Matrix4& sourceView,
    const Matrix4& viewSpaceDelta) noexcept;

// Applies one already-derived per-frame World-space attachment directly after
// the game-owned World matrix. It accepts no eye View, so the original draw
// and both eye replays share the exact same attachment for that frame.
[[nodiscard]] std::optional<Matrix4> ApplyWorldSpaceWeaponDeltaToD3D8World(
    const Matrix4& sourceWorld,
    const Matrix4& worldSpaceDelta) noexcept;

} // namespace bfvr::stereo
