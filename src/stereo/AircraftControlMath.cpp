#include "stereo/AircraftControlMath.h"

#include <cmath>

namespace bfvr::stereo
{

AircraftControlInput MapAircraftControlInput(
    float leftStickX,
    float leftStickY,
    float rightStickX,
    float rightStickY,
    bool pitchWithRoll,
    bool swapSticks) noexcept
{
    if (!std::isfinite(leftStickX) || !std::isfinite(leftStickY) ||
        !std::isfinite(rightStickX) || !std::isfinite(rightStickY))
    {
        return {};
    }
    const float pitchStickX = swapSticks ? leftStickX : rightStickX;
    const float pitchStickY = swapSticks ? leftStickY : rightStickY;
    const float throttleStickX = swapSticks ? rightStickX : leftStickX;
    const float throttleStickY = swapSticks ? rightStickY : leftStickY;
    return pitchWithRoll
        ? AircraftControlInput{
              pitchStickX,
              throttleStickX,
              throttleStickY,
              pitchStickY}
        : AircraftControlInput{
              throttleStickX,
              pitchStickX,
              throttleStickY,
              pitchStickY};
}

} // namespace bfvr::stereo
