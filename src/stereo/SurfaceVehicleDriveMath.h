#pragma once

namespace bfvr::stereo
{

struct SurfaceVehicleDriveConfiguration
{
    float axisDeadzone = 0.20F;
    float fullSteeringAngleRadians = 1.047197551F; // 60 degrees: 10/2 o'clock
    float steeringResponseExponent = 2.0F;
};

struct SurfaceVehicleDriveInput
{
    float steering = 0.0F;
    float throttle = 0.0F;
};

// Maps the left stick specifically for BF1942 surface vehicles. Throttle
// retains the proven keyboard-equivalent full forward/reverse command outside
// its deadzone. Steering rises smoothly from the forward axis and reaches full
// strength at the configured clock angle, allowing full throttle and full
// steering together at 10/2 o'clock. Aircraft do not use this mapping.
[[nodiscard]] SurfaceVehicleDriveInput MapSurfaceVehicleDrive(
    float stickX,
    float stickY,
    const SurfaceVehicleDriveConfiguration& configuration = {}) noexcept;

} // namespace bfvr::stereo
