#pragma once

#include "client/D3D8SharedPresentationBridge.h"

#include <cstdint>

namespace bfvr
{

enum class D3D8TrackingContextKind : std::uint32_t
{
    Unavailable = 0,
    Infantry,
    Seat
};

struct D3D8TrackingContext
{
    D3D8TrackingContextKind kind = D3D8TrackingContextKind::Unavailable;
    std::uintptr_t token = 0;
};

// Keeps OpenXR's runtime-owned tracking origin immutable and supplies a
// local, context-specific neutral pose to the Battlefield camera, hands, and
// input consumers. Seated/Standing vertical placement and manual trim remain
// infantry-only and never enter a vehicle seat.
class D3D8TrackingAnchor
{
public:
    void Reset() noexcept;
    void Update(
        const D3D8RuntimeView& currentHead,
        bool headTracked,
        D3D8TrackingContext context,
        std::int64_t predictedDisplayTime,
        LONG recenterForwardSequence,
        bool standingMode,
        bool standingHeightValid,
        float standingHeightMeters,
        float calibratedStandingHeightMeters,
        float manualHeightAdjustmentMeters) noexcept;

    [[nodiscard]] D3D8RuntimeView ReferenceHead(
        const D3D8RuntimeView& fallbackHead) const noexcept;
    [[nodiscard]] D3D8RuntimeView RebaseView(
        const D3D8RuntimeView& view) const noexcept;
    [[nodiscard]] D3D8RuntimeControllerSample RebaseControllerSample(
        const D3D8RuntimeControllerSample& sample) const noexcept;
    [[nodiscard]] D3D8TrackingContext Context() const noexcept;
    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] bool IsSeatedPostureTransitionActive() const noexcept;

private:
    void Capture(
        const D3D8RuntimeView& currentHead,
        D3D8TrackingContext context) noexcept;

    D3D8RuntimeView baseReference_ = {};
    D3D8TrackingContext context_ = {};
    LONG consumedRecenterSequence_ = 0;
    float standingReferenceY_ = 0.0F;
    float manualHeightAdjustmentMeters_ = 0.0F;
    float lastSeatedStageHeightMeters_ = 0.0F;
    std::int64_t lastSeatedVerticalMotionTime_ = 0;
    bool standingMode_ = false;
    bool standingReferenceValid_ = false;
    bool infantryModeInitialized_ = false;
    bool seatedPostureTransitionActive_ = false;
    bool seatedDescentObserved_ = false;
    bool valid_ = false;
};

} // namespace bfvr
