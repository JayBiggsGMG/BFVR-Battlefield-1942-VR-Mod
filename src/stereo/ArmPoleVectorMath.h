#pragma once

#include <array>
#include <optional>

namespace bfvr::stereo
{

struct ArmPoleVectorInput
{
    std::array<float, 3> shoulder = {};
    std::array<float, 3> handTarget = {};
    std::array<float, 3> previousPole = {};
    bool hasPreviousPole = false;
    bool leftArm = false;
    bool preserveNative = false;
    float maximumAngularStepRadians = 0.20943951F; // 12 degrees/XR sample
};

struct ArmPoleVectorResult
{
    std::array<float, 3> pole = {};
    bool usedPreviousPole = false;
    bool usedFallbackAxis = false;
    bool rateLimited = false;
};

// Computes one raw anatomical elbow intent in the stable shoulder/body frame.
// Maya remains the sole projector into the current shoulder-to-wrist solve
// plane. The fixed Parger direction is used only near vertical/behind-shoulder
// singularities; normal poses respond to hand position. Continuity is limited
// once per accepted XR generation by the runtime owner.
[[nodiscard]] std::optional<ArmPoleVectorResult>
ComputeArmPoleVector(const ArmPoleVectorInput& input) noexcept;

} // namespace bfvr::stereo
