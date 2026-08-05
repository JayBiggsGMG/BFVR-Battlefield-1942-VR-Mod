#include "stereo/DirectionalLocomotion.h"

#include <array>
#include <cstddef>
#include <cmath>

namespace
{
constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = 2.0F * kPi;
constexpr float kSectorWidth = kPi / 4.0F;
constexpr float kSectorHalfWidth = kSectorWidth / 2.0F;
constexpr float kSectorHysteresis = 0.087266463F; // five degrees
constexpr float kMinimumMagnitudeSquared = 0.000001F;

constexpr std::array<bfvr::stereo::DigitalLocomotionDirection, 8>
    kDirections = {{
        {0, 1},
        {1, 1},
        {1, 0},
        {1, -1},
        {0, -1},
        {-1, -1},
        {-1, 0},
        {-1, 1}}};

float WrappedAngularDistance(float first, float second) noexcept
{
    float distance = std::fabs(first - second);
    while (distance >= kTwoPi) distance -= kTwoPi;
    return distance > kPi ? kTwoPi - distance : distance;
}
} // namespace

namespace bfvr::stereo
{

DigitalLocomotionDirection QuantizeDigitalLocomotion(
    float stickX,
    float stickY,
    float rightPositiveYawRadians,
    DigitalLocomotionState& state) noexcept
{
    if (!std::isfinite(stickX) || !std::isfinite(stickY) ||
        !std::isfinite(rightPositiveYawRadians))
    {
        ResetDigitalLocomotion(state);
        return {};
    }

    const float cosine = std::cos(rightPositiveYawRadians);
    const float sine = std::sin(rightPositiveYawRadians);
    const float rotatedX = stickX * cosine + stickY * sine;
    const float rotatedY = -stickX * sine + stickY * cosine;
    if (rotatedX * rotatedX + rotatedY * rotatedY <
        kMinimumMagnitudeSquared)
    {
        ResetDigitalLocomotion(state);
        return {};
    }

    float angle = std::atan2(rotatedX, rotatedY);
    if (angle < 0.0F) angle += kTwoPi;
    const int candidate = static_cast<int>(std::floor(
        (angle + kSectorHalfWidth) / kSectorWidth)) % 8;

    if (state.sector >= 0 && state.sector < 8)
    {
        const float currentCenter =
            static_cast<float>(state.sector) * kSectorWidth;
        if (WrappedAngularDistance(angle, currentCenter) <=
            kSectorHalfWidth + kSectorHysteresis)
        {
            return kDirections[static_cast<std::size_t>(state.sector)];
        }
    }

    state.sector = candidate;
    return kDirections[static_cast<std::size_t>(candidate)];
}

void ResetDigitalLocomotion(DigitalLocomotionState& state) noexcept
{
    state = {};
}

} // namespace bfvr::stereo
