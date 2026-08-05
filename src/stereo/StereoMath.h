#pragma once

#include <array>
#include <optional>

namespace bfvr::stereo
{
// All input poses use OpenXR's reference-space convention: metres, +X right,
// +Y up, and -Z forward. Orientation maps eye-local coordinates into that
// reference space.
struct Vec3
{
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct Vec4
{
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 0.0F;
};

struct Quaternion
{
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 1.0F;
};

struct Pose
{
    Vec3 position = {};
    Quaternion orientation = {};
};

struct EyePoses
{
    Pose left = {};
    Pose right = {};
};

// These are tan(angleLeft), tan(angleRight), tan(angleUp), and
// tan(angleDown), matching the values derived from XrFovf angles. They avoid
// an OpenXR-header dependency so this module remains independently testable.
struct FovTangents
{
    float left = 0.0F;
    float right = 0.0F;
    float up = 0.0F;
    float down = 0.0F;
};

// Matrix storage is D3D8/D3DMATRIX-compatible: row-major elements consumed by
// a row vector, i.e. clipPosition = position * matrix. The projection it
// builds uses the D3D8 left-handed clip-depth interval [0, w].
struct Matrix4
{
    std::array<std::array<float, 4>, 4> values = {};
};

struct D3D8StereoTransformPair
{
    Matrix4 leftView = {};
    Matrix4 rightView = {};
    Matrix4 leftProjection = {};
    Matrix4 rightProjection = {};
};

// Expresses current in reference-local coordinates. Translation is measured
// from the reference position and rotated by the inverse reference
// orientation, keeping headset and controller rebasing on one transform.
[[nodiscard]] std::optional<Pose> MakeRelativePose(
    const Pose& reference,
    const Pose& current,
    float positionScale = 1.0F) noexcept;

// Converts a position or direction from OpenXR's right-handed convention to
// D3D8's conventional left-handed world convention by preserving X/Y and
// negating Z. It does not normalize directions.
[[nodiscard]] Vec3 OpenXRToD3D8Coordinates(Vec3 value) noexcept;

// Places eyes at +/- ipd/2 metres on the head-local X axis, then rotates that
// offset by the head orientation. Returns nullopt for non-finite input, a zero
// quaternion, or a negative IPD.
[[nodiscard]] std::optional<EyePoses> ComputeEyePoses(const Pose& headPose, float ipdMeters) noexcept;

// Reconstructs a defensive centre-view pose from two OpenXR eye poses. The
// position is their midpoint and the orientation is their shortest-path
// normalized midpoint, including q/-q equivalence. Runtime code should prefer
// locating XR_REFERENCE_SPACE_TYPE_VIEW directly; this exists for fallback
// compatibility and deterministic testing.
[[nodiscard]] std::optional<Pose> ComputeCentreViewPose(
    const Pose& leftEye,
    const Pose& rightEye) noexcept;

// Builds the D3D8 fixed-function View transform for a pose expressed in the
// OpenXR convention. It applies C = diag(1, 1, -1) before taking the rigid
// inverse, then packs the result for D3D8 row-vector multiplication.
[[nodiscard]] std::optional<Matrix4> MakeD3D8ViewFromOpenXRPose(const Pose& eyePose) noexcept;

// Builds an asymmetric D3D8 left-handed perspective matrix from per-eye FOV
// tangents and near/far distances in metres. Returns nullopt for non-finite or
// degenerate frusta, or for an invalid depth interval.
[[nodiscard]] std::optional<Matrix4> MakeD3D8ProjectionFromFovTangents(
    const FovTangents& fov,
    float nearZ,
    float farZ) noexcept;

// Derives a diagnostic parallel-axis stereo pair from the D3D8 View and
// Projection matrices already active for a game draw. The two views receive
// opposite view-space X translations. The projection centres receive the
// matching opposite shifts for the requested convergence distance. This is a
// renderer proof aid; the engine-unit scale must be calibrated before use as
// physical IPD.
[[nodiscard]] std::optional<D3D8StereoTransformPair> MakeDiagnosticD3D8StereoPair(
    const Matrix4& sourceView,
    const Matrix4& sourceProjection,
    float halfEyeOffset,
    float convergenceDistance) noexcept;

// Builds a first native-target stereo pair from two runtime eye poses/FOVs.
// It preserves the game's camera orientation and position, applies the
// runtime eye separation along game-camera X, and derives the game's near/far
// interval for the asymmetric per-eye projections. Full head-orientation and
// positional camera composition remains a separate engine-camera bridge.
[[nodiscard]] std::optional<D3D8StereoTransformPair> MakeRuntimeFovD3D8StereoPair(
    const Matrix4& sourceView,
    const Matrix4& sourceProjection,
    const Pose& leftEye,
    const Pose& rightEye,
    const FovTangents& leftFov,
    const FovTangents& rightFov,
    float worldUnitsPerMeter) noexcept;

// Composes runtime head orientation, translation, IPD, and asymmetric FOV
// with the game's existing camera. referenceHead is the neutral centre-eye
// pose captured when presentation begins. Each current eye pose is expressed
// relative to that neutral pose, converted to a D3D8 rigid view transform,
// and post-multiplied onto sourceView so physical motion remains local to the
// current game camera.
[[nodiscard]] std::optional<D3D8StereoTransformPair> MakeRuntimePoseD3D8StereoPair(
    const Matrix4& sourceView,
    const Matrix4& sourceProjection,
    const Pose& referenceHead,
    const Pose& leftEye,
    const Pose& rightEye,
    const FovTangents& leftFov,
    const FovTangents& rightFov,
    float worldUnitsPerMeter) noexcept;

// Applies a current centre-head pose, relative to a neutral reference pose,
// to a game camera-to-world transform. The OpenXR relative pose is converted
// to D3D8 coordinates and pre-multiplied so the motion occurs in the current
// game camera's local space. This is the renderer-camera counterpart to the
// residual per-eye view transforms above.
[[nodiscard]] std::optional<Matrix4> ComposeRuntimeHeadWithD3D8Camera(
    const Matrix4& sourceCamera,
    const Pose& referenceHead,
    const Pose& currentHead,
    float worldUnitsPerMeter) noexcept;

// Multiplies a homogeneous row vector by a D3D8-compatible matrix without
// applying a perspective divide. This is primarily a deterministic test aid.
[[nodiscard]] Vec4 TransformRowVector(const Vec4& value, const Matrix4& matrix) noexcept;
}
