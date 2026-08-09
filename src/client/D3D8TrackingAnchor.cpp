#include "client/D3D8TrackingAnchor.h"

#include "stereo/StereoMath.h"

#include <windows.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace
{
constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = 2.0F * kPi;
constexpr float kBattlefieldStandingEyeHeightMeters = 1.70F;
constexpr float kClearlySeatedBelowStandingMeters = 0.30F;
constexpr float kSeatedVerticalMotionThresholdMeters = 0.003F;
constexpr float kArtificialTurnSettleRadians = 0.00174532925F; // 0.1 degree
constexpr std::int32_t kMaximumArtificialTurnDeltaMillidegrees = 720'000;
constexpr std::int64_t kSeatedSettleDurationNanoseconds = 750'000'000;
float WrapYaw(float value) noexcept
{
    while (value > kPi) value -= kTwoPi;
    while (value < -kPi) value += kTwoPi;
    return value;
}

bool ExtractYaw(
    const bfvr::D3D8RuntimeView& view,
    float& yaw) noexcept
{
    const float x = view.orientationX;
    const float y = view.orientationY;
    const float z = view.orientationZ;
    const float w = view.orientationW;
    const float norm = x * x + y * y + z * z + w * w;
    if (!std::isfinite(norm) || norm < 0.5F || norm > 1.5F)
    {
        return false;
    }
    yaw = std::atan2(
        2.0F * (w * y + x * z),
        1.0F - 2.0F * (y * y + z * z));
    return std::isfinite(yaw);
}

void SetYaw(bfvr::D3D8RuntimeView& view, float yaw) noexcept
{
    const float half = WrapYaw(yaw) * 0.5F;
    view.orientationX = 0.0F;
    view.orientationY = std::sin(half);
    view.orientationZ = 0.0F;
    view.orientationW = std::cos(half);
}

std::int32_t WrappedMillidegreeDelta(LONG current, LONG previous) noexcept
{
    const std::uint32_t difference =
        static_cast<std::uint32_t>(current) -
        static_cast<std::uint32_t>(previous);
    return std::bit_cast<std::int32_t>(difference);
}

bfvr::stereo::Pose ToPose(const bfvr::D3D8RuntimeView& view) noexcept
{
    return {
        {view.positionX, view.positionY, view.positionZ},
        {view.orientationX, view.orientationY,
         view.orientationZ, view.orientationW}};
}

bfvr::D3D8RuntimeView FromPose(
    const bfvr::stereo::Pose& pose,
    const bfvr::D3D8RuntimeView& source) noexcept
{
    bfvr::D3D8RuntimeView result = source;
    result.positionX = pose.position.x;
    result.positionY = pose.position.y;
    result.positionZ = pose.position.z;
    result.orientationX = pose.orientation.x;
    result.orientationY = pose.orientation.y;
    result.orientationZ = pose.orientation.z;
    result.orientationW = pose.orientation.w;
    return result;
}

bfvr::D3D8RuntimeControllerPose RebaseControllerPose(
    const bfvr::D3D8RuntimeView& reference,
    const bfvr::D3D8RuntimeControllerPose& source) noexcept
{
    const bfvr::stereo::Pose pose = {
        {source.positionX, source.positionY, source.positionZ},
        {source.orientationX, source.orientationY,
         source.orientationZ, source.orientationW}};
    const auto relative = bfvr::stereo::MakeRelativePose(
        ToPose(reference), pose);
    if (!relative.has_value())
    {
        return source;
    }
    bfvr::D3D8RuntimeControllerPose result = source;
    result.positionX = relative->position.x;
    result.positionY = relative->position.y;
    result.positionZ = relative->position.z;
    result.orientationX = relative->orientation.x;
    result.orientationY = relative->orientation.y;
    result.orientationZ = relative->orientation.z;
    result.orientationW = relative->orientation.w;
    return result;
}

} // namespace

namespace bfvr
{

void D3D8TrackingAnchor::Reset() noexcept
{
    baseReference_ = {};
    context_ = {};
    ClearPendingContext();
    contextGeneration_ = 0;
    consumedRecenterSequence_ = 0;
    standingReferenceY_ = 0.0F;
    manualHeightAdjustmentMeters_ = 0.0F;
    lastSeatedStageHeightMeters_ = 0.0F;
    observedInfantryBodyYawRadians_ = 0.0F;
    artificialTurnLeadRadians_ = 0.0F;
    lastSeatedVerticalMotionTime_ = 0;
    consumedArtificialTurnMillidegrees_ = 0;
    standingMode_ = false;
    standingReferenceValid_ = false;
    infantryModeInitialized_ = false;
    seatedPostureTransitionActive_ = false;
    seatedDescentObserved_ = false;
    artificialTurnInitialized_ = false;
    valid_ = false;
}

void D3D8TrackingAnchor::ClearPendingContext() noexcept
{
    pendingContext_ = {};
    pendingContextSamples_ = 0;
}

void D3D8TrackingAnchor::Capture(
    const D3D8RuntimeView& currentHead,
    D3D8TrackingContext context) noexcept
{
    float yaw = 0.0F;
    if (!ExtractYaw(currentHead, yaw) ||
        !std::isfinite(currentHead.positionX) ||
        !std::isfinite(currentHead.positionY) ||
        !std::isfinite(currentHead.positionZ))
    {
        return;
    }
    baseReference_ = currentHead;
    SetYaw(baseReference_, yaw);
    context_ = context;
    ++contextGeneration_;
    if (contextGeneration_ == 0)
    {
        contextGeneration_ = 1;
    }
    ClearPendingContext();
    standingReferenceValid_ = false;
    infantryModeInitialized_ = false;
    seatedPostureTransitionActive_ = false;
    seatedDescentObserved_ = false;
    lastSeatedVerticalMotionTime_ = 0;
    artificialTurnLeadRadians_ = 0.0F;
    observedInfantryBodyYawRadians_ = 0.0F;
    artificialTurnInitialized_ = false;
    valid_ = true;
}

void D3D8TrackingAnchor::ResetArtificialTurnState(
    LONG cumulativeIntentMillidegrees) noexcept
{
    consumedArtificialTurnMillidegrees_ = cumulativeIntentMillidegrees;
    observedInfantryBodyYawRadians_ = 0.0F;
    artificialTurnLeadRadians_ = 0.0F;
    artificialTurnInitialized_ = false;
}

void D3D8TrackingAnchor::UpdateArtificialTurn(
    const D3D8ArtificialTurnInput& artificialTurn) noexcept
{
    if (!valid_ || context_.kind != D3D8TrackingContextKind::Infantry)
    {
        ResetArtificialTurnState(
            artificialTurn.cumulativeIntentMillidegrees);
        return;
    }
    if (!artificialTurn.infantryBodyYawValid ||
        !std::isfinite(artificialTurn.infantryBodyYawRadians))
    {
        if (artificialTurnInitialized_ &&
            std::fabs(artificialTurnLeadRadians_) > 0.0F)
        {
            float referenceYaw = 0.0F;
            if (ExtractYaw(baseReference_, referenceYaw))
            {
                SetYaw(
                    baseReference_,
                    referenceYaw - artificialTurnLeadRadians_);
            }
        }
        ResetArtificialTurnState(
            artificialTurn.cumulativeIntentMillidegrees);
        return;
    }
    if (!artificialTurnInitialized_)
    {
        consumedArtificialTurnMillidegrees_ =
            artificialTurn.cumulativeIntentMillidegrees;
        observedInfantryBodyYawRadians_ =
            WrapYaw(artificialTurn.infantryBodyYawRadians);
        artificialTurnLeadRadians_ = 0.0F;
        artificialTurnInitialized_ = true;
        return;
    }

    const std::int32_t intentDeltaMillidegrees = WrappedMillidegreeDelta(
        artificialTurn.cumulativeIntentMillidegrees,
        consumedArtificialTurnMillidegrees_);
    consumedArtificialTurnMillidegrees_ =
        artificialTurn.cumulativeIntentMillidegrees;
    const std::int64_t intentDeltaMagnitude =
        intentDeltaMillidegrees < 0
        ? -static_cast<std::int64_t>(intentDeltaMillidegrees)
        : static_cast<std::int64_t>(intentDeltaMillidegrees);
    if (intentDeltaMagnitude > kMaximumArtificialTurnDeltaMillidegrees)
    {
        ResetArtificialTurnState(
            artificialTurn.cumulativeIntentMillidegrees);
        observedInfantryBodyYawRadians_ =
            WrapYaw(artificialTurn.infantryBodyYawRadians);
        artificialTurnInitialized_ = true;
        return;
    }

    constexpr float kMillidegreesToRadians =
        kPi / (180.0F * 1000.0F);
    const float requestedDelta =
        static_cast<float>(intentDeltaMillidegrees) *
        kMillidegreesToRadians;
    const float bodyYaw = WrapYaw(artificialTurn.infantryBodyYawRadians);
    const float bodyDelta = WrapYaw(
        bodyYaw - observedInfantryBodyYawRadians_);
    observedInfantryBodyYawRadians_ = bodyYaw;

    artificialTurnLeadRadians_ += requestedDelta;
    float consumedBodyDelta = 0.0F;
    if (artificialTurnLeadRadians_ != 0.0F && bodyDelta != 0.0F &&
        std::signbit(artificialTurnLeadRadians_) == std::signbit(bodyDelta))
    {
        consumedBodyDelta = std::copysign(
            (std::min)(
                std::fabs(artificialTurnLeadRadians_),
                std::fabs(bodyDelta)),
            artificialTurnLeadRadians_);
        artificialTurnLeadRadians_ -= consumedBodyDelta;
    }
    if (intentDeltaMillidegrees == 0 &&
        std::fabs(artificialTurnLeadRadians_) <=
            kArtificialTurnSettleRadians)
    {
        consumedBodyDelta += artificialTurnLeadRadians_;
        artificialTurnLeadRadians_ = 0.0F;
    }

    // OpenXR +Y yaw has the opposite horizontal sign from BF1942's D3D8
    // body yaw after the Z-axis conversion. Advancing the neutral reference
    // by the requested D3D8 turn therefore produces an equal immediate turn
    // in the rebased HMD/controllers. As the root soldier catches up, moving
    // the reference back by that body delta prevents a second visible turn.
    float referenceYaw = 0.0F;
    if (ExtractYaw(baseReference_, referenceYaw))
    {
        SetYaw(
            baseReference_,
            referenceYaw + requestedDelta - consumedBodyDelta);
    }
}

void D3D8TrackingAnchor::Update(
    const D3D8RuntimeView& currentHead,
    bool headTracked,
    D3D8TrackingContext context,
    std::int64_t predictedDisplayTime,
    LONG recenterForwardSequence,
    bool standingMode,
    bool standingHeightValid,
    float standingHeightMeters,
    float calibratedStandingHeightMeters,
    float manualHeightAdjustmentMeters,
    D3D8ArtificialTurnInput artificialTurn) noexcept
{
    manualHeightAdjustmentMeters_ = std::clamp(
        std::isfinite(manualHeightAdjustmentMeters)
            ? manualHeightAdjustmentMeters
            : 0.0F,
        -0.30F,
        0.30F);
    if (!headTracked)
    {
        return;
    }

    const bool concreteContext =
        context.kind != D3D8TrackingContextKind::Unavailable;
    if (!valid_)
    {
        Capture(currentHead, context);
    }
    else if (!concreteContext)
    {
        // A failed ownership read is not a new camera context. Requiring the
        // next concrete value to start a fresh stability window prevents a
        // partial BF1942 enter/exit transaction from redefining head zero.
        ClearPendingContext();
    }
    else if (context.kind == context_.kind && context.token == context_.token)
    {
        ClearPendingContext();
    }
    else
    {
        const bool sameCandidate =
            context.kind == pendingContext_.kind &&
            context.token == pendingContext_.token;
        if (!sameCandidate)
        {
            pendingContext_ = context;
            pendingContextSamples_ = 1;
        }
        else if (pendingContextSamples_ < kContextStabilitySamples)
        {
            ++pendingContextSamples_;
        }

        // BF1942 updates player/control/camera ownership as a transaction.
        // During vehicle entry those pointers may expose transient values.
        // Capturing every value makes inverse(currentHead) * currentHead the
        // camera delta on consecutive frames, suppressing head motion and
        // pinning the image to the headset. Keep the established anchor live
        // until one concrete context survives several render requests, then
        // perform exactly one intended neutral-pose handoff.
        if (pendingContextSamples_ >= kContextStabilitySamples)
        {
            Capture(currentHead, pendingContext_);
        }
    }

    UpdateArtificialTurn(artificialTurn);

    if (valid_ && context_.kind == D3D8TrackingContextKind::Infantry)
    {
        if (!infantryModeInitialized_ || standingMode_ != standingMode)
        {
            standingMode_ = standingMode;
            infantryModeInitialized_ = true;
            standingReferenceValid_ = false;
            if (std::isfinite(currentHead.positionY))
            {
                // Entering Seated always treats the player's current physical
                // posture as neutral, including a mid-session Standing->Seated
                // switch or a seated start after a standing session. It is
                // also the safe neutral fallback if STAGE is unavailable when
                // entering Standing.
                baseReference_.positionY = currentHead.positionY;
            }
            seatedPostureTransitionActive_ = false;
            seatedDescentObserved_ = false;
            lastSeatedVerticalMotionTime_ = predictedDisplayTime;
            if (!standingMode && standingHeightValid &&
                std::isfinite(standingHeightMeters) &&
                std::isfinite(calibratedStandingHeightMeters) &&
                calibratedStandingHeightMeters >= 0.50F &&
                calibratedStandingHeightMeters <= 2.50F &&
                standingHeightMeters > calibratedStandingHeightMeters -
                    kClearlySeatedBelowStandingMeters)
            {
                // The user selected Seated while still physically standing.
                // Keep the neutral Y following the ensuing sit-down motion,
                // then lock it after the headset has settled. This makes the
                // mode change independent of whether the user sits before or
                // after pressing Save.
                seatedPostureTransitionActive_ = true;
                lastSeatedStageHeightMeters_ = standingHeightMeters;
            }
        }

        if (standingMode && standingHeightValid &&
            std::isfinite(standingHeightMeters) &&
            standingHeightMeters >= 0.20F &&
            standingHeightMeters <= 3.0F &&
            std::isfinite(currentHead.positionY))
        {
            // BF1942 already places its logical camera at authored eye height.
            // Derive the immutable LOCAL-space floor from the simultaneous
            // STAGE measurement, then use a 1.70-m eye reference. The rebased
            // delta becomes (live floor-to-head - 1.70 m), never the user's
            // whole height added on top of Battlefield's camera.
            standingReferenceY_ =
                currentHead.positionY - standingHeightMeters +
                kBattlefieldStandingEyeHeightMeters;
            standingReferenceValid_ = true;
        }
        else if (!standingMode && seatedPostureTransitionActive_ &&
            std::isfinite(currentHead.positionY))
        {
            baseReference_.positionY = currentHead.positionY;
            if (standingHeightValid && std::isfinite(standingHeightMeters))
            {
                const float stageDelta = standingHeightMeters -
                    lastSeatedStageHeightMeters_;
                if (stageDelta < -kSeatedVerticalMotionThresholdMeters)
                {
                    seatedDescentObserved_ = true;
                }
                if (std::fabs(stageDelta) >=
                    kSeatedVerticalMotionThresholdMeters)
                {
                    lastSeatedVerticalMotionTime_ = predictedDisplayTime;
                }
                lastSeatedStageHeightMeters_ = standingHeightMeters;
                const bool settledAfterDescent = seatedDescentObserved_ &&
                    predictedDisplayTime > lastSeatedVerticalMotionTime_ &&
                    predictedDisplayTime - lastSeatedVerticalMotionTime_ >=
                        kSeatedSettleDurationNanoseconds;
                if (settledAfterDescent)
                {
                    seatedPostureTransitionActive_ = false;
                }
            }
        }
    }

    if (recenterForwardSequence > 0 &&
        recenterForwardSequence != consumedRecenterSequence_ && valid_)
    {
        float yaw = 0.0F;
        if (ExtractYaw(currentHead, yaw))
        {
            baseReference_.positionX = currentHead.positionX;
            baseReference_.positionZ = currentHead.positionZ;
            if (context_.kind == D3D8TrackingContextKind::Seat ||
                context_.kind == D3D8TrackingContextKind::Unavailable)
            {
                baseReference_.positionY = currentHead.positionY;
            }
            SetYaw(
                baseReference_,
                yaw + artificialTurnLeadRadians_);
            consumedRecenterSequence_ = recenterForwardSequence;
        }
    }
}

D3D8RuntimeView D3D8TrackingAnchor::ReferenceHead(
    const D3D8RuntimeView& fallbackHead) const noexcept
{
    if (!valid_)
    {
        return fallbackHead;
    }
    D3D8RuntimeView result = baseReference_;
    if (context_.kind == D3D8TrackingContextKind::Infantry)
    {
        if (standingMode_ && standingReferenceValid_)
        {
            result.positionY = standingReferenceY_;
        }
        result.positionY -= manualHeightAdjustmentMeters_;
    }
    return result;
}

D3D8RuntimeView D3D8TrackingAnchor::RebaseView(
    const D3D8RuntimeView& view) const noexcept
{
    if (!valid_)
    {
        return view;
    }
    const auto relative = stereo::MakeRelativePose(
        ToPose(ReferenceHead(view)), ToPose(view));
    return relative.has_value() ? FromPose(*relative, view) : view;
}

D3D8RuntimeControllerSample D3D8TrackingAnchor::RebaseControllerSample(
    const D3D8RuntimeControllerSample& sample) const noexcept
{
    if (!valid_)
    {
        return sample;
    }
    D3D8RuntimeControllerSample result = sample;
    const D3D8RuntimeView reference = ReferenceHead({});
    for (D3D8RuntimeControllerHand& hand : result.hands)
    {
        hand.aimPose = RebaseControllerPose(reference, hand.aimPose);
        hand.gripPose = RebaseControllerPose(reference, hand.gripPose);
    }
    return result;
}

D3D8TrackingContext D3D8TrackingAnchor::Context() const noexcept
{
    return context_;
}

std::uint32_t D3D8TrackingAnchor::ContextGeneration() const noexcept
{
    return contextGeneration_;
}

bool D3D8TrackingAnchor::IsValid() const noexcept
{
    return valid_;
}

bool D3D8TrackingAnchor::IsSeatedPostureTransitionActive() const noexcept
{
    return seatedPostureTransitionActive_;
}

} // namespace bfvr
