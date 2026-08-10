#include "stereo/ScopeViewMath.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
using bfvr::stereo::Matrix4;
using bfvr::stereo::Quaternion;

constexpr float kPi = 3.14159265358979323846F;
constexpr float kAffineTolerance = 0.001F;
constexpr float kOrthonormalTolerance = 0.02F;

bool IsFinite(const Matrix4& matrix) noexcept
{
    for (const auto& row : matrix.values)
    {
        for (const float value : row)
        {
            if (!std::isfinite(value))
            {
                return false;
            }
        }
    }
    return true;
}

bool IsRigidAffine(const Matrix4& matrix) noexcept
{
    if (!IsFinite(matrix) ||
        std::fabs(matrix.values[0][3]) > kAffineTolerance ||
        std::fabs(matrix.values[1][3]) > kAffineTolerance ||
        std::fabs(matrix.values[2][3]) > kAffineTolerance ||
        std::fabs(matrix.values[3][3] - 1.0F) > kAffineTolerance)
    {
        return false;
    }

    for (std::size_t row = 0; row < 3; ++row)
    {
        float lengthSquared = 0.0F;
        for (std::size_t column = 0; column < 3; ++column)
        {
            lengthSquared +=
                matrix.values[row][column] * matrix.values[row][column];
        }
        if (std::fabs(lengthSquared - 1.0F) > kOrthonormalTolerance)
        {
            return false;
        }
        for (std::size_t other = row + 1; other < 3; ++other)
        {
            float dot = 0.0F;
            for (std::size_t column = 0; column < 3; ++column)
            {
                dot += matrix.values[row][column] *
                    matrix.values[other][column];
            }
            if (std::fabs(dot) > kOrthonormalTolerance)
            {
                return false;
            }
        }
    }

    const float determinant =
        matrix.values[0][0] *
            (matrix.values[1][1] * matrix.values[2][2] -
             matrix.values[1][2] * matrix.values[2][1]) -
        matrix.values[0][1] *
            (matrix.values[1][0] * matrix.values[2][2] -
             matrix.values[1][2] * matrix.values[2][0]) +
        matrix.values[0][2] *
            (matrix.values[1][0] * matrix.values[2][1] -
             matrix.values[1][1] * matrix.values[2][0]);
    return std::isfinite(determinant) && determinant > 0.98F &&
        determinant < 1.02F;
}

struct BasisVector
{
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

float Dot(const BasisVector& left, const BasisVector& right) noexcept
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

BasisVector Cross(
    const BasisVector& left,
    const BasisVector& right) noexcept
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x};
}

bool Normalize(BasisVector& value) noexcept
{
    const float lengthSquared = Dot(value, value);
    if (!std::isfinite(lengthSquared) || lengthSquared < 0.000001F)
    {
        return false;
    }
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    value.x *= inverseLength;
    value.y *= inverseLength;
    value.z *= inverseLength;
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

std::optional<Quaternion> NormalizeQuaternion(
    const Quaternion& value) noexcept
{
    const float lengthSquared = value.x * value.x + value.y * value.y +
        value.z * value.z + value.w * value.w;
    if (!std::isfinite(lengthSquared) || lengthSquared < 0.000001F)
    {
        return std::nullopt;
    }
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    const Quaternion result = {
        value.x * inverseLength,
        value.y * inverseLength,
        value.z * inverseLength,
        value.w * inverseLength};
    return std::isfinite(result.x) && std::isfinite(result.y) &&
            std::isfinite(result.z) && std::isfinite(result.w)
        ? std::optional<Quaternion>(result)
        : std::nullopt;
}

std::optional<Quaternion> QuaternionFromMatrix(
    const Matrix4& matrix) noexcept
{
    if (!IsRigidAffine(matrix))
    {
        return std::nullopt;
    }
    const float m00 = matrix.values[0][0];
    const float m01 = matrix.values[1][0];
    const float m02 = matrix.values[2][0];
    const float m10 = matrix.values[0][1];
    const float m11 = matrix.values[1][1];
    const float m12 = matrix.values[2][1];
    const float m20 = matrix.values[0][2];
    const float m21 = matrix.values[1][2];
    const float m22 = matrix.values[2][2];
    const float trace = m00 + m11 + m22;
    Quaternion result = {};
    if (trace > 0.0F)
    {
        const float scale = std::sqrt(trace + 1.0F) * 2.0F;
        result = {
            (m21 - m12) / scale,
            (m02 - m20) / scale,
            (m10 - m01) / scale,
            0.25F * scale};
    }
    else if (m00 > m11 && m00 > m22)
    {
        const float scale = std::sqrt(1.0F + m00 - m11 - m22) * 2.0F;
        result = {
            0.25F * scale,
            (m01 + m10) / scale,
            (m02 + m20) / scale,
            (m21 - m12) / scale};
    }
    else if (m11 > m22)
    {
        const float scale = std::sqrt(1.0F + m11 - m00 - m22) * 2.0F;
        result = {
            (m01 + m10) / scale,
            0.25F * scale,
            (m12 + m21) / scale,
            (m02 - m20) / scale};
    }
    else
    {
        const float scale = std::sqrt(1.0F + m22 - m00 - m11) * 2.0F;
        result = {
            (m02 + m20) / scale,
            (m12 + m21) / scale,
            0.25F * scale,
            (m10 - m01) / scale};
    }
    return NormalizeQuaternion(result);
}

Quaternion SlerpQuaternion(
    const Quaternion& source,
    Quaternion target,
    const float amount) noexcept
{
    float dot = source.x * target.x + source.y * target.y +
        source.z * target.z + source.w * target.w;
    if (dot < 0.0F)
    {
        target = {-target.x, -target.y, -target.z, -target.w};
        dot = -dot;
    }
    dot = std::clamp(dot, -1.0F, 1.0F);
    const float clampedAmount = std::clamp(amount, 0.0F, 1.0F);
    if (dot > 0.9995F)
    {
        return NormalizeQuaternion({
            source.x + (target.x - source.x) * clampedAmount,
            source.y + (target.y - source.y) * clampedAmount,
            source.z + (target.z - source.z) * clampedAmount,
            source.w + (target.w - source.w) * clampedAmount})
            .value_or(source);
    }
    const float angle = std::acos(dot);
    const float sine = std::sin(angle);
    if (sine < 0.000001F)
    {
        return source;
    }
    const float sourceWeight =
        std::sin((1.0F - clampedAmount) * angle) / sine;
    const float targetWeight = std::sin(clampedAmount * angle) / sine;
    return NormalizeQuaternion({
        source.x * sourceWeight + target.x * targetWeight,
        source.y * sourceWeight + target.y * targetWeight,
        source.z * sourceWeight + target.z * targetWeight,
        source.w * sourceWeight + target.w * targetWeight})
        .value_or(source);
}

float QuaternionAngle(
    const Quaternion& left,
    const Quaternion& right) noexcept
{
    const Quaternion relative = {
        left.w * right.x - left.x * right.w - left.y * right.z +
            left.z * right.y,
        left.w * right.y + left.x * right.z - left.y * right.w -
            left.z * right.x,
        left.w * right.z - left.x * right.y + left.y * right.x -
            left.z * right.w,
        left.w * right.w + left.x * right.x + left.y * right.y +
            left.z * right.z};
    const float vectorLength = std::sqrt(
        relative.x * relative.x + relative.y * relative.y +
        relative.z * relative.z);
    return 2.0F * std::atan2(
        vectorLength,
        std::fabs(relative.w));
}

BasisVector RotateBasisVector(
    const Quaternion& orientation,
    const BasisVector& value) noexcept
{
    const BasisVector axis = {
        orientation.x, orientation.y, orientation.z};
    const BasisVector doubledCross = {
        2.0F * (axis.y * value.z - axis.z * value.y),
        2.0F * (axis.z * value.x - axis.x * value.z),
        2.0F * (axis.x * value.y - axis.y * value.x)};
    const BasisVector axisCross = Cross(axis, doubledCross);
    return {
        value.x + orientation.w * doubledCross.x + axisCross.x,
        value.y + orientation.w * doubledCross.y + axisCross.y,
        value.z + orientation.w * doubledCross.z + axisCross.z};
}

std::optional<Matrix4> SlerpScopeAimRotation(
    const Matrix4& previous,
    const Matrix4& current,
    const float currentWeight) noexcept
{
    if (!IsRigidAffine(previous) || !IsRigidAffine(current) ||
        !std::isfinite(currentWeight) || currentWeight < 0.0F ||
        currentWeight > 1.0F)
    {
        return std::nullopt;
    }
    const auto previousQuaternion = QuaternionFromMatrix(previous);
    const auto currentQuaternion = QuaternionFromMatrix(current);
    if (!previousQuaternion.has_value() || !currentQuaternion.has_value())
    {
        return std::nullopt;
    }
    const Quaternion filtered = SlerpQuaternion(
        *previousQuaternion,
        *currentQuaternion,
        currentWeight);
    const BasisVector right = RotateBasisVector(filtered, {1.0F, 0.0F, 0.0F});
    const BasisVector up = RotateBasisVector(filtered, {0.0F, 1.0F, 0.0F});
    const BasisVector forward = RotateBasisVector(filtered, {0.0F, 0.0F, 1.0F});
    Matrix4 result = current;
    result.values[0][0] = right.x;
    result.values[0][1] = right.y;
    result.values[0][2] = right.z;
    result.values[1][0] = up.x;
    result.values[1][1] = up.y;
    result.values[1][2] = up.z;
    result.values[2][0] = forward.x;
    result.values[2][1] = forward.y;
    result.values[2][2] = forward.z;
    return IsRigidAffine(result)
        ? std::optional<Matrix4>(result)
        : std::nullopt;
}

std::optional<float> RotationAngle(
    const Matrix4& previous,
    const Matrix4& current) noexcept
{
    const auto previousQuaternion = QuaternionFromMatrix(previous);
    const auto currentQuaternion = QuaternionFromMatrix(current);
    if (!previousQuaternion.has_value() || !currentQuaternion.has_value())
    {
        return std::nullopt;
    }
    return QuaternionAngle(*previousQuaternion, *currentQuaternion);
}

float CalculateScopeAimStabilizedLerp(
    const float normalizedDistance,
    const float elapsedSeconds) noexcept
{
    if (normalizedDistance >= 1.0F)
    {
        return 1.0F;
    }
    if (normalizedDistance <= 0.0F || elapsedSeconds <= 0.0F)
    {
        return 0.0F;
    }
    constexpr float kNinetyHertzSliceSeconds = 1.0F / 90.0F;
    const float secondSliceLerp = normalizedDistance -
        normalizedDistance * normalizedDistance;
    const float thirdSliceLerp = secondSliceLerp * secondSliceLerp;
    const float sliceCount = elapsedSeconds / kNinetyHertzSliceSeconds;
    const float firstSlice = std::clamp(sliceCount, 0.0F, 1.0F);
    const float secondSlice = std::clamp(sliceCount - 1.0F, 0.0F, 1.0F);
    const float thirdSlice = std::clamp(sliceCount - 2.0F, 0.0F, 1.0F);
    return std::clamp(
        normalizedDistance * firstSlice +
            secondSliceLerp * secondSlice + thirdSliceLerp * thirdSlice,
        0.0F,
        1.0F);
}

Matrix4 MultiplyMatrices(
    const Matrix4& left,
    const Matrix4& right) noexcept
{
    Matrix4 result = {};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            for (std::size_t inner = 0; inner < 4; ++inner)
            {
                result.values[row][column] +=
                    left.values[row][inner] * right.values[inner][column];
            }
        }
    }
    return result;
}

std::optional<Matrix4> InvertRigidAffine(const Matrix4& matrix) noexcept
{
    if (!IsRigidAffine(matrix))
    {
        return std::nullopt;
    }
    Matrix4 inverse = {};
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            inverse.values[row][column] = matrix.values[column][row];
        }
    }
    for (std::size_t column = 0; column < 3; ++column)
    {
        inverse.values[3][column] = -(
            matrix.values[3][0] * inverse.values[0][column] +
            matrix.values[3][1] * inverse.values[1][column] +
            matrix.values[3][2] * inverse.values[2][column]);
    }
    inverse.values[3][3] = 1.0F;
    return IsRigidAffine(inverse)
        ? std::optional<Matrix4>(inverse)
        : std::nullopt;
}
} // namespace

namespace bfvr::stereo
{

std::optional<Matrix4> UpdateD3D8ScopeAimSmoothing(
    ScopeAimSmoothingState& state,
    const Matrix4& currentGunWorld,
    const void* const weapon,
    const void* const soldier,
    const std::int32_t controllerGeneration,
    const std::int64_t predictedDisplayTime,
    const bool enabled) noexcept
{
    if (!IsRigidAffine(currentGunWorld))
    {
        ResetD3D8ScopeAimSmoothing(state);
        return std::nullopt;
    }
    if (!enabled || weapon == nullptr || soldier == nullptr ||
        controllerGeneration <= 0 || predictedDisplayTime <= 0)
    {
        ResetD3D8ScopeAimSmoothing(state);
        return currentGunWorld;
    }

    const bool sameLifetime = state.valid && state.weapon == weapon &&
        state.soldier == soldier;
    if (sameLifetime &&
        state.controllerGeneration == controllerGeneration)
    {
        return state.filteredGunWorld;
    }

    Matrix4 filtered = currentGunWorld;
    const std::int64_t elapsedNanoseconds = sameLifetime
        ? predictedDisplayTime - state.predictedDisplayTime
        : 0;
    const bool continuousSample = sameLifetime &&
        elapsedNanoseconds > 0 &&
        elapsedNanoseconds <=
            kScopeAimSmoothingMaximumSampleIntervalNanoseconds;
    if (continuousSample)
    {
        const auto angularError = RotationAngle(
            state.filteredGunWorld,
            currentGunWorld);
        if (!angularError.has_value())
        {
            ResetD3D8ScopeAimSmoothing(state);
            return std::nullopt;
        }
        if (*angularError < kScopeAimSmoothingMaximumErrorRadians)
        {
            const float elapsedSeconds =
                static_cast<float>(elapsedNanoseconds) * 0.000000001F;
            const float currentWeight = CalculateScopeAimStabilizedLerp(
                *angularError / kScopeAimSmoothingMaximumErrorRadians,
                elapsedSeconds);
            const auto stabilized = SlerpScopeAimRotation(
                state.filteredGunWorld,
                currentGunWorld,
                currentWeight);
            if (!stabilized.has_value())
            {
                ResetD3D8ScopeAimSmoothing(state);
                return std::nullopt;
            }
            filtered = *stabilized;
        }
    }
    state.filteredGunWorld = filtered;
    state.weapon = weapon;
    state.soldier = soldier;
    state.predictedDisplayTime = predictedDisplayTime;
    state.controllerGeneration = controllerGeneration;
    state.valid = true;
    return filtered;
}

void ResetD3D8ScopeAimSmoothing(
    ScopeAimSmoothingState& state) noexcept
{
    state = {};
}

bool ShouldAwaitD3D8NativeScopeDecisionAfterAcceptedShot(
    const void* const requestedWeapon,
    const void* const ownedWeapon,
    const void* const ownedSoldier,
    const bool ownedScopeEnabled,
    const void* const shotWeapon,
    const void* const shotSoldier) noexcept
{
    return requestedWeapon != nullptr && ownedWeapon != nullptr &&
        ownedSoldier != nullptr && ownedScopeEnabled &&
        requestedWeapon == ownedWeapon && ownedWeapon == shotWeapon &&
        ownedSoldier == shotSoldier;
}

bool ShouldReleaseD3D8OwnedScopeForNativePostShotState(
    const bool nativeDecisionAwaited,
    const bool ownedScopeEnabled,
    const bool nativeZoomEnabled) noexcept
{
    return nativeDecisionAwaited && ownedScopeEnabled && !nativeZoomEnabled;
}

bool ShouldReleaseD3D8ScopeForPlayerLifecycle(
    const bool localPlayerStateReadable,
    const bool localPlayerAlive,
    const bool scopeLifetimeActive) noexcept
{
    return localPlayerStateReadable && !localPlayerAlive &&
        scopeLifetimeActive;
}

ScopeAimSource SelectScopeAimSource(
    bool scopeRequested,
    bool freshPoseMatchesRequestedWeapon,
    bool freshPoseContradictsRequestedWeapon,
    bool retainOwnedLifetimeThroughPoseContradiction,
    bool trackedPoseAvailable,
    bool latchedPoseAvailable) noexcept
{
    if (!scopeRequested)
    {
        return ScopeAimSource::None;
    }
    if (freshPoseContradictsRequestedWeapon &&
        !retainOwnedLifetimeThroughPoseContradiction)
    {
        return ScopeAimSource::None;
    }
    // Once an exact scope has established its native-to-controller
    // correction, keep the continuously tracked basis authoritative. BF1942
    // can intermittently republish its otherwise hidden 1P arm while scoped;
    // preferring that asynchronous cache for one frame and then returning to
    // tracked aim produces a visible magnified camera hitch.
    if (trackedPoseAvailable)
    {
        return ScopeAimSource::Tracked;
    }
    if (freshPoseMatchesRequestedWeapon)
    {
        return ScopeAimSource::Fresh;
    }
    return latchedPoseAvailable
        ? ScopeAimSource::Latched
        : ScopeAimSource::None;
}

bool IsExactScopeFirePoseEligible(
    const bool scopeFrameAvailable,
    const void* const fireWeapon,
    const void* const scopeWeapon,
    const void* const currentSoldier,
    const void* const scopeSoldier) noexcept
{
    return scopeFrameAvailable && fireWeapon != nullptr &&
        fireWeapon == scopeWeapon && currentSoldier != nullptr &&
        currentSoldier == scopeSoldier;
}

std::optional<Matrix4> MakeD3D8ScopeAimCorrection(
    const Matrix4& authoritativeGunWorld,
    const Matrix4& trackedGunWorld) noexcept
{
    if (!IsRigidAffine(authoritativeGunWorld))
    {
        return std::nullopt;
    }
    const auto inverseTracked = InvertRigidAffine(trackedGunWorld);
    if (!inverseTracked.has_value())
    {
        return std::nullopt;
    }
    const Matrix4 correction = MultiplyMatrices(
        authoritativeGunWorld,
        *inverseTracked);
    return IsRigidAffine(correction)
        ? std::optional<Matrix4>(correction)
        : std::nullopt;
}

std::optional<Matrix4> ApplyD3D8ScopeAimCorrection(
    const Matrix4& correction,
    const Matrix4& trackedGunWorld) noexcept
{
    if (!IsRigidAffine(correction) || !IsRigidAffine(trackedGunWorld))
    {
        return std::nullopt;
    }
    const Matrix4 corrected = MultiplyMatrices(correction, trackedGunWorld);
    return IsRigidAffine(corrected)
        ? std::optional<Matrix4>(corrected)
        : std::nullopt;
}

bool IsD3D8ScopeOffHandSupportHeld(
    const bool bindingEstablished,
    const bool sessionFocused,
    const bool leftGripTracked,
    const bool leftSqueezeActive,
    const float leftSqueezeValue) noexcept
{
    constexpr float kSqueezeReleaseThreshold = 0.45F;
    if (!bindingEstablished || !sessionFocused || !leftGripTracked ||
        !leftSqueezeActive || !std::isfinite(leftSqueezeValue) ||
        leftSqueezeValue < kSqueezeReleaseThreshold)
    {
        return false;
    }
    return true;
}

std::optional<float> ComputeD3D8ScopeProjectionScale(
    float normalFovRadians,
    float scopeFovRadians) noexcept
{
    if (!std::isfinite(normalFovRadians) ||
        !std::isfinite(scopeFovRadians) || normalFovRadians <= 0.0F ||
        scopeFovRadians <= 0.0F || normalFovRadians >= kPi ||
        scopeFovRadians >= kPi)
    {
        return std::nullopt;
    }
    const float normalTangent = std::tan(normalFovRadians * 0.5F);
    const float scopeTangent = std::tan(scopeFovRadians * 0.5F);
    const float scale = normalTangent / scopeTangent;
    return std::isfinite(scale) && scale > 0.0F
        ? std::optional<float>(scale)
        : std::nullopt;
}

std::optional<Matrix4> MakeD3D8WeaponDirectedScopeCamera(
    const Matrix4& headAdjustedCameraWorld,
    const Matrix4& controllerGunWorld) noexcept
{
    if (!IsRigidAffine(headAdjustedCameraWorld) ||
        !IsRigidAffine(controllerGunWorld))
    {
        return std::nullopt;
    }

    Matrix4 result = headAdjustedCameraWorld;
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            result.values[row][column] =
                controllerGunWorld.values[row][column];
        }
    }
    return IsRigidAffine(result)
        ? std::optional<Matrix4>(result)
        : std::nullopt;
}

bool ApplyD3D8ScopeProjectionScale(
    Matrix4& projection,
    float projectionScale) noexcept
{
    if (!IsFinite(projection) || !std::isfinite(projectionScale) ||
        projectionScale <= 0.0F)
    {
        return false;
    }
    const float scaledX =
        projection.values[0][0] * projectionScale;
    const float scaledY =
        projection.values[1][1] * projectionScale;
    if (!std::isfinite(scaledX) || !std::isfinite(scaledY) ||
        scaledX == 0.0F || scaledY == 0.0F)
    {
        return false;
    }
    projection.values[0][0] = scaledX;
    projection.values[1][1] = scaledY;
    return true;
}

std::optional<ScopeOverlayQuadSize>
ComputeEyeFillingScopeOverlayQuadSize(
    const ScopeOverlayFov& fov,
    float distanceMeters,
    float overscanScale) noexcept
{
    constexpr float kHalfPi = kPi * 0.5F;
    const std::array<float, 4> angles = {
        fov.angleLeft,
        fov.angleRight,
        fov.angleUp,
        fov.angleDown};
    if (!std::isfinite(distanceMeters) || distanceMeters <= 0.0F ||
        !std::isfinite(overscanScale) || overscanScale < 1.0F)
    {
        return std::nullopt;
    }
    for (const float angle : angles)
    {
        if (!std::isfinite(angle) || std::fabs(angle) >= kHalfPi)
        {
            return std::nullopt;
        }
    }

    const float horizontalTangent = std::max(
        std::fabs(std::tan(fov.angleLeft)),
        std::fabs(std::tan(fov.angleRight)));
    const float verticalTangent = std::max(
        std::fabs(std::tan(fov.angleUp)),
        std::fabs(std::tan(fov.angleDown)));
    ScopeOverlayQuadSize result = {
        2.0F * distanceMeters * horizontalTangent * overscanScale,
        2.0F * distanceMeters * verticalTangent * overscanScale};
    return std::isfinite(result.widthMeters) &&
        std::isfinite(result.heightMeters) && result.widthMeters > 0.0F &&
        result.heightMeters > 0.0F
        ? std::optional<ScopeOverlayQuadSize>(result)
        : std::nullopt;
}

std::optional<ScopeOverlayQuad> MakeEyeFillingScopeOverlayQuad(
    const Pose& eyePose,
    const ScopeOverlayFov& fov,
    float distanceMeters,
    float overscanScale) noexcept
{
    if (!std::isfinite(eyePose.position.x) ||
        !std::isfinite(eyePose.position.y) ||
        !std::isfinite(eyePose.position.z))
    {
        return std::nullopt;
    }
    const float lengthSquared =
        eyePose.orientation.x * eyePose.orientation.x +
        eyePose.orientation.y * eyePose.orientation.y +
        eyePose.orientation.z * eyePose.orientation.z +
        eyePose.orientation.w * eyePose.orientation.w;
    if (!std::isfinite(lengthSquared) || lengthSquared < 0.25F ||
        lengthSquared > 2.25F)
    {
        return std::nullopt;
    }
    const auto size = ComputeEyeFillingScopeOverlayQuadSize(
        fov,
        distanceMeters,
        overscanScale);
    if (!size.has_value())
    {
        return std::nullopt;
    }

    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    const Quaternion orientation = {
        eyePose.orientation.x * inverseLength,
        eyePose.orientation.y * inverseLength,
        eyePose.orientation.z * inverseLength,
        eyePose.orientation.w * inverseLength};
    const Vec3 axis = {
        orientation.x,
        orientation.y,
        orientation.z};
    const Vec3 forward = {0.0F, 0.0F, -distanceMeters};
    const Vec3 cross = {
        axis.y * forward.z - axis.z * forward.y,
        axis.z * forward.x - axis.x * forward.z,
        axis.x * forward.y - axis.y * forward.x};
    const Vec3 doubledCross = {
        cross.x * 2.0F,
        cross.y * 2.0F,
        cross.z * 2.0F};
    const Vec3 secondCross = {
        axis.y * doubledCross.z - axis.z * doubledCross.y,
        axis.z * doubledCross.x - axis.x * doubledCross.z,
        axis.x * doubledCross.y - axis.y * doubledCross.x};
    const Vec3 offset = {
        forward.x + doubledCross.x * orientation.w + secondCross.x,
        forward.y + doubledCross.y * orientation.w + secondCross.y,
        forward.z + doubledCross.z * orientation.w + secondCross.z};
    ScopeOverlayQuad result = {};
    result.pose.position = {
        eyePose.position.x + offset.x,
        eyePose.position.y + offset.y,
        eyePose.position.z + offset.z};
    result.pose.orientation = orientation;
    result.widthMeters = size->widthMeters;
    result.heightMeters = size->heightMeters;
    return std::isfinite(result.pose.position.x) &&
            std::isfinite(result.pose.position.y) &&
            std::isfinite(result.pose.position.z)
        ? std::optional<ScopeOverlayQuad>(result)
        : std::nullopt;
}

} // namespace bfvr::stereo
