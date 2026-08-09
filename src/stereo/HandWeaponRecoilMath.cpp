#include "stereo/HandWeaponRecoilMath.h"

#include <cmath>

namespace
{

using bfvr::stereo::Matrix4;

constexpr float kIdentityAngleEpsilonDegrees = 0.0001F;
constexpr float kRigidTolerance = 0.02F;

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

bool IsRigid(const Matrix4& matrix) noexcept
{
    if (!IsFinite(matrix) ||
        std::fabs(matrix.values[0][3]) > 0.001F ||
        std::fabs(matrix.values[1][3]) > 0.001F ||
        std::fabs(matrix.values[2][3]) > 0.001F ||
        std::fabs(matrix.values[3][3] - 1.0F) > 0.001F)
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
        if (std::fabs(lengthSquared - 1.0F) > kRigidTolerance)
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
            if (std::fabs(dot) > kRigidTolerance)
            {
                return false;
            }
        }
    }
    return true;
}

Matrix4 Multiply(const Matrix4& left, const Matrix4& right) noexcept
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

float RecoverAngle(
    const float angle,
    const float elapsedSeconds,
    const float halfLifeSeconds) noexcept
{
    const float decay = std::exp2(-elapsedSeconds / halfLifeSeconds);
    const float result = angle * decay;
    return std::fabs(result) <= kIdentityAngleEpsilonDegrees ? 0.0F : result;
}

} // namespace

namespace bfvr::stereo
{

HandWeaponRecoilStepStatus AccumulateHandWeaponRecoilStep(
    HandWeaponRecoilState& state,
    const std::uint64_t nativeSequence,
    const float pitchStepDegrees,
    const float yawStepDegrees,
    const float pitchScale,
    const float yawScale,
    const float maximumAbsoluteAngleDegrees) noexcept
{
    if (nativeSequence == 0 || !std::isfinite(pitchStepDegrees) ||
        !std::isfinite(yawStepDegrees) || !std::isfinite(pitchScale) ||
        !std::isfinite(yawScale) ||
        !std::isfinite(maximumAbsoluteAngleDegrees) || pitchScale < 0.0F ||
        yawScale < 0.0F || maximumAbsoluteAngleDegrees <= 0.0F)
    {
        return HandWeaponRecoilStepStatus::Rejected;
    }
    if (nativeSequence <= state.lastNativeSequence)
    {
        return HandWeaponRecoilStepStatus::Stale;
    }
    const float pitch =
        state.angles.pitchDegrees + pitchStepDegrees * pitchScale;
    const float yaw = state.angles.yawDegrees + yawStepDegrees * yawScale;
    if (!std::isfinite(pitch) || !std::isfinite(yaw) ||
        std::fabs(pitch) > maximumAbsoluteAngleDegrees ||
        std::fabs(yaw) > maximumAbsoluteAngleDegrees)
    {
        return HandWeaponRecoilStepStatus::Rejected;
    }
    state.angles = {pitch, yaw};
    state.lastNativeSequence = nativeSequence;
    return HandWeaponRecoilStepStatus::Applied;
}

bool RecoverHandWeaponRecoilToIdentity(
    HandWeaponRecoilState& state,
    const float elapsedSeconds,
    const float pitchHalfLifeSeconds,
    const float yawHalfLifeSeconds) noexcept
{
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0F ||
        !std::isfinite(pitchHalfLifeSeconds) ||
        !std::isfinite(yawHalfLifeSeconds) || pitchHalfLifeSeconds <= 0.0F ||
        yawHalfLifeSeconds <= 0.0F ||
        !std::isfinite(state.angles.pitchDegrees) ||
        !std::isfinite(state.angles.yawDegrees))
    {
        return false;
    }
    state.angles.pitchDegrees = RecoverAngle(
        state.angles.pitchDegrees,
        elapsedSeconds,
        pitchHalfLifeSeconds);
    state.angles.yawDegrees = RecoverAngle(
        state.angles.yawDegrees,
        elapsedSeconds,
        yawHalfLifeSeconds);
    return true;
}

std::optional<Matrix4> ApplyHandWeaponRecoilToGunPose(
    const Matrix4& gunWorld,
    const HandWeaponRecoilAngles& recoil) noexcept
{
    if (!IsRigid(gunWorld) || !std::isfinite(recoil.pitchDegrees) ||
        !std::isfinite(recoil.yawDegrees))
    {
        return std::nullopt;
    }

    constexpr float kDegreesToRadians = 0.01745329251994329577F;
    const float pitch = -recoil.pitchDegrees * kDegreesToRadians;
    const float yaw = -recoil.yawDegrees * kDegreesToRadians;
    const float pitchCosine = std::cos(pitch);
    const float pitchSine = std::sin(pitch);
    const float yawCosine = std::cos(yaw);
    const float yawSine = std::sin(yaw);
    if (!std::isfinite(pitchCosine) || !std::isfinite(pitchSine) ||
        !std::isfinite(yawCosine) || !std::isfinite(yawSine))
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

    // Row-vector hierarchy: local recoil precedes the complete live gun world
    // pose. A zero-translation recoil matrix therefore rotates the barrel in
    // its own frame while retaining the exact controller-owned world origin.
    const Matrix4 result = Multiply(
        Multiply(pitchRotation, yawRotation),
        gunWorld);
    return IsRigid(result) ? std::optional<Matrix4>(result) : std::nullopt;
}

bool IsHandWeaponRecoilAtIdentity(
    const HandWeaponRecoilState& state) noexcept
{
    return std::isfinite(state.angles.pitchDegrees) &&
        std::isfinite(state.angles.yawDegrees) &&
        std::fabs(state.angles.pitchDegrees) <=
            kIdentityAngleEpsilonDegrees &&
        std::fabs(state.angles.yawDegrees) <=
            kIdentityAngleEpsilonDegrees;
}

} // namespace bfvr::stereo
