#pragma once

#include <cmath>
#include <cstdint>

namespace bfvr::stereo
{

struct InfantryPresentationContextInput
{
    const void* currentControlObject = nullptr;
    const void* defaultControlObject = nullptr;
    const void* cameraSoldier = nullptr;
    bool playerAlive = false;
    bool soldierParachuting = false;
};

struct InfantryPresentationContextDecision
{
    const void* soldier = nullptr;
    bool eligible = false;
    bool parachuteOverride = false;
};

[[nodiscard]] inline bool ResolveHorizontalInfantryBodyYaw(
    const float forwardX,
    const float forwardZ,
    const float rightX,
    const float rightZ,
    float& yawRadians) noexcept
{
    yawRadians = 0.0F;
    const float forwardHorizontalLength = std::hypot(forwardX, forwardZ);
    if (std::isfinite(forwardHorizontalLength) &&
        forwardHorizontalLength >= 0.5F)
    {
        yawRadians = std::atan2(forwardX, forwardZ);
        return std::isfinite(yawRadians);
    }

    // A steep parachute pose can point the forward basis toward vertical.
    // The orthogonal right basis retains the same horizontal heading.
    const float rightHorizontalLength = std::hypot(rightX, rightZ);
    if (!std::isfinite(rightHorizontalLength) ||
        rightHorizontalLength < 0.5F)
    {
        return false;
    }
    yawRadians = std::atan2(-rightZ, rightX);
    return std::isfinite(yawRadians);
}

// Keeps the ordinary current==default infantry boundary. The sole exception
// is BF1942's own local camera soldier while its native parachute state is
// active. This policy selects a local presentation lifetime only; it owns no
// input, movement, physics, packet, or replicated soldier state.
[[nodiscard]] inline InfantryPresentationContextDecision
ResolveInfantryPresentationContext(
    const InfantryPresentationContextInput& input) noexcept
{
    InfantryPresentationContextDecision decision = {};
    if (!input.playerAlive || input.defaultControlObject == nullptr)
    {
        return decision;
    }
    if (input.currentControlObject == input.defaultControlObject)
    {
        decision.soldier = input.defaultControlObject;
        decision.eligible = true;
        return decision;
    }
    if (input.currentControlObject != nullptr &&
        input.cameraSoldier == input.defaultControlObject &&
        input.soldierParachuting)
    {
        decision.soldier = input.defaultControlObject;
        decision.eligible = true;
        decision.parachuteOverride = true;
    }
    return decision;
}

} // namespace bfvr::stereo
