#include "stereo/OffHandSupportPolicy.h"
#include "stereo/OffHandWeaponSteeringMath.h"
#include "client/BFSoldierOffHandWeaponSteering.h"
#include "client/BFSoldierOffHandSupportBinding.h"
#include "client/BFSoldierLeftGripRotationBinding.h"
#include "client/BFSoldierPrimarySupportPoseCache.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace
{

using bfvr::stereo::OffHandSupportPolicy;
using bfvr::stereo::OffHandSupportSample;
using bfvr::stereo::OffHandSupportState;
using bfvr::stereo::Matrix4;

bool Expect(
    const bool condition,
    const char* message) noexcept
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
    return condition;
}

OffHandSupportSample ValidSample(
    const double timeSeconds,
    const float distanceMetres,
    const std::uint64_t bindingId = 1) noexcept
{
    OffHandSupportSample sample = {};
    sample.bindingId = bindingId;
    sample.timeSeconds = timeSeconds;
    sample.supportDistanceMetres = distanceMetres;
    sample.sessionFocused = true;
    sample.leftGripTracked = true;
    sample.leftGripHeld = true;
    sample.supportPoseValid = true;
    return sample;
}

Matrix4 Translation(
    const float x,
    const float y,
    const float z) noexcept
{
    Matrix4 result = {};
    result.values[0][0] = 1.0F;
    result.values[1][1] = 1.0F;
    result.values[2][2] = 1.0F;
    result.values[3][0] = x;
    result.values[3][1] = y;
    result.values[3][2] = z;
    result.values[3][3] = 1.0F;
    return result;
}

Matrix4 YawRightAngle() noexcept
{
    auto result = Translation(0.0F, 0.0F, 0.0F);
    result.values[0][0] = 0.0F;
    result.values[0][2] = 1.0F;
    result.values[2][0] = -1.0F;
    result.values[2][2] = 0.0F;
    return result;
}

Matrix4 PitchUpRightAngle() noexcept
{
    auto result = Translation(0.0F, 0.0F, 0.0F);
    result.values[1][1] = 0.0F;
    result.values[1][2] = 1.0F;
    result.values[2][1] = -1.0F;
    result.values[2][2] = 0.0F;
    return result;
}

bool LearnedLeftWristReferenceSurvivesItemAndTrackingChanges() noexcept
{
    bfvr::BFSoldierLeftGripRotationBinding binding;
    auto* const soldier = reinterpret_cast<void*>(0x1000);
    auto* const skeleton = reinterpret_cast<void*>(0x2000);
    auto* const primary = reinterpret_cast<void*>(0x3000);
    auto* const gadget = reinterpret_cast<void*>(0x4000);
    const auto grip = Translation(0.0F, 0.0F, 0.0F);
    const auto authoredPrimary = YawRightAngle();
    const auto gadgetNative = PitchUpRightAngle();
    Matrix4 target = {};
    bool created = false;
    if (!binding.Update(
            soldier, skeleton, primary, 21, grip, authoredPrimary,
            target, created))
    {
        return Expect(false, "left wrist fallback could not initialize");
    }
    binding.CaptureAnatomicalReference(
        soldier, skeleton, primary, 21, grip, authoredPrimary, nullptr);
    created = false;
    if (!binding.Update(
            soldier, skeleton, gadget, 21, grip, gadgetNative,
            target, created) ||
        !Expect(
            std::fabs(target.values[0][2] - 1.0F) < 0.0001F &&
                std::fabs(target.values[1][2]) < 0.0001F && !created,
            "gadget switch replaced the learned anatomical left wrist"))
    {
        return false;
    }
    binding.ResetTransient();
    created = false;
    if (!binding.Update(
            soldier, skeleton, gadget, 21, grip, gadgetNative,
            target, created) ||
        !Expect(
            std::fabs(target.values[0][2] - 1.0F) < 0.0001F && !created,
            "tracking reset discarded the learned anatomical left wrist"))
    {
        return false;
    }
    binding.Reset();
    created = false;
    return binding.Update(
               soldier, skeleton, gadget, 21, grip, gadgetNative,
               target, created) &&
        Expect(
            std::fabs(target.values[1][2] - 1.0F) < 0.0001F && created,
            "full lifetime reset retained the prior anatomical left wrist");
}

bool PrimarySupportRelationRejectsRedeployDrift() noexcept
{
    bfvr::BFSoldierPrimarySupportPoseCache cache;
    auto* const soldier = reinterpret_cast<void*>(0x1000);
    auto* const skeleton = reinterpret_cast<void*>(0x2000);
    auto* const rifle = reinterpret_cast<void*>(0x3000);
    auto first = YawRightAngle();
    first.values[3][0] = 0.45F;
    auto redeploy = PitchUpRightAngle();
    redeploy.values[3][0] = 0.57F;
    cache.Resolve(soldier, skeleton, rifle, 3, first, nullptr);
    cache.Resolve(soldier, skeleton, rifle, 3, redeploy, nullptr);
    if (!Expect(
            std::fabs(redeploy.values[3][0] - 0.45F) < 0.0001F &&
                std::fabs(redeploy.values[0][2] - 1.0F) < 0.0001F,
            "primary redeploy replaced its first known-good support relation"))
    {
        return false;
    }
    auto gadget = PitchUpRightAngle();
    gadget.values[3][0] = 0.12F;
    cache.Resolve(soldier, skeleton, rifle, 4, gadget, nullptr);
    if (!Expect(
            std::fabs(gadget.values[3][0] - 0.12F) < 0.0001F,
            "non-primary relation was overwritten by the primary cache"))
    {
        return false;
    }
    cache.Reset();
    auto nextLifetime = Translation(0.61F, 0.0F, 0.0F);
    cache.Resolve(soldier, skeleton, rifle, 3, nextLifetime, nullptr);
    return Expect(
        std::fabs(nextLifetime.values[3][0] - 0.61F) < 0.0001F,
        "primary cache reset retained the prior lifetime relation");
}

bool ReconstructsAuthoredVisualSocket() noexcept
{
    const auto leftFromRightHand =
        Translation(0.40F, 0.0F, 0.0F);
    Matrix4 controllerRightHandWorld = {};
    // Row-vector +90-degree Z rotation: the authored +X socket must become
    // +Y under the live solved right-hand basis.
    controllerRightHandWorld.values[0][1] = 1.0F;
    controllerRightHandWorld.values[1][0] = -1.0F;
    controllerRightHandWorld.values[2][2] = 1.0F;
    controllerRightHandWorld.values[3][0] = 2.0F;
    controllerRightHandWorld.values[3][1] = 1.0F;
    controllerRightHandWorld.values[3][2] = -3.0F;
    controllerRightHandWorld.values[3][3] = 1.0F;
    const auto inverseSoldier =
        Translation(-2.0F, -1.0F, 3.0F);
    const auto controllerLeft =
        Translation(0.10F, 0.40F, 0.0F);
    const auto result =
        bfvr::stereo::ComputeOffHandAuthoredSupportPose(
            leftFromRightHand,
            controllerRightHandWorld,
            inverseSoldier,
            controllerLeft,
            1.0F);
    return Expect(
               result.has_value(),
               "valid authored visual socket was rejected") &&
        Expect(
            std::fabs(
                result->targetLocal.values[3][1] -
                0.40F) < 0.0001F,
            "authored support target used the wrong row-vector order") &&
        Expect(
            std::fabs(
                result->targetLocal.values[0][1] -
                1.0F) < 0.0001F,
            "authored support orientation did not follow the gun basis") &&
        Expect(
            std::fabs(
                result->controllerDistanceMetres -
                0.10F) < 0.0001F,
            "controller-to-authored-socket distance is incorrect");
}

bool CapturedClosePoseIsNoJumpAndFollowsRightHand() noexcept
{
    auto rightHandWorld =
        Translation(2.0F, 1.0F, -3.0F);
    const auto inverseSoldier =
        Translation(-2.0F, -1.0F, 3.0F);
    const auto controllerLeft =
        Translation(-0.08F, -0.03F, 0.02F);
    const auto candidate =
        bfvr::stereo::ComputeOffHandCloseSupportCandidate(
            rightHandWorld,
            inverseSoldier,
            controllerLeft,
            1.0F);
    const auto relation =
        bfvr::stereo::CaptureOffHandCloseRelation(
            rightHandWorld,
            inverseSoldier,
            controllerLeft);
    const auto atCapture =
        relation.has_value()
        ? bfvr::stereo::
              ComputeOffHandCapturedCloseSupportPose(
                  *relation,
                  rightHandWorld,
                  inverseSoldier,
                  controllerLeft,
                  1.0F)
        : std::nullopt;
    if (!Expect(
            candidate.has_value() &&
                relation.has_value() &&
                atCapture.has_value(),
            "valid close visual cup could not be captured") ||
        !Expect(
            candidate->controllerDistanceMetres > 0.08F &&
                candidate->controllerDistanceMetres < 0.10F,
            "close candidate did not measure hand separation") ||
        !Expect(
            std::fabs(
                atCapture->targetLocal.values[3][0] -
                controllerLeft.values[3][0]) < 0.0001F &&
                std::fabs(
                    atCapture->targetLocal.values[3][1] -
                    controllerLeft.values[3][1]) < 0.0001F &&
                std::fabs(
                    atCapture->targetLocal.values[3][2] -
                    controllerLeft.values[3][2]) < 0.0001F,
            "close visual cup jumped when captured"))
    {
        return false;
    }

    rightHandWorld.values[3][0] += 0.50F;
    const auto followed =
        bfvr::stereo::ComputeOffHandCapturedCloseSupportPose(
            *relation,
            rightHandWorld,
            inverseSoldier,
            controllerLeft,
            1.0F);
    return Expect(
        followed.has_value() &&
            std::fabs(
                followed->targetLocal.values[3][0] -
                (controllerLeft.values[3][0] + 0.50F)) <
                0.0001F,
        "captured close cup did not follow the solved right hand");
}

bool CloseBindingCapturesAndIgnoresLeftNoise() noexcept
{
    bfvr::BFSoldierOffHandSupportBinding binding;
    bfvr::BFSoldierOffHandSupportInput input = {};
    input.bindingId = 7;
    input.timeSeconds = 1.0;
    input.squeezeValue = 1.0F;
    input.sessionFocused = true;
    input.leftGripTracked = true;
    input.leftSqueezeActive = true;
    input.mode =
        bfvr::BFSoldierOffHandSupportMode::CapturedClose;
    input.controllerRightHandWorld =
        Translation(2.0F, 1.0F, -3.0F);
    input.inverseSoldierWorld =
        Translation(-2.0F, -1.0F, 3.0F);
    input.controllerLeftHandLocal =
        Translation(-0.08F, -0.03F, 0.02F);
    if (!Expect(
            binding.Update(input).state ==
                OffHandSupportState::Candidate,
            "close binding did not enter Candidate"))
    {
        return false;
    }

    input.timeSeconds = 1.05;
    const auto acquired = binding.Update(input);
    if (!Expect(
            acquired.supported &&
                acquired.enteredSupport,
            "close binding did not capture support") ||
        !Expect(
            std::fabs(
                acquired.targetLocal.values[3][0] -
                input.controllerLeftHandLocal.values[3][0]) <
                0.0001F,
            "close binding jumped on acquisition"))
    {
        return false;
    }
    bfvr::BFSoldierOffHandSteeringInput steeringInput = {};
    steeringInput.bindingId = input.bindingId;
    steeringInput.squeezeValue = input.squeezeValue;
    steeringInput.sessionFocused = true;
    steeringInput.leftGripTracked = true;
    steeringInput.leftSqueezeActive = true;
    steeringInput.mode =
        bfvr::BFSoldierOffHandSupportMode::CapturedClose;
    steeringInput.controllerGunWorld =
        input.controllerRightHandWorld;
    steeringInput.predictedSupportWorld =
        Translation(2.0F, 1.0F, -2.6F);
    steeringInput.trackedLeftHandWorld =
        Translation(2.1F, 1.0F, -2.6F);
    steeringInput.maximumSwingRadians = 0.60F;
    bfvr::stereo::OffHandWeaponSteeringResult steering = {};
    if (!Expect(
            !binding.TryComputeSupportedWeaponSteering(
                steeringInput,
                steering),
            "captured-close pistol support was allowed to steer"))
    {
        return false;
    }

    input.timeSeconds = 1.06;
    input.controllerRightHandWorld.values[3][0] += 0.10F;
    input.controllerLeftHandLocal.values[3][0] += 0.15F;
    const auto followed = binding.Update(input);
    if (!Expect(
            followed.supported &&
                std::fabs(
                    followed.targetLocal.values[3][0] -
                    0.02F) < 0.0001F,
            "close binding did not follow only the right-hand delta"))
    {
        return false;
    }

    input.timeSeconds = 1.07;
    input.leftSqueezeActive = false;
    input.squeezeValue = 0.0F;
    const auto released = binding.Update(input);
    return Expect(
        released.state == OffHandSupportState::Free &&
            released.exitedSupport,
        "close binding did not release on squeeze-up");
}

bool AuthoredBindingAllowsOnlyCurrentSupportedSteering() noexcept
{
    bfvr::BFSoldierOffHandSupportBinding binding;
    bfvr::BFSoldierOffHandSupportInput input = {};
    input.bindingId = 11;
    input.timeSeconds = 1.0;
    input.squeezeValue = 1.0F;
    input.sessionFocused = true;
    input.leftGripTracked = true;
    input.leftSqueezeActive = true;
    input.mode =
        bfvr::BFSoldierOffHandSupportMode::AuthoredHandSpan;
    input.leftHandFromRightHand =
        Translation(0.0F, 0.0F, 0.40F);
    input.controllerRightHandWorld =
        Translation(2.0F, 1.0F, -3.0F);
    input.inverseSoldierWorld =
        Translation(-2.0F, -1.0F, 3.0F);
    input.controllerLeftHandLocal =
        Translation(0.0F, 0.0F, 0.35F);
    static_cast<void>(binding.Update(input));
    input.timeSeconds = 1.05;
    const auto acquired = binding.Update(input);
    if (!Expect(
            acquired.supported,
            "authored binding did not acquire for steering"))
    {
        return false;
    }

    bfvr::BFSoldierOffHandSteeringInput steeringInput = {};
    steeringInput.bindingId = input.bindingId;
    steeringInput.squeezeValue = input.squeezeValue;
    steeringInput.sessionFocused = true;
    steeringInput.leftGripTracked = true;
    steeringInput.leftSqueezeActive = true;
    steeringInput.mode = input.mode;
    steeringInput.controllerGunWorld =
        input.controllerRightHandWorld;
    steeringInput.predictedSupportWorld =
        Translation(2.0F, 1.0F, -2.6F);
    steeringInput.trackedLeftHandWorld =
        Translation(2.1F, 1.0F, -2.6F);
    steeringInput.maximumSwingRadians = 0.60F;
    bfvr::stereo::OffHandWeaponSteeringResult steering = {};
    if (!Expect(
            binding.TryComputeSupportedWeaponSteering(
                steeringInput,
                steering),
            "supported authored span could not steer"))
    {
        return false;
    }

    steeringInput.leftSqueezeActive = false;
    return Expect(
        !binding.TryComputeSupportedWeaponSteering(
            steeringInput,
            steering),
        "current squeeze-up retained stale steering");
}

bool ToggleGripReleasesOnlyOnNextPress() noexcept
{
    bfvr::BFSoldierOffHandSupportBinding binding;
    bfvr::BFSoldierOffHandSupportInput input = {};
    input.bindingId = 13;
    input.timeSeconds = 1.0;
    input.squeezeValue = 1.0F;
    input.sessionFocused = true;
    input.leftGripTracked = true;
    input.leftSqueezeActive = true;
    input.toggleGripStyle = true;
    input.mode = bfvr::BFSoldierOffHandSupportMode::AuthoredHandSpan;
    input.leftHandFromRightHand = Translation(0.0F, 0.0F, 0.40F);
    input.controllerRightHandWorld = Translation(2.0F, 1.0F, -3.0F);
    input.inverseSoldierWorld = Translation(-2.0F, -1.0F, 3.0F);
    input.controllerLeftHandLocal = Translation(0.0F, 0.0F, 0.35F);
    static_cast<void>(binding.Update(input));
    input.timeSeconds = 1.05;
    if (!Expect(
            binding.Update(input).supported,
            "toggle grip did not acquire support"))
    {
        return false;
    }
    input.timeSeconds = 1.06;
    input.leftSqueezeActive = false;
    input.squeezeValue = 0.0F;
    if (!Expect(
            binding.Update(input).supported,
            "toggle grip released on physical squeeze-up"))
    {
        return false;
    }
    input.timeSeconds = 1.07;
    input.leftSqueezeActive = true;
    input.squeezeValue = 1.0F;
    const auto released = binding.Update(input);
    return Expect(
        released.state == OffHandSupportState::Free &&
            released.exitedSupport,
        "toggle grip did not release on the next press");
}

bool SoldierSteeringFrameUsesTrackedGripAndRejectsPistol() noexcept
{
    bfvr::BFSoldierOffHandSupportBinding binding;
    bfvr::BFSoldierOffHandSupportInput support = {};
    support.bindingId = 19;
    support.timeSeconds = 1.0;
    support.squeezeValue = 1.0F;
    support.sessionFocused = true;
    support.leftGripTracked = true;
    support.leftSqueezeActive = true;
    support.mode =
        bfvr::BFSoldierOffHandSupportMode::AuthoredHandSpan;
    support.leftHandFromRightHand =
        Translation(0.0F, 0.0F, 0.40F);
    support.controllerRightHandWorld =
        Translation(2.0F, 1.0F, -3.0F);
    support.inverseSoldierWorld =
        Translation(-2.0F, -1.0F, 3.0F);
    support.controllerLeftHandLocal =
        Translation(0.05F, 0.0F, 0.40F);
    static_cast<void>(binding.Update(support));
    support.timeSeconds = 1.05;
    if (!binding.Update(support).supported)
    {
        return Expect(
            false,
            "authored frame binding did not acquire");
    }

    bfvr::BFSoldierOffHandWeaponSteeringInput input = {};
    input.bindingId = support.bindingId;
    input.sessionFocused = true;
    input.leftGripTracked = true;
    input.leftSqueezeActive = true;
    input.mode = support.mode;
    input.leftHand.squeezeValue = 1.0F;
    input.leftHand.gripPose.orientationW = 1.0F;
    input.leftHand.gripPose.positionX = 2.10F;
    input.leftHand.gripPose.positionY = 1.0F;
    input.leftHand.gripPose.positionZ = 2.60F;
    input.soldierWorld = Translation(0.0F, 0.0F, 0.0F);
    input.controllerGunWorld =
        support.controllerRightHandWorld;
    input.controllerRightHandWorld =
        support.controllerRightHandWorld;
    input.leftHandFromRightHand =
        support.leftHandFromRightHand;
    input.maximumSwingRadians = 0.60F;
    const auto rifle =
        bfvr::TryComputeBFSoldierOffHandWeaponSteering(
            binding,
            input);
    if (!Expect(
            rifle.has_value() &&
                std::fabs(
                    rifle->gunWorld.values[3][0] - 2.0F) <
                    0.0001F,
            "soldier steering frame did not preserve its pivot"))
    {
        return false;
    }
    input.mode =
        bfvr::BFSoldierOffHandSupportMode::CapturedClose;
    return Expect(
        !bfvr::TryComputeBFSoldierOffHandWeaponSteering(
             binding,
             input).has_value(),
        "soldier steering frame accepted pistol mode");
}

bool SteeringUsesFixedPivotAndFullDirectionalSwing() noexcept
{
    constexpr float kPi = 3.14159265358979323846F;
    const float requestedSwing = 0.5F * kPi;
    const auto gun = Translation(2.0F, 1.0F, -3.0F);
    const auto predicted =
        Translation(2.0F, 1.0F, -2.6F);
    const auto tracked =
        Translation(2.4F, 1.0F, -3.0F);
    const auto result =
        bfvr::stereo::ComputeBoundedOffHandWeaponSteering(
            gun,
            predicted,
            tracked,
            bfvr::stereo::kUnrestrictedOffHandWeaponSwingRadians,
            1.0F);
    if (!Expect(
            result.has_value(),
            "valid long support steering was rejected"))
    {
        return false;
    }

    const float sine = std::sin(requestedSwing);
    const float cosine = std::cos(requestedSwing);
    return Expect(
               std::fabs(
                   result->appliedSwingRadians -
                    requestedSwing) < 0.0001F,
               "full off-hand swing retained the obsolete 35-degree cap") &&
        Expect(
            std::fabs(result->gunWorld.values[3][0] - 2.0F) <
                    0.0001F &&
                std::fabs(result->gunWorld.values[3][1] - 1.0F) <
                    0.0001F &&
                std::fabs(result->gunWorld.values[3][2] + 3.0F) <
                    0.0001F,
            "off-hand steering moved the right-grip pivot") &&
        Expect(
            std::fabs(result->gunWorld.values[2][0] - sine) <
                    0.0001F &&
                std::fabs(result->gunWorld.values[2][2] - cosine) <
                    0.0001F,
            "full-range row-vector swing used the wrong direction") &&
        Expect(
            std::fabs(
                result->gunWorld.values[2][0] *
                        result->gunWorld.values[2][0] +
                    result->gunWorld.values[2][1] *
                        result->gunWorld.values[2][1] +
                    result->gunWorld.values[2][2] *
                        result->gunWorld.values[2][2] -
                    1.0F) < 0.0001F,
            "off-hand steering introduced weapon scale");
}

bool SteeringIgnoresRadialMismatchAndHandlesOppositeDirection() noexcept
{
    constexpr float kPi = 3.14159265358979323846F;
    const float requested = 10.0F * kPi / 180.0F;
    const auto gun = Translation(2.0F, 1.0F, -3.0F);
    const auto predicted =
        Translation(2.0F, 1.0F, -2.6F);
    const auto tracked = Translation(
        2.0F + 1.5F * std::sin(requested),
        1.0F,
        -3.0F + 1.5F * std::cos(requested));
    const auto result =
        bfvr::stereo::ComputeBoundedOffHandWeaponSteering(
            gun,
            predicted,
            tracked,
            bfvr::stereo::kUnrestrictedOffHandWeaponSwingRadians,
            1.0F);
    if (!Expect(
            result.has_value() &&
                std::fabs(
                    result->appliedSwingRadians -
                    requested) < 0.0001F,
            "radial mismatch altered a valid support direction"))
    {
        return false;
    }

    const auto collapsed =
        bfvr::stereo::ComputeBoundedOffHandWeaponSteering(
            gun,
            predicted,
            gun,
            bfvr::stereo::kUnrestrictedOffHandWeaponSwingRadians,
            1.0F);
    const auto opposite = Translation(2.0F, 1.0F, -3.4F);
    const auto oppositeResult =
        bfvr::stereo::ComputeBoundedOffHandWeaponSteering(
            gun,
            predicted,
            opposite,
            bfvr::stereo::kUnrestrictedOffHandWeaponSwingRadians,
            1.0F);
    return Expect(
               !collapsed.has_value(),
               "collapsed tracked support span was accepted") &&
        Expect(
            oppositeResult.has_value() &&
                std::fabs(oppositeResult->appliedSwingRadians - kPi) <
                    0.0001F &&
                std::fabs(oppositeResult->gunWorld.values[2][2] + 1.0F) <
                    0.0001F,
            "opposite support direction did not use a deterministic full swing");
}

bool AcquireByProximityAndRetainUntilExplicitRelease() noexcept
{
    OffHandSupportPolicy policy;
    if (!Expect(
            policy.Update(ValidSample(1.0, 0.10F)).state ==
                OffHandSupportState::Candidate,
            "near authored socket did not enter Candidate"))
    {
        return false;
    }
    if (!Expect(
            policy.Update(ValidSample(1.03, 0.10F)).state ==
                OffHandSupportState::Candidate,
            "Candidate acquired before the hold interval"))
    {
        return false;
    }
    const auto acquired = policy.Update(ValidSample(1.05, 0.10F));
    if (!Expect(
            acquired.state == OffHandSupportState::Supported &&
                acquired.enteredSupport,
            "stable near grip did not enter Supported"))
    {
        return false;
    }
    if (!Expect(
            policy.Update(ValidSample(1.06, 5.0F)).state ==
                OffHandSupportState::Supported,
            "held support auto-detached after pulling away"))
    {
        return false;
    }
    auto releaseSample = ValidSample(1.07, 5.0F);
    releaseSample.leftGripHeld = false;
    const auto released = policy.Update(releaseSample);
    return Expect(
        released.state == OffHandSupportState::Free &&
            released.exitedSupport,
        "support did not release on explicit squeeze-up");
}

bool RejectsUnsafeInputs() noexcept
{
    OffHandSupportPolicy policy;
    auto sample = ValidSample(1.0, 0.10F);
    sample.leftGripHeld = false;
    if (!Expect(
            policy.Update(sample).state == OffHandSupportState::Free,
            "unheld grip became a support candidate"))
    {
        return false;
    }

    sample = ValidSample(1.1, 0.10F);
    sample.leftGripTracked = false;
    if (!Expect(
            policy.Update(sample).state == OffHandSupportState::Free,
            "untracked grip became a support candidate"))
    {
        return false;
    }

    sample = ValidSample(1.2, 0.10F);
    sample.nativeLeftHandTargetActive = true;
    if (!Expect(
            policy.Update(sample).state == OffHandSupportState::Free,
            "native left-hand target ownership was ignored"))
    {
        return false;
    }

    sample = ValidSample(
        1.3,
        std::numeric_limits<float>::quiet_NaN());
    return Expect(
        policy.Update(sample).state == OffHandSupportState::Free,
        "non-finite support distance was accepted");
}

bool ReleasesOnTrackingOrBindingChange() noexcept
{
    OffHandSupportPolicy policy;
    static_cast<void>(policy.Update(ValidSample(1.0, 0.10F)));
    static_cast<void>(policy.Update(ValidSample(1.1, 0.10F)));
    auto sample = ValidSample(1.2, 0.10F);
    sample.sessionFocused = false;
    const auto focusLost = policy.Update(sample);
    if (!Expect(
            focusLost.state == OffHandSupportState::Free &&
                focusLost.exitedSupport,
            "focus loss did not release support"))
    {
        return false;
    }

    static_cast<void>(policy.Update(ValidSample(2.0, 0.10F, 1)));
    static_cast<void>(policy.Update(ValidSample(2.1, 0.10F, 1)));
    const auto changed = policy.Update(ValidSample(2.2, 0.10F, 2));
    return Expect(
        changed.state == OffHandSupportState::Candidate &&
            changed.exitedSupport,
        "item/soldier binding change retained old support");
}

bool RequiresContinuousAcquisition() noexcept
{
    OffHandSupportPolicy policy;
    static_cast<void>(policy.Update(ValidSample(1.0, 0.10F)));
    if (!Expect(
            policy.Update(ValidSample(1.02, 0.13F)).state ==
                OffHandSupportState::Free,
            "Candidate persisted outside the acquire radius"))
    {
        return false;
    }
    if (!Expect(
            policy.Update(ValidSample(2.0, 0.10F)).state ==
                OffHandSupportState::Candidate,
            "second acquisition did not enter Candidate"))
    {
        return false;
    }
    if (!Expect(
            policy.Update(ValidSample(1.0, 0.10F)).state ==
                OffHandSupportState::Candidate,
            "backward time did not safely restart Candidate"))
    {
        return false;
    }
    return Expect(
        policy.Update(ValidSample(1.03, 0.10F)).state ==
            OffHandSupportState::Candidate,
        "restarted Candidate reused elapsed time from the prior clock");
}

} // namespace

int main()
{
    const bool passed =
        ReconstructsAuthoredVisualSocket() &&
        LearnedLeftWristReferenceSurvivesItemAndTrackingChanges() &&
        PrimarySupportRelationRejectsRedeployDrift() &&
        CapturedClosePoseIsNoJumpAndFollowsRightHand() &&
        CloseBindingCapturesAndIgnoresLeftNoise() &&
        AuthoredBindingAllowsOnlyCurrentSupportedSteering() &&
        ToggleGripReleasesOnlyOnNextPress() &&
        SoldierSteeringFrameUsesTrackedGripAndRejectsPistol() &&
        SteeringUsesFixedPivotAndFullDirectionalSwing() &&
        SteeringIgnoresRadialMismatchAndHandlesOppositeDirection() &&
        AcquireByProximityAndRetainUntilExplicitRelease() &&
        RejectsUnsafeInputs() &&
        ReleasesOnTrackingOrBindingChange() &&
        RequiresContinuousAcquisition();
    if (passed)
    {
        std::puts("Off-hand support policy tests passed.");
        return 0;
    }
    return 1;
}
