#include "client/BFSoldierOffHandSupportBinding.h"

#include "stereo/OffHandWeaponSteeringMath.h"

#include <cmath>
#include <optional>
#include <array>
#include <cwchar>

namespace
{

constexpr float kSqueezePressThreshold = 0.60F;
constexpr float kSqueezeReleaseThreshold = 0.45F;
constexpr float kAuthoredAcquireDistanceMetres = 0.18F;
constexpr float kCapturedCloseAcquireDistanceMetres = 0.12F;
constexpr DWORD kBlockedReportIntervalMs = 500;
constexpr LONG kMaximumBlockedReports = 12;

} // namespace

namespace bfvr
{

BFSoldierOffHandSupportOutput
BFSoldierOffHandSupportBinding::Update(
    const BFSoldierOffHandSupportInput& input) noexcept
{
    BFSoldierOffHandSupportOutput output = {};
    AcquireSRWLockExclusive(&lock_);
    const bool toggleGripStyle = input.toggleGripStyle;
    if (toggleGripStyle_ != toggleGripStyle)
    {
        policy_.Reset();
        squeezeHeld_ = false;
        physicalSqueezeHeld_ = false;
        toggleSupportHeld_ = false;
        toggleGripStyle_ = toggleGripStyle;
    }
    if (bindingId_ != input.bindingId ||
        mode_ != input.mode)
    {
        closeLeftHandFromRightHandLocal_ = {};
        closeRelationValid_ = false;
        bindingId_ = input.bindingId;
        mode_ = input.mode;
        lastBlockedReportAt_ = 0;
        blockedReports_ = 0;
        toggleSupportHeld_ = false;
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

    const bool physicalWasHeld = physicalSqueezeHeld_;
    if (!input.leftSqueezeActive ||
        !std::isfinite(input.squeezeValue))
    {
        physicalSqueezeHeld_ = false;
    }
    else if (physicalSqueezeHeld_)
    {
        physicalSqueezeHeld_ =
            input.squeezeValue >= kSqueezeReleaseThreshold;
    }
    else
    {
        physicalSqueezeHeld_ =
            input.squeezeValue >= kSqueezePressThreshold;
    }
    if (!input.sessionFocused || !input.leftGripTracked)
    {
        toggleSupportHeld_ = false;
    }
    else if (toggleGripStyle_ && physicalSqueezeHeld_ && !physicalWasHeld)
    {
        toggleSupportHeld_ = !toggleSupportHeld_;
    }
    squeezeHeld_ = toggleGripStyle_
        ? toggleSupportHeld_
        : physicalSqueezeHeld_;

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
    const float acquireDistanceMetres =
        input.mode == BFSoldierOffHandSupportMode::AuthoredHandSpan
        ? kAuthoredAcquireDistanceMetres
        : input.mode == BFSoldierOffHandSupportMode::CapturedClose
        ? kCapturedCloseAcquireDistanceMetres
        : 0.0F;
    auto state = policy_.Update(sample, acquireDistanceMetres);
    if (input.mode == BFSoldierOffHandSupportMode::AuthoredHandSpan &&
        pose.has_value() && squeezeHeld_ &&
        pose->controllerDistanceMetres > kAuthoredAcquireDistanceMetres &&
        state.state != stereo::OffHandSupportState::Supported &&
        input.diagnostics.appendLog != nullptr &&
        blockedReports_ < kMaximumBlockedReports)
    {
        const DWORD now = GetTickCount();
        if (lastBlockedReportAt_ == 0 ||
            now - lastBlockedReportAt_ >= kBlockedReportIntervalMs)
        {
            lastBlockedReportAt_ = now;
            ++blockedReports_;
            std::array<wchar_t, 896> message = {};
            _snwprintf_s(
                message.data(), message.size(), _TRUNCATE,
                L"Native 1P primary support acquisition is outside the "
                L"authored-point gate: soldier=%p item=%p activeItemIndex=%ld "
                L"distance=%.4f m predictedLocal=(%.4f,%.4f,%.4f) "
                L"trackedLocal=(%.4f,%.4f,%.4f). No support snap or anchor "
                L"replacement was applied.",
                input.diagnostics.soldier,
                input.diagnostics.activeItem,
                input.diagnostics.activeItemIndex,
                pose->controllerDistanceMetres,
                pose->targetLocal.values[3][0],
                pose->targetLocal.values[3][1],
                pose->targetLocal.values[3][2],
                input.controllerLeftHandLocal.values[3][0],
                input.controllerLeftHandLocal.values[3][1],
                input.controllerLeftHandLocal.values[3][2]);
            input.diagnostics.appendLog(message.data());
        }
    }
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
    const bool gripStillActive = toggleGripStyle_
        ? squeezeHeld_
        : squeezeHeld_ && input.leftSqueezeActive &&
            std::isfinite(input.squeezeValue) &&
            input.squeezeValue >= kSqueezeReleaseThreshold;
    const bool supported =
        input.mode ==
            BFSoldierOffHandSupportMode::AuthoredHandSpan &&
        mode_ == input.mode &&
        bindingId_ == input.bindingId &&
        bindingId_ != 0 &&
        policy_.State() ==
            stereo::OffHandSupportState::Supported &&
        gripStillActive &&
        input.sessionFocused &&
        input.leftGripTracked &&
        !input.nativeLeftHandTargetActive &&
        std::isfinite(input.squeezeValue);
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
    physicalSqueezeHeld_ = false;
    toggleSupportHeld_ = false;
    toggleGripStyle_ = false;
    lastBlockedReportAt_ = 0;
    blockedReports_ = 0;
    ReleaseSRWLockExclusive(&lock_);
}

} // namespace bfvr
