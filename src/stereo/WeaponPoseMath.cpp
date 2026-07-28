#include "stereo/WeaponPoseMath.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{

using bfvr::stereo::Matrix4;
using bfvr::stereo::Pose;
using bfvr::stereo::Quaternion;
using bfvr::stereo::Vec3;

bool IsFinite(float value) noexcept
{
    return std::isfinite(value);
}

bool IsFinite(const Vec3& value) noexcept
{
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
}

bool IsFinite(const Quaternion& value) noexcept
{
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z) &&
        IsFinite(value.w);
}

bool IsFinite(const Matrix4& value) noexcept
{
    for (const auto& row : value.values)
    {
        for (float element : row)
        {
            if (!IsFinite(element))
            {
                return false;
            }
        }
    }
    return true;
}

std::optional<Quaternion> Normalize(const Quaternion& value) noexcept
{
    if (!IsFinite(value))
    {
        return std::nullopt;
    }
    const float lengthSquared =
        value.x * value.x + value.y * value.y +
        value.z * value.z + value.w * value.w;
    if (!IsFinite(lengthSquared) || lengthSquared <= 0.000001F)
    {
        return std::nullopt;
    }
    const float reciprocalLength = 1.0F / std::sqrt(lengthSquared);
    if (!IsFinite(reciprocalLength))
    {
        return std::nullopt;
    }
    return Quaternion{
        value.x * reciprocalLength,
        value.y * reciprocalLength,
        value.z * reciprocalLength,
        value.w * reciprocalLength};
}

Quaternion Conjugate(const Quaternion& value) noexcept
{
    return {-value.x, -value.y, -value.z, value.w};
}

Quaternion Multiply(const Quaternion& lhs, const Quaternion& rhs) noexcept
{
    return {
        lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.w * rhs.y - lhs.x * rhs.z + lhs.y * rhs.w + lhs.z * rhs.x,
        lhs.w * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.w,
        lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z};
}

Vec3 Add(const Vec3& lhs, const Vec3& rhs) noexcept
{
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

Vec3 Subtract(const Vec3& lhs, const Vec3& rhs) noexcept
{
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

Vec3 Scale(const Vec3& value, float scalar) noexcept
{
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

Vec3 Cross(const Vec3& lhs, const Vec3& rhs) noexcept
{
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x};
}

Vec3 Rotate(const Quaternion& orientation, const Vec3& value) noexcept
{
    const Vec3 axis{orientation.x, orientation.y, orientation.z};
    const Vec3 doubledCross = Scale(Cross(axis, value), 2.0F);
    return Add(
        value,
        Add(Scale(doubledCross, orientation.w), Cross(axis, doubledCross)));
}

std::optional<Pose> MakeHeadRelativePose(
    const Pose& head,
    const Pose& grip) noexcept
{
    if (!IsFinite(head.position) || !IsFinite(grip.position))
    {
        return std::nullopt;
    }
    const auto headOrientation = Normalize(head.orientation);
    const auto gripOrientation = Normalize(grip.orientation);
    if (!headOrientation.has_value() || !gripOrientation.has_value())
    {
        return std::nullopt;
    }
    const Quaternion inverseHead = Conjugate(*headOrientation);
    const Pose relative = {
        Rotate(inverseHead, Subtract(grip.position, head.position)),
        Multiply(inverseHead, *gripOrientation)};
    const auto normalizedOrientation = Normalize(relative.orientation);
    if (!IsFinite(relative.position) || !normalizedOrientation.has_value())
    {
        return std::nullopt;
    }
    return Pose{relative.position, *normalizedOrientation};
}

bool HasContinuousPoseDelta(
    const Pose& reference,
    const Pose& current,
    float maximumTranslationMeters) noexcept
{
    if (!IsFinite(maximumTranslationMeters) || maximumTranslationMeters <= 0.0F)
    {
        return false;
    }
    const auto referenceOrientation = Normalize(reference.orientation);
    const auto currentOrientation = Normalize(current.orientation);
    if (!referenceOrientation.has_value() || !currentOrientation.has_value())
    {
        return false;
    }
    const Vec3 position = Subtract(current.position, reference.position);
    const float distanceSquared =
        position.x * position.x + position.y * position.y + position.z * position.z;
    return IsFinite(position) && IsFinite(distanceSquared) &&
        distanceSquared <=
            maximumTranslationMeters * maximumTranslationMeters;
}

std::optional<Matrix4> MakeD3D8RigidTransform(
    const Pose& pose,
    float worldUnitsPerMeter) noexcept
{
    if (!IsFinite(pose.position) || !IsFinite(worldUnitsPerMeter) ||
        worldUnitsPerMeter <= 0.0F)
    {
        return std::nullopt;
    }
    const auto d3dView = bfvr::stereo::MakeD3D8ViewFromOpenXRPose(pose);
    if (!d3dView.has_value())
    {
        return std::nullopt;
    }
    Matrix4 result = {};
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            result.values[row][column] = d3dView->values[column][row];
        }
    }
    const Vec3 position = bfvr::stereo::OpenXRToD3D8Coordinates(
        Scale(pose.position, worldUnitsPerMeter));
    result.values[3][0] = position.x;
    result.values[3][1] = position.y;
    result.values[3][2] = position.z;
    result.values[3][3] = 1.0F;
    return IsFinite(result) ? std::optional<Matrix4>(result) : std::nullopt;
}

Matrix4 MultiplyMatrices(const Matrix4& lhs, const Matrix4& rhs) noexcept
{
    Matrix4 result = {};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            for (std::size_t inner = 0; inner < 4; ++inner)
            {
                result.values[row][column] +=
                    lhs.values[row][inner] * rhs.values[inner][column];
            }
        }
    }
    return result;
}

std::optional<Matrix4> InvertMatrix(const Matrix4& matrix) noexcept
{
    if (!IsFinite(matrix))
    {
        return std::nullopt;
    }
    std::array<std::array<float, 8>, 4> augmented = {};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            augmented[row][column] = matrix.values[row][column];
            augmented[row][column + 4] = row == column ? 1.0F : 0.0F;
        }
    }

    constexpr float kPivotEpsilon = 0.000001F;
    for (std::size_t pivotColumn = 0; pivotColumn < 4; ++pivotColumn)
    {
        std::size_t pivotRow = pivotColumn;
        for (std::size_t candidate = pivotColumn + 1; candidate < 4; ++candidate)
        {
            if (std::fabs(augmented[candidate][pivotColumn]) >
                std::fabs(augmented[pivotRow][pivotColumn]))
            {
                pivotRow = candidate;
            }
        }
        const float pivot = augmented[pivotRow][pivotColumn];
        if (!IsFinite(pivot) || std::fabs(pivot) <= kPivotEpsilon)
        {
            return std::nullopt;
        }
        if (pivotRow != pivotColumn)
        {
            std::swap(augmented[pivotRow], augmented[pivotColumn]);
        }
        for (float& element : augmented[pivotColumn])
        {
            element /= pivot;
        }
        for (std::size_t row = 0; row < 4; ++row)
        {
            if (row == pivotColumn)
            {
                continue;
            }
            const float factor = augmented[row][pivotColumn];
            for (std::size_t column = 0; column < 8; ++column)
            {
                augmented[row][column] -= factor * augmented[pivotColumn][column];
            }
        }
    }

    Matrix4 inverse = {};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            inverse.values[row][column] = augmented[row][column + 4];
        }
    }
    return IsFinite(inverse) ? std::optional<Matrix4>(inverse) : std::nullopt;
}

} // namespace

namespace bfvr::stereo
{

std::optional<Matrix4> MakeD3D8ViewSpaceWeaponDelta(
    const Pose& referenceHead,
    const Pose& referenceGrip,
    const Pose& currentHead,
    const Pose& currentGrip,
    float worldUnitsPerMeter,
    float maximumTranslationMeters) noexcept
{
    const auto reference = MakeHeadRelativePose(referenceHead, referenceGrip);
    const auto current = MakeHeadRelativePose(currentHead, currentGrip);
    if (!reference.has_value() || !current.has_value())
    {
        return std::nullopt;
    }
    if (!HasContinuousPoseDelta(
            *reference,
            *current,
            maximumTranslationMeters))
    {
        return std::nullopt;
    }

    // Treat the grip as a genuine parent transform. For row-vector D3D8
    // matrices, an object that starts at W follows the controller as:
    //
    //   W' = W * inverse(referenceGrip) * currentGrip
    //
    // The translation introduced by that conjugation is essential. Omitting
    // it produces a rotation-only delta, which rotates an already-positioned
    // weapon around the HMD/view origin instead of around the tracked grip.
    const auto referenceTransform =
        MakeD3D8RigidTransform(*reference, worldUnitsPerMeter);
    const auto currentTransform =
        MakeD3D8RigidTransform(*current, worldUnitsPerMeter);
    if (!referenceTransform.has_value() || !currentTransform.has_value())
    {
        return std::nullopt;
    }
    const auto inverseReference = InvertMatrix(*referenceTransform);
    if (!inverseReference.has_value())
    {
        return std::nullopt;
    }
    const Matrix4 result =
        MultiplyMatrices(*inverseReference, *currentTransform);
    return IsFinite(result) ? std::optional<Matrix4>(result) : std::nullopt;
}

std::optional<Matrix4> MakeD3D8CalibrationSpaceWeaponDelta(
    const Pose& calibrationHead,
    const Pose& referenceGrip,
    const Pose& currentGrip,
    float worldUnitsPerMeter,
    float maximumTranslationMeters) noexcept
{
    const auto reference =
        MakeHeadRelativePose(calibrationHead, referenceGrip);
    const auto current =
        MakeHeadRelativePose(calibrationHead, currentGrip);
    if (!reference.has_value() || !current.has_value() ||
        !HasContinuousPoseDelta(
            *reference,
            *current,
            maximumTranslationMeters))
    {
        return std::nullopt;
    }

    const auto referenceTransform =
        MakeD3D8RigidTransform(*reference, worldUnitsPerMeter);
    const auto currentTransform =
        MakeD3D8RigidTransform(*current, worldUnitsPerMeter);
    if (!referenceTransform.has_value() || !currentTransform.has_value())
    {
        return std::nullopt;
    }
    const auto inverseReference = InvertMatrix(*referenceTransform);
    if (!inverseReference.has_value())
    {
        return std::nullopt;
    }
    const Matrix4 result =
        MultiplyMatrices(*inverseReference, *currentTransform);
    return IsFinite(result) ? std::optional<Matrix4>(result) : std::nullopt;
}

std::optional<Matrix4> ComposeD3D8ViewSpaceWeaponDeltas(
    const Matrix4& baseOffset,
    const Matrix4& controllerMotion) noexcept
{
    if (!IsFinite(baseOffset) || !IsFinite(controllerMotion))
    {
        return std::nullopt;
    }
    const Matrix4 result = MultiplyMatrices(baseOffset, controllerMotion);
    return IsFinite(result) ? std::optional<Matrix4>(result) : std::nullopt;
}

std::optional<Matrix4> MakeD3D8ControllerToWeaponAttachment(
    const Pose& calibrationHead,
    const Pose& gripAtCommit,
    const Matrix4& targetViewDelta,
    float worldUnitsPerMeter) noexcept
{
    if (!IsFinite(targetViewDelta))
    {
        return std::nullopt;
    }
    const auto relativeGrip =
        MakeHeadRelativePose(calibrationHead, gripAtCommit);
    if (!relativeGrip.has_value())
    {
        return std::nullopt;
    }
    const auto gripTransform =
        MakeD3D8RigidTransform(*relativeGrip, worldUnitsPerMeter);
    if (!gripTransform.has_value())
    {
        return std::nullopt;
    }
    const auto inverseGrip = InvertMatrix(*gripTransform);
    if (!inverseGrip.has_value())
    {
        return std::nullopt;
    }
    const Matrix4 result = MultiplyMatrices(targetViewDelta, *inverseGrip);
    return IsFinite(result) ? std::optional<Matrix4>(result) : std::nullopt;
}

std::optional<Matrix4> MakeD3D8AttachedWeaponViewDelta(
    const Matrix4& controllerToWeaponAttachment,
    const Pose& currentHead,
    const Pose& currentGrip,
    float worldUnitsPerMeter) noexcept
{
    if (!IsFinite(controllerToWeaponAttachment))
    {
        return std::nullopt;
    }
    const auto relativeGrip =
        MakeHeadRelativePose(currentHead, currentGrip);
    if (!relativeGrip.has_value())
    {
        return std::nullopt;
    }
    const auto gripTransform =
        MakeD3D8RigidTransform(*relativeGrip, worldUnitsPerMeter);
    if (!gripTransform.has_value())
    {
        return std::nullopt;
    }
    const Matrix4 result = MultiplyMatrices(
        controllerToWeaponAttachment,
        *gripTransform);
    return IsFinite(result) ? std::optional<Matrix4>(result) : std::nullopt;
}

std::optional<Matrix4> MakeD3D8CurrentBodyView(
    const Matrix4& sourceView,
    const Pose& currentHead,
    float worldUnitsPerMeter) noexcept
{
    if (!IsFinite(sourceView) || !IsFinite(currentHead.position) ||
        !IsFinite(currentHead.orientation) || !std::isfinite(worldUnitsPerMeter) ||
        worldUnitsPerMeter <= 0.0F)
    {
        return std::nullopt;
    }
    const auto headView = MakeD3D8ViewFromOpenXRPose(currentHead);
    const auto headPose = headView.has_value() ? InvertMatrix(*headView) : std::nullopt;
    if (!headPose.has_value())
    {
        return std::nullopt;
    }
    const Matrix4 result = MultiplyMatrices(sourceView, *headPose);
    return IsFinite(result) ? std::optional<Matrix4>(result) : std::nullopt;
}

std::optional<Matrix4> MakeD3D8AbsoluteGripToWeaponAttachment(
    const Pose& gripAtCommit,
    const Matrix4& targetBodyViewDelta,
    float worldUnitsPerMeter) noexcept
{
    if (!IsFinite(targetBodyViewDelta))
    {
        return std::nullopt;
    }
    const auto gripTransform =
        MakeD3D8RigidTransform(gripAtCommit, worldUnitsPerMeter);
    const auto inverseGrip =
        gripTransform.has_value() ? InvertMatrix(*gripTransform) : std::nullopt;
    if (!inverseGrip.has_value())
    {
        return std::nullopt;
    }
    const Matrix4 result = MultiplyMatrices(targetBodyViewDelta, *inverseGrip);
    return IsFinite(result) ? std::optional<Matrix4>(result) : std::nullopt;
}

std::optional<Matrix4> MakeD3D8AbsoluteGripWeaponDelta(
    const Matrix4& absoluteGripToWeaponAttachment,
    const Pose& currentGrip,
    float worldUnitsPerMeter) noexcept
{
    if (!IsFinite(absoluteGripToWeaponAttachment))
    {
        return std::nullopt;
    }
    const auto gripTransform =
        MakeD3D8RigidTransform(currentGrip, worldUnitsPerMeter);
    if (!gripTransform.has_value())
    {
        return std::nullopt;
    }
    const Matrix4 result = MultiplyMatrices(
        absoluteGripToWeaponAttachment,
        *gripTransform);
    return IsFinite(result) ? std::optional<Matrix4>(result) : std::nullopt;
}

std::optional<Matrix4> MakeD3D8AbsoluteGripWeaponRecoilDelta(
    const Matrix4& absoluteGripToWeaponAttachment,
    const Pose& currentGrip,
    float worldUnitsPerMeter,
    float pitch,
    float yaw) noexcept
{
    if (!IsFinite(absoluteGripToWeaponAttachment) || !std::isfinite(pitch) ||
        !std::isfinite(yaw))
    {
        return std::nullopt;
    }
    const auto gripTransform =
        MakeD3D8RigidTransform(currentGrip, worldUnitsPerMeter);
    if (!gripTransform.has_value())
    {
        return std::nullopt;
    }
    // BFSoldier's recoil table/accessors are in the engine's degree-angle
    // convention. D3D matrix trigonometry is radians. Accumulate the native
    // values unchanged, then convert exactly once at this presentation edge.
    constexpr float kDegreesToRadians = 0.01745329251994329577F;
    // A camera View rotates the world in the inverse sense of a physical
    // held-gun transform. Preserve the native per-weapon magnitude/timing but
    // invert both axes while moving the effect from the legacy camera to the
    // weapon.
    const float pitchRadians = -pitch * kDegreesToRadians;
    const float yawRadians = -yaw * kDegreesToRadians;
    const float pitchCosine = std::cos(pitchRadians);
    const float pitchSine = std::sin(pitchRadians);
    const float yawCosine = std::cos(yawRadians);
    const float yawSine = std::sin(yawRadians);
    if (!IsFinite(pitchCosine) || !IsFinite(pitchSine) ||
        !IsFinite(yawCosine) || !IsFinite(yawSine))
    {
        return std::nullopt;
    }

    Matrix4 pitchRotation = {};
    pitchRotation.values[0][0] = 1.0F;
    pitchRotation.values[1][1] = pitchCosine;
    pitchRotation.values[1][2] = -pitchSine;
    pitchRotation.values[2][1] = pitchSine;
    pitchRotation.values[2][2] = pitchCosine;
    pitchRotation.values[3][3] = 1.0F;

    Matrix4 yawRotation = {};
    yawRotation.values[0][0] = yawCosine;
    yawRotation.values[0][2] = yawSine;
    yawRotation.values[1][1] = 1.0F;
    yawRotation.values[2][0] = -yawSine;
    yawRotation.values[2][2] = yawCosine;
    yawRotation.values[3][3] = 1.0F;

    const Matrix4 recoilRotation = MultiplyMatrices(pitchRotation, yawRotation);
    // A maps the weapon's authored local origin into grip local space. Put the
    // recoil immediately after A, then apply the live grip, so the local point
    // attached to the controller remains at the tracked grip translation.
    const Matrix4 result = MultiplyMatrices(
        MultiplyMatrices(absoluteGripToWeaponAttachment, recoilRotation),
        *gripTransform);
    return IsFinite(result) ? std::optional<Matrix4>(result) : std::nullopt;
}

std::optional<WeaponRecoilAngles> AccumulateD3D8WeaponRecoil(
    const WeaponRecoilAngles& current,
    float pitchImpulse,
    float yawImpulse) noexcept
{
    if (!std::isfinite(current.pitch) || !std::isfinite(current.yaw) ||
        !std::isfinite(pitchImpulse) || !std::isfinite(yawImpulse))
    {
        return std::nullopt;
    }
    const WeaponRecoilAngles result = {
        current.pitch + pitchImpulse,
        current.yaw + yawImpulse};
    return std::isfinite(result.pitch) && std::isfinite(result.yaw)
        ? std::optional<WeaponRecoilAngles>(result)
        : std::nullopt;
}

std::optional<Matrix4> MakeD3D8WorldSpaceWeaponDelta(
    const Matrix4& frameView,
    const Matrix4& frameViewDelta) noexcept
{
    if (!IsFinite(frameView) || !IsFinite(frameViewDelta))
    {
        return std::nullopt;
    }
    const auto inverseFrameView = InvertMatrix(frameView);
    if (!inverseFrameView.has_value())
    {
        return std::nullopt;
    }
    const Matrix4 result = MultiplyMatrices(
        MultiplyMatrices(frameView, frameViewDelta),
        *inverseFrameView);
    return IsFinite(result) ? std::optional<Matrix4>(result) : std::nullopt;
}

std::optional<Matrix4> MakeD3D8CalibrationViewWeaponOffset(
    const Matrix4& frameView,
    const Matrix4& worldSpaceDelta) noexcept
{
    if (!IsFinite(frameView) || !IsFinite(worldSpaceDelta))
    {
        return std::nullopt;
    }
    const auto inverseFrameView = InvertMatrix(frameView);
    if (!inverseFrameView.has_value())
    {
        return std::nullopt;
    }
    const Matrix4 result = MultiplyMatrices(
        MultiplyMatrices(*inverseFrameView, worldSpaceDelta),
        frameView);
    return IsFinite(result) ? std::optional<Matrix4>(result) : std::nullopt;
}

std::optional<Matrix4> MakeD3D8ViewModelPerspectiveCorrection(
    const Matrix4& viewModelProjection,
    const Matrix4& ordinaryWorldProjection) noexcept
{
    constexpr float kProjectionEpsilon = 0.0001F;
    constexpr float kMinimumFocalRatio = 0.25F;
    constexpr float kMaximumFocalRatio = 4.0F;
    if (!IsFinite(viewModelProjection) ||
        !IsFinite(ordinaryWorldProjection) ||
        std::fabs(viewModelProjection.values[2][3]) <= kProjectionEpsilon ||
        std::fabs(viewModelProjection.values[3][3]) > kProjectionEpsilon ||
        std::fabs(ordinaryWorldProjection.values[2][3]) <=
            kProjectionEpsilon ||
        std::fabs(ordinaryWorldProjection.values[3][3]) >
            kProjectionEpsilon ||
        std::fabs(viewModelProjection.values[0][0]) <= kProjectionEpsilon ||
        std::fabs(viewModelProjection.values[1][1]) <= kProjectionEpsilon ||
        std::fabs(ordinaryWorldProjection.values[0][0]) <=
            kProjectionEpsilon ||
        std::fabs(ordinaryWorldProjection.values[1][1]) <=
            kProjectionEpsilon)
    {
        return std::nullopt;
    }

    const float horizontalRatio =
        viewModelProjection.values[0][0] /
        ordinaryWorldProjection.values[0][0];
    const float verticalRatio =
        viewModelProjection.values[1][1] /
        ordinaryWorldProjection.values[1][1];
    if (!IsFinite(horizontalRatio) || !IsFinite(verticalRatio) ||
        horizontalRatio < kMinimumFocalRatio ||
        horizontalRatio > kMaximumFocalRatio ||
        verticalRatio < kMinimumFocalRatio ||
        verticalRatio > kMaximumFocalRatio)
    {
        return std::nullopt;
    }

    // Viewmodels commonly use a different near/far range in addition to a
    // narrower FOV. Perspective correction must not import that depth range:
    // doing so can scale a held object dramatically or make it clip as it
    // approaches the viewer. This follows the established viewmodel
    // correction construction used by DXVK Remix.
    Matrix4 normalizedViewModelProjection = viewModelProjection;
    normalizedViewModelProjection.values[2][2] =
        ordinaryWorldProjection.values[2][2];
    normalizedViewModelProjection.values[2][3] =
        ordinaryWorldProjection.values[2][3];
    normalizedViewModelProjection.values[3][2] =
        ordinaryWorldProjection.values[3][2];
    normalizedViewModelProjection.values[3][3] =
        ordinaryWorldProjection.values[3][3];

    const auto inverseOrdinaryProjection =
        InvertMatrix(ordinaryWorldProjection);
    if (!inverseOrdinaryProjection.has_value())
    {
        return std::nullopt;
    }
    const Matrix4 result = MultiplyMatrices(
        normalizedViewModelProjection,
        *inverseOrdinaryProjection);
    if (!IsFinite(result) ||
        std::fabs(result.values[0][3]) > kProjectionEpsilon ||
        std::fabs(result.values[1][3]) > kProjectionEpsilon ||
        std::fabs(result.values[2][3]) > kProjectionEpsilon ||
        std::fabs(result.values[3][3] - 1.0F) > kProjectionEpsilon)
    {
        return std::nullopt;
    }
    return result;
}

std::optional<Matrix4> ApplyViewSpaceWeaponDeltaToD3D8Wvp(
    const Matrix4& sourceWvp,
    const Matrix4& eyeProjection,
    const Matrix4& viewSpaceDelta) noexcept
{
    if (!IsFinite(sourceWvp) || !IsFinite(eyeProjection) ||
        !IsFinite(viewSpaceDelta))
    {
        return std::nullopt;
    }
    const auto inverseProjection = InvertMatrix(eyeProjection);
    if (!inverseProjection.has_value())
    {
        return std::nullopt;
    }
    const Matrix4 result = MultiplyMatrices(
        MultiplyMatrices(
            MultiplyMatrices(sourceWvp, *inverseProjection),
            viewSpaceDelta),
        eyeProjection);
    return IsFinite(result) ? std::optional<Matrix4>(result) : std::nullopt;
}

std::optional<Matrix4> ApplyViewSpaceWeaponDeltaToD3D8World(
    const Matrix4& sourceWorld,
    const Matrix4& sourceView,
    const Matrix4& viewSpaceDelta) noexcept
{
    if (!IsFinite(sourceWorld) || !IsFinite(sourceView) ||
        !IsFinite(viewSpaceDelta))
    {
        return std::nullopt;
    }
    const auto inverseView = InvertMatrix(sourceView);
    if (!inverseView.has_value())
    {
        return std::nullopt;
    }
    const Matrix4 result = MultiplyMatrices(
        MultiplyMatrices(
            MultiplyMatrices(sourceWorld, sourceView),
            viewSpaceDelta),
        *inverseView);
    return IsFinite(result) ? std::optional<Matrix4>(result) : std::nullopt;
}

std::optional<Matrix4> ApplyWorldSpaceWeaponDeltaToD3D8World(
    const Matrix4& sourceWorld,
    const Matrix4& worldSpaceDelta) noexcept
{
    if (!IsFinite(sourceWorld) || !IsFinite(worldSpaceDelta))
    {
        return std::nullopt;
    }
    const Matrix4 result = MultiplyMatrices(sourceWorld, worldSpaceDelta);
    return IsFinite(result) ? std::optional<Matrix4>(result) : std::nullopt;
}

} // namespace bfvr::stereo
