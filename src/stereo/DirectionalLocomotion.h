#pragma once

namespace bfvr::stereo
{

struct DigitalLocomotionState
{
    int sector = -1;
};

struct DigitalLocomotionDirection
{
    int horizontal = 0;
    int forward = 0;
};

// Rotates a stick into the requested gravity-aligned basis, then selects one
// of eight stable BF1942 keyboard-equivalent directions. Sector hysteresis
// prevents headset/controller tracking jitter from turning a forward command
// into alternating full-strength strafes.
[[nodiscard]] DigitalLocomotionDirection QuantizeDigitalLocomotion(
    float stickX,
    float stickY,
    float rightPositiveYawRadians,
    DigitalLocomotionState& state) noexcept;

void ResetDigitalLocomotion(DigitalLocomotionState& state) noexcept;

} // namespace bfvr::stereo
