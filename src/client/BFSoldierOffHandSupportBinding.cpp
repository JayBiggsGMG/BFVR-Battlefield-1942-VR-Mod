#include "client/BFSoldierOffHandSupportBinding.h"

#include "stereo/OffHandWeaponSteeringMath.h"

#include <cmath>
#include <optional>

namespace
{

constexpr float kSqueezePressThreshold = 0.60F;
constexpr float kSqueezeReleaseThreshold = 0.45F;

} // namespace

namespace bfvr
{

BFSoldierOffHandSupportOutput
BFSoldierOffHandSupportBinding::Update(
    const BFSoldierOffHandSupportInput& input) noexcept
{
    BFSoldierOffHandSupportOutput output = {};
    AcquireSRWLockExclusive(&lock_);
    if (bindingId_ != input.bindingId ||
        mode_ != input.mode)
    {
        closeLeftHandFromRightHandLocal_ = {};
        closeRelationValid_ = false;
        bindingId_ = input.bindingId;
        mode_ = input.mode;
    }

    std::optional<stereo::OffHandVisualSupportPose> pose;
    switch (input.mode)
    {
    case BFSoldierOffHandSupportMode::AuthoredHandSpan:
        pose = stereo::ComputeOffHandAuthoredSupportPose(
            input.leftHandFromRightHand,
            input.controllerRightHandWorld,
            input.inverseSoldierWorld,
            input.controllerLeftHandLocal,
            1.0F);
        break;

    case BFSoldierOffHandSupportMode::CapturedClose:
        pose = closeRelationValid_
            ? stereo::ComputeOffHandCapturedCloseSupportPose(
                  closeLeftHandFromRightHandLocal_,
                  input.controllerRightHandWorld,
                  input.inverseSoldierWorld,
                  input.controllerLeftHandLocal,
                  1.0F)
            : stereo::ComputeOffHandCloseSupportCandidate(
                  input.controllerRightHandWorld,
                  input.inverseSoldierWorld,
                  input.controllerLeftHandLocal,
                  1.0F);
        break;

    case BFSoldierOffHandSupportMode::Disabled:
        break;
    }

    if (!input.leftSqueezeActive ||
        !std::isfinite(input.squeezeValue))
    {
        squeezeHeld_ = false;
    }
    else if (squeezeHeld_)
    {
        squeezeHeld_ =
            input.squeezeValue >= kSqueezeReleaseThreshold;
    }
    else
    {
        squeezeHeld_ =
            input.squeezeValue >= kSqueezePressThreshold;
    }

    stereo::OffHandSupportSample sample = {};
    sample.bindingId = input.bindingId;
    sample.timeSeconds = input.timeSeconds;
    sample.supportDistanceMetres =
        pose.has_value()
        ? pose->controllerDistanceMetres
        : 0.0F;
    sample.sessionFocused = input.sessionFocused;
    sample.leftGripTracked = input.leftGripTracked;
    sample.leftGripHeld = squeezeHeld_;
    sample.supportPoseValid = pose.has_value();
    sample.nativeLeftHandTargetActive =
        input.nativeLeftHandTargetActive;
    auto state = policy_.Update(sample);
    if (input.mode ==
            BFSoldierOffHandSupportMode::CapturedClose &&
        state.enteredSupport)
    {
        const float acquisitionDistance =
            pose.has_value()
            ? pose->controllerDistanceMetres
            : 0.0F;
        const auto relation =
            stereo::CaptureOffHandCloseRelation(
                input.controllerRightHandWorld,
                input.inverseSoldierWorld,
                input.controllerLeftHandLocal);
        if (relation.has_value())
        {
            closeLeftHandFromRightHandLocal_ = *relation;
            closeRelationValid_ = true;
            pose =
                stereo::ComputeOffHandCapturedCloseSupportPose(
                    closeLeftHandFromRightHandLocal_,
                    input.controllerRightHandWorld,
                    input.inverseSoldierWorld,
                    input.controllerLeftHandLocal,
                    1.0F);
            if (pose.has_value())
            {
                pose->controllerDistanceMetres =
                    acquisitionDistance;
            }
        }
        else
        {
            policy_.Reset();
            state = {};
        }
    }
    if (input.mode ==
            BFSoldierOffHandSupportMode::CapturedClose &&
        state.state == stereo::OffHandSupportState::Free)
    {
        closeLeftHandFromRightHandLocal_ = {};
        closeRelationValid_ = false;
    }
    output.state = state.state;
    output.enteredSupport = state.enteredSupport;
    output.exitedSupport = state.exitedSupport;
    if (pose.has_value())
    {
        output.targetLocal = pose->targetLocal;
        output.controllerDistanceMetres =
            pose->controllerDistanceMetres;
        output.supported =
            state.state ==
                stereo::OffHandSupportState::Supported &&
            (input.mode !=
                 BFSoldierOffHandSupportMode::CapturedClose ||
             closeRelationValid_);
    }
    ReleaseSRWLockExclusive(&lock_);
    return output;
}

bool BFSoldierOffHandSupportBinding::
TryComputeSupportedWeaponSteering(
    const BFSoldierOffHandSteeringInput& input,
    stereo::OffHandWeaponSteeringResult& output) noexcept
{
    output = {};
    AcquireSRWLockShared(&lock_);
    const bool supported =
        input.mode ==
            BFSoldierOffHandSupportMode::AuthoredHandSpan &&
        mode_ == input.mode &&
        bindingId_ == input.bindingId &&
        bindingId_ != 0 &&
        policy_.State() ==
            stereo::OffHandSupportState::Supported &&
        squeezeHeld_ &&
        input.sessionFocused &&
        input.leftGripTracked &&
        input.leftSqueezeActive &&
        !input.nativeLeftHandTargetActive &&
        std::isfinite(input.squeezeValue) &&
        input.squeezeValue >= kSqueezeReleaseThreshold;
    if (!supported)
    {
        ReleaseSRWLockShared(&lock_);
        return false;
    }

    const auto steering =
        stereo::ComputeBoundedOffHandWeaponSteering(
            input.controllerGunWorld,
            input.predictedSupportWorld,
            input.trackedLeftHandWorld,
            input.maximumSwingRadians,
            input.worldUnitsPerMetre);
    ReleaseSRWLockShared(&lock_);
    if (!steering.has_value())
    {
        return false;
    }
    output = *steering;
    return true;
}

void BFSoldierOffHandSupportBinding::Reset() noexcept
{
    AcquireSRWLockExclusive(&lock_);
    policy_.Reset();
    closeLeftHandFromRightHandLocal_ = {};
    bindingId_ = 0;
    mode_ = BFSoldierOffHandSupportMode::Disabled;
    closeRelationValid_ = false;
    squeezeHeld_ = false;
    ReleaseSRWLockExclusive(&lock_);
}

} // namespace bfvr
