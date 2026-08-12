#pragma once

namespace bfvr::stereo
{

struct AircraftControlInput
{
    float roll = 0.0F;
    float yaw = 0.0F;
    float throttle = 0.0F;
    float pitch = 0.0F;
};

// Routes all four aircraft stick axes. The first preference chooses whether
// pitch is paired with yaw or roll. The second exchanges the complete pitch
// and throttle stick roles, producing all four handed/grouping combinations.
[[nodiscard]] AircraftControlInput MapAircraftControlInput(
    float leftStickX,
    float leftStickY,
    float rightStickX,
    float rightStickY,
    bool pitchWithRoll,
    bool swapSticks) noexcept;

} // namespace bfvr::stereo
