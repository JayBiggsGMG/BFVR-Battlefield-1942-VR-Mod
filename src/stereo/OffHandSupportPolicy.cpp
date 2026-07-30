#include "stereo/OffHandSupportPolicy.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace bfvr::stereo
{

namespace
{

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

Matrix4 Multiply(
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
                    left.values[row][inner] *
                    right.values[inner][column];
            }
        }
    }
    return result;
}

std::optional<Matrix4> Invert(
    const Matrix4& matrix) noexcept
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
            augmented[row][column + 4] =
                row == column ? 1.0F : 0.0F;
        }
    }

    constexpr float kPivotEpsilon = 0.000001F;
    for (std::size_t column = 0; column < 4; ++column)
    {
        std::size_t pivotRow = column;
        for (std::size_t candidate = column + 1;
             candidate < 4;
             ++candidate)
        {
            if (std::fabs(augmented[candidate][column]) >
                std::fabs(augmented[pivotRow][column]))
            {
                pivotRow = candidate;
            }
        }
        const float pivot = augmented[pivotRow][column];
        if (!std::isfinite(pivot) ||
            std::fabs(pivot) <= kPivotEpsilon)
        {
            return std::nullopt;
        }
        if (pivotRow != column)
        {
            std::swap(
                augmented[pivotRow],
                augmented[column]);
        }
        for (float& value : augmented[column])
        {
            value /= pivot;
        }
        for (std::size_t row = 0; row < 4; ++row)
        {
            if (row == column)
            {
                continue;
            }
            const float factor = augmented[row][column];
            for (std::size_t index = 0;
                 index < augmented[row].size();
                 ++index)
            {
                augmented[row][index] -=
                    factor * augmented[column][index];
            }
        }
    }

    Matrix4 inverse = {};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            inverse.values[row][column] =
                augmented[row][column + 4];
        }
    }
    return IsFinite(inverse)
        ? std::optional<Matrix4>(inverse)
        : std::nullopt;
}

std::optional<OffHandVisualSupportPose> MakePose(
    const Matrix4& targetLocal,
    const Matrix4& controllerLeftHandLocal,
    const float worldUnitsPerMetre) noexcept
{
    if (!IsFinite(targetLocal) ||
        !IsFinite(controllerLeftHandLocal) ||
        !std::isfinite(worldUnitsPerMetre) ||
        worldUnitsPerMetre <= 0.0F)
    {
        return std::nullopt;
    }
    const float x =
        targetLocal.values[3][0] -
        controllerLeftHandLocal.values[3][0];
    const float y =
        targetLocal.values[3][1] -
        controllerLeftHandLocal.values[3][1];
    const float z =
        targetLocal.values[3][2] -
        controllerLeftHandLocal.values[3][2];
    const float distance =
        std::sqrt(x * x + y * y + z * z) /
        worldUnitsPerMetre;
    return std::isfinite(distance)
        ? std::optional<OffHandVisualSupportPose>(
              OffHandVisualSupportPose{
                  targetLocal,
                  distance})
        : std::nullopt;
}

} // namespace

std::optional<OffHandVisualSupportPose>
ComputeOffHandAuthoredSupportPose(
    const Matrix4& leftHandFromRightHand,
    const Matrix4& controllerRightHandWorld,
    const Matrix4& inverseSoldierWorld,
    const Matrix4& controllerLeftHandLocal,
    const float worldUnitsPerMetre) noexcept
{
    if (!IsFinite(leftHandFromRightHand) ||
        !IsFinite(controllerRightHandWorld) ||
        !IsFinite(inverseSoldierWorld) ||
        !IsFinite(controllerLeftHandLocal))
    {
        return std::nullopt;
    }

    const Matrix4 predictedWorld =
        Multiply(
            leftHandFromRightHand,
            controllerRightHandWorld);
    const Matrix4 predictedLocal =
        Multiply(predictedWorld, inverseSoldierWorld);
    return MakePose(
        predictedLocal,
        controllerLeftHandLocal,
        worldUnitsPerMetre);
}

std::optional<OffHandVisualSupportPose>
ComputeOffHandCloseSupportCandidate(
    const Matrix4& controllerRightHandWorld,
    const Matrix4& inverseSoldierWorld,
    const Matrix4& controllerLeftHandLocal,
    const float worldUnitsPerMetre) noexcept
{
    if (!IsFinite(controllerRightHandWorld) ||
        !IsFinite(inverseSoldierWorld))
    {
        return std::nullopt;
    }
    const Matrix4 rightHandLocal =
        Multiply(
            controllerRightHandWorld,
            inverseSoldierWorld);
    return MakePose(
        controllerLeftHandLocal,
        rightHandLocal,
        worldUnitsPerMetre);
}

std::optional<Matrix4> CaptureOffHandCloseRelation(
    const Matrix4& controllerRightHandWorld,
    const Matrix4& inverseSoldierWorld,
    const Matrix4& controllerLeftHandLocal) noexcept
{
    if (!IsFinite(controllerRightHandWorld) ||
        !IsFinite(inverseSoldierWorld) ||
        !IsFinite(controllerLeftHandLocal))
    {
        return std::nullopt;
    }
    const Matrix4 rightHandLocal =
        Multiply(
            controllerRightHandWorld,
            inverseSoldierWorld);
    const auto inverseRightHandLocal =
        Invert(rightHandLocal);
    if (!inverseRightHandLocal.has_value())
    {
        return std::nullopt;
    }
    const Matrix4 relation =
        Multiply(
            controllerLeftHandLocal,
            *inverseRightHandLocal);
    return IsFinite(relation)
        ? std::optional<Matrix4>(relation)
        : std::nullopt;
}

std::optional<OffHandVisualSupportPose>
ComputeOffHandCapturedCloseSupportPose(
    const Matrix4& leftHandFromRightHandLocal,
    const Matrix4& controllerRightHandWorld,
    const Matrix4& inverseSoldierWorld,
    const Matrix4& controllerLeftHandLocal,
    const float worldUnitsPerMetre) noexcept
{
    if (!IsFinite(leftHandFromRightHandLocal) ||
        !IsFinite(controllerRightHandWorld) ||
        !IsFinite(inverseSoldierWorld))
    {
        return std::nullopt;
    }
    const Matrix4 rightHandLocal =
        Multiply(
            controllerRightHandWorld,
            inverseSoldierWorld);
    const Matrix4 predictedLocal =
        Multiply(
            leftHandFromRightHandLocal,
            rightHandLocal);
    return MakePose(
        predictedLocal,
        controllerLeftHandLocal,
        worldUnitsPerMetre);
}

OffHandSupportPolicy::OffHandSupportPolicy(
    OffHandSupportConfiguration configuration) noexcept
    : configuration_(configuration)
{
    configuration_.acquireDistanceMetres =
        std::max(0.0F, configuration_.acquireDistanceMetres);
    configuration_.releaseDistanceMetres =
        std::max(
            configuration_.acquireDistanceMetres,
            configuration_.releaseDistanceMetres);
    configuration_.candidateHoldSeconds =
        std::max(0.0, configuration_.candidateHoldSeconds);
}

OffHandSupportResult OffHandSupportPolicy::Update(
    const OffHandSupportSample& sample) noexcept
{
    const OffHandSupportState previousState = state_;
    const bool validSample =
        sample.bindingId != 0 &&
        std::isfinite(sample.timeSeconds) &&
        std::isfinite(sample.supportDistanceMetres) &&
        sample.supportDistanceMetres >= 0.0F &&
        sample.sessionFocused &&
        sample.leftGripTracked &&
        sample.leftGripHeld &&
        sample.supportPoseValid &&
        !sample.nativeLeftHandTargetActive;
    if (!validSample)
    {
        SetFree();
        return {
            state_,
            false,
            previousState == OffHandSupportState::Supported};
    }

    if (bindingId_ != sample.bindingId)
    {
        SetFree();
        bindingId_ = sample.bindingId;
    }

    switch (state_)
    {
    case OffHandSupportState::Free:
        if (sample.supportDistanceMetres <=
            configuration_.acquireDistanceMetres)
        {
            state_ = OffHandSupportState::Candidate;
            candidateStartSeconds_ = sample.timeSeconds;
        }
        break;

    case OffHandSupportState::Candidate:
        if (sample.supportDistanceMetres >
            configuration_.acquireDistanceMetres)
        {
            SetFree();
            bindingId_ = sample.bindingId;
        }
        else if (sample.timeSeconds < candidateStartSeconds_)
        {
            candidateStartSeconds_ = sample.timeSeconds;
        }
        else if (
            sample.timeSeconds - candidateStartSeconds_ >=
            configuration_.candidateHoldSeconds)
        {
            state_ = OffHandSupportState::Supported;
        }
        break;

    case OffHandSupportState::Supported:
        if (sample.supportDistanceMetres >
            configuration_.releaseDistanceMetres)
        {
            SetFree();
            bindingId_ = sample.bindingId;
        }
        break;
    }

    return {
        state_,
        previousState != OffHandSupportState::Supported &&
            state_ == OffHandSupportState::Supported,
        previousState == OffHandSupportState::Supported &&
            state_ != OffHandSupportState::Supported};
}

void OffHandSupportPolicy::Reset() noexcept
{
    SetFree();
}

OffHandSupportState OffHandSupportPolicy::State() const noexcept
{
    return state_;
}

void OffHandSupportPolicy::SetFree() noexcept
{
    state_ = OffHandSupportState::Free;
    bindingId_ = 0;
    candidateStartSeconds_ = 0.0;
}

} // namespace bfvr::stereo
