#pragma once

#include "stereo/StereoMath.h"

#include <cstdint>
#include <optional>

namespace bfvr::stereo
{

enum class OffHandSupportState
{
    Free,
    Candidate,
    Supported,
};

struct OffHandSupportConfiguration
{
    float acquireDistanceMetres = 0.12F;
    double candidateHoldSeconds = 0.04;
};

// supportDistanceMetres is the distance from the tracked left grip to the
// selected item's predicted authored support point under the current one-hand
// gun pose. It is deliberately not controller-to-controller distance.
struct OffHandSupportSample
{
    std::uint64_t bindingId = 0;
    double timeSeconds = 0.0;
    float supportDistanceMetres = 0.0F;
    bool sessionFocused = false;
    bool leftGripTracked = false;
    bool leftGripHeld = false;
    bool supportPoseValid = false;
    bool nativeLeftHandTargetActive = false;
};

struct OffHandSupportResult
{
    OffHandSupportState state = OffHandSupportState::Free;
    bool enteredSupport = false;
    bool exitedSupport = false;
};

struct OffHandVisualSupportPose
{
    Matrix4 targetLocal = {};
    float controllerDistanceMetres = 0.0F;
};

// Reconstructs the animation-authored left-hand pose from BF1942's untouched
// left-to-right-hand relation under the exact solved controller right hand,
// then expresses it in the local soldier Skeleton frame. This function never
// changes the gun basis.
[[nodiscard]] std::optional<OffHandVisualSupportPose>
ComputeOffHandAuthoredSupportPose(
    const Matrix4& leftHandFromRightHand,
    const Matrix4& controllerRightHandWorld,
    const Matrix4& inverseSoldierWorld,
    const Matrix4& controllerLeftHandLocal,
    float worldUnitsPerMetre) noexcept;

// The current warm-up capture does not reliably expose BF1942's authored
// sidearm cup state. Before acquisition, measure the tracked left hand directly
// against the solved right hand while retaining the current left-hand target
// as the no-jump fallback candidate.
[[nodiscard]] std::optional<OffHandVisualSupportPose>
ComputeOffHandCloseSupportCandidate(
    const Matrix4& controllerRightHandWorld,
    const Matrix4& inverseSoldierWorld,
    const Matrix4& controllerLeftHandLocal,
    float worldUnitsPerMetre) noexcept;

// Captures and reapplies a close visual cup entirely in the soldier-local
// hand hierarchy. The relation affects only the left-hand target.
[[nodiscard]] std::optional<Matrix4>
CaptureOffHandCloseRelation(
    const Matrix4& controllerRightHandWorld,
    const Matrix4& inverseSoldierWorld,
    const Matrix4& controllerLeftHandLocal) noexcept;

[[nodiscard]] std::optional<OffHandVisualSupportPose>
ComputeOffHandCapturedCloseSupportPose(
    const Matrix4& leftHandFromRightHandLocal,
    const Matrix4& controllerRightHandWorld,
    const Matrix4& inverseSoldierWorld,
    const Matrix4& controllerLeftHandLocal,
    float worldUnitsPerMetre) noexcept;

class OffHandSupportPolicy final
{
public:
    explicit OffHandSupportPolicy(
        OffHandSupportConfiguration configuration = {}) noexcept;

    [[nodiscard]] OffHandSupportResult Update(
        const OffHandSupportSample& sample) noexcept;
    void Reset() noexcept;

    [[nodiscard]] OffHandSupportState State() const noexcept;

private:
    void SetFree() noexcept;

    OffHandSupportConfiguration configuration_ = {};
    OffHandSupportState state_ = OffHandSupportState::Free;
    std::uint64_t bindingId_ = 0;
    double candidateStartSeconds_ = 0.0;
};

} // namespace bfvr::stereo
