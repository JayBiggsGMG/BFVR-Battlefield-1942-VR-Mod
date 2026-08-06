#include "stereo/SurfaceVehicleDriveMath.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr float kHalfPi = 1.570796327F;
}

namespace bfvr::stereo
{

SurfaceVehicleDriveInput MapSurfaceVehicleDrive(
    float stickX,
    float stickY,
    const SurfaceVehicleDriveConfiguration& configuration) noexcept
{
    if (!std::isfinite(stickX) || !std::isfinite(stickY) ||
        !std::isfinite(configuration.axisDeadzone) ||
        !std::isfinite(configuration.fullSteeringAngleRadians) ||
        !std::isfinite(configuration.steeringResponseExponent) ||
        configuration.axisDeadzone < 0.0F ||
        configuration.axisDeadzone >= 1.0F ||
        configuration.fullSteeringAngleRadians <= 0.0F ||
        configuration.fullSteeringAngleRadians > kHalfPi ||
        configuration.steeringResponseExponent <= 0.0F)
    {
        return {};
    }

    const float clampedX = std::clamp(stickX, -1.0F, 1.0F);
    const float clampedY = std::clamp(stickY, -1.0F, 1.0F);
    const float fullSteeringX =
        std::sin(configuration.fullSteeringAngleRadians);
    if (fullSteeringX <= configuration.axisDeadzone)
    {
        return {};
    }

    SurfaceVehicleDriveInput output = {};
    const float steeringMagnitude = std::fabs(clampedX);
    if (steeringMagnitude > configuration.axisDeadzone)
    {
        const float steeringProgress = std::clamp(
            (steeringMagnitude - configuration.axisDeadzone) /
                (fullSteeringX - configuration.axisDeadzone),
            0.0F,
            1.0F);
        output.steering = std::copysign(
            std::pow(
                steeringProgress,
                configuration.steeringResponseExponent),
            clampedX);
    }

    if (std::fabs(clampedY) > configuration.axisDeadzone)
    {
        output.throttle = std::copysign(1.0F, clampedY);
    }
    return output;
}

} // namespace bfvr::stereo
