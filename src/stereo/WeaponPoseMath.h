#pragma once

#include "stereo/StereoMath.h"

#include <optional>

namespace bfvr::stereo
{

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
// attachment and the absolute current grip in the fixed calibration-HMD basis.
[[nodiscard]] std::optional<Matrix4> MakeD3D8AttachedWeaponViewDelta(
    const Matrix4& controllerToWeaponAttachment,
    const Pose& calibrationHead,
    const Pose& currentGrip,
    float worldUnitsPerMeter) noexcept;

// Removes only relative physical HMD pose from the current source View. The
// result follows BF1942 player/body translation and rotation while remaining
// unchanged for head-only motion. calibrationHead is the HMD basis used by the
// controller attachment.
[[nodiscard]] std::optional<Matrix4> MakeD3D8PlayerBodyWeaponView(
    const Matrix4& currentSourceView,
    const Pose& calibrationHead,
    const Pose& currentHead,
    float worldUnitsPerMeter) noexcept;

// Converts a view-space delta through the supplied frame View into the exact
// World-space attachment needed for that draw. For a held weapon the frame is
// the current head-cancelled player/body View, not an immutable calibration
// View and not the current head-bearing source View.
[[nodiscard]] std::optional<Matrix4> MakeD3D8WorldSpaceWeaponDelta(
    const Matrix4& frameView,
    const Matrix4& frameViewDelta) noexcept;

// Re-expresses a World-space attachment in a supplied frame View. Calibration
// uses this inverse conversion when changing its HMD/controller basis so the
// frozen target does not jump.
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
