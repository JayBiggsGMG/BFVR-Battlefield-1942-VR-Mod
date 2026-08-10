#include "stereo/InfantryAuthoritativeAim.h"

#include <algorithm>
#include <cmath>

namespace
{

constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = kPi * 2.0F;

float NormalizeRadians(float radians) noexcept
{
    radians = std::remainder(radians, kTwoPi);
    if (radians <= -kPi)
    {
        radians += kTwoPi;
    }
    else if (radians > kPi)
    {
        radians -= kTwoPi;
    }
    return radians;
}

float ApplyDeadzone(const float value, const float deadzone) noexcept
{
    return std::fabs(value) < deadzone ? 0.0F : value;
}

bool IsConfigurationValid(
    const bfvr::stereo::InfantryAuthoritativeAimConfiguration& value) noexcept
{
    return std::isfinite(value.inputPerRadian) &&
        value.inputPerRadian > 0.0F &&
        std::isfinite(value.deadzoneRadians) &&
        value.deadzoneRadians >= 0.0F &&
        std::isfinite(value.maximumAxis) && value.maximumAxis > 0.0F &&
        std::isfinite(value.minimumHorizontalForward) &&
        value.minimumHorizontalForward >= 0.0F;
}

} // namespace

namespace bfvr::stereo
{

void ResetInfantryAuthoritativeAim(
    InfantryAuthoritativeAimState& state) noexcept
{
    state = {};
}

InfantryAuthoritativeAimOutput UpdateInfantryAuthoritativeAim(
    InfantryAuthoritativeAimState& state,
    const bool enabled,
    const bool targetTracked,
    const Vec3& targetForwardWorld,
    const float currentYawRadians,
    const float currentPitchRadians,
    const std::uintptr_t infantryLifetime,
    const std::uintptr_t itemLifetime,
    const std::uint64_t controllerSequence,
    const InfantryAuthoritativeAimConfiguration& configuration) noexcept
{
    InfantryAuthoritativeAimOutput output = {};
    const float forwardLengthSquared =
        targetForwardWorld.x * targetForwardWorld.x +
        targetForwardWorld.y * targetForwardWorld.y +
        targetForwardWorld.z * targetForwardWorld.z;
    if (!enabled || !targetTracked || infantryLifetime == 0 ||
        itemLifetime == 0 || controllerSequence == 0 ||
        !std::isfinite(forwardLengthSquared) ||
        forwardLengthSquared < 0.25F || forwardLengthSquared > 2.25F ||
        !std::isfinite(currentYawRadians) ||
        !std::isfinite(currentPitchRadians) ||
        !IsConfigurationValid(configuration))
    {
        ResetInfantryAuthoritativeAim(state);
        return output;
    }

    const float inverseLength = 1.0F / std::sqrt(forwardLengthSquared);
    const float forwardX = targetForwardWorld.x * inverseLength;
    const float forwardY = targetForwardWorld.y * inverseLength;
    const float forwardZ = targetForwardWorld.z * inverseLength;
    const float horizontalLength = std::hypot(forwardX, forwardZ);
    if (!std::isfinite(horizontalLength))
    {
        ResetInfantryAuthoritativeAim(state);
        return output;
    }

    output.targetAccepted = true;
    output.currentYawRadians = NormalizeRadians(currentYawRadians);
    output.currentPitchRadians = currentPitchRadians;
    // Exact vertical aim has no unique yaw. Preserve the current native yaw
    // rather than injecting arbitrary horizontal motion near the pole.
    output.targetYawRadians =
        horizontalLength >= configuration.minimumHorizontalForward
        ? NormalizeRadians(std::atan2(forwardX, forwardZ))
        : output.currentYawRadians;
    output.targetPitchRadians = std::atan2(forwardY, horizontalLength);
    if (!std::isfinite(output.targetYawRadians) ||
        !std::isfinite(output.targetPitchRadians))
    {
        ResetInfantryAuthoritativeAim(state);
        return {};
    }

    if (!state.valid || state.infantryLifetime != infantryLifetime ||
        state.itemLifetime != itemLifetime)
    {
        state.infantryLifetime = infantryLifetime;
        state.itemLifetime = itemLifetime;
        state.controllerSequence = controllerSequence;
        state.valid = true;
        output.lifetimeCaptured = true;
        return output;
    }
    if (state.controllerSequence == controllerSequence)
    {
        return output;
    }
    state.controllerSequence = controllerSequence;

    output.yawErrorRadians = NormalizeRadians(
        output.targetYawRadians - output.currentYawRadians);
    output.pitchErrorRadians =
        output.targetPitchRadians - output.currentPitchRadians;
    const float yawCorrection = ApplyDeadzone(
        output.yawErrorRadians,
        configuration.deadzoneRadians);
    const float pitchCorrection = ApplyDeadzone(
        output.pitchErrorRadians,
        configuration.deadzoneRadians);

    // D3D8 world yaw and BF1942 mouse-look X share their sign. BF1942's
    // mouse-look Y sign is opposite a D3D8 +Y/up target pitch.
    output.mouseLookX = std::clamp(
        yawCorrection * configuration.inputPerRadian,
        -configuration.maximumAxis,
        configuration.maximumAxis);
    output.mouseLookY = std::clamp(
        -pitchCorrection * configuration.inputPerRadian,
        -configuration.maximumAxis,
        configuration.maximumAxis);
    return output;
}

} // namespace bfvr::stereo
