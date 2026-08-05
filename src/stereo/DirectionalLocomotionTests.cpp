#include "stereo/DirectionalLocomotion.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
constexpr float kDegreesToRadians = 0.01745329251994329577F;

bool Is(
    const bfvr::stereo::DigitalLocomotionDirection& value,
    int horizontal,
    int forward) noexcept
{
    return value.horizontal == horizontal && value.forward == forward;
}
} // namespace

int wmain()
{
    bfvr::stereo::DigitalLocomotionState state;
    if (!Is(bfvr::stereo::QuantizeDigitalLocomotion(
                0.0F, 1.0F, 0.2F * kDegreesToRadians, state), 0, 1) ||
        !Is(bfvr::stereo::QuantizeDigitalLocomotion(
                0.0F, 1.0F, -0.3F * kDegreesToRadians, state), 0, 1))
    {
        std::fwprintf(stderr, L"[FAIL] forward tracking jitter produced strafe\n");
        return 1;
    }

    if (!Is(bfvr::stereo::QuantizeDigitalLocomotion(
                0.0F, 1.0F, 30.0F * kDegreesToRadians, state), 1, 1) ||
        !Is(bfvr::stereo::QuantizeDigitalLocomotion(
                0.0F, 1.0F, 20.0F * kDegreesToRadians, state), 1, 1) ||
        !Is(bfvr::stereo::QuantizeDigitalLocomotion(
                0.0F, 1.0F, 15.0F * kDegreesToRadians, state), 0, 1))
    {
        std::fwprintf(stderr, L"[FAIL] directional sector hysteresis failed\n");
        return 1;
    }

    bfvr::stereo::ResetDigitalLocomotion(state);
    if (!Is(bfvr::stereo::QuantizeDigitalLocomotion(
                1.0F, 0.0F, 0.0F, state), 1, 0) ||
        !Is(bfvr::stereo::QuantizeDigitalLocomotion(
                0.0F, -1.0F, 0.0F, state), 0, -1))
    {
        std::fwprintf(stderr, L"[FAIL] cardinal directions were not preserved\n");
        return 1;
    }

    const float nan = std::numeric_limits<float>::quiet_NaN();
    if (!Is(bfvr::stereo::QuantizeDigitalLocomotion(
                0.0F, 1.0F, nan, state), 0, 0) || state.sector != -1)
    {
        std::fwprintf(stderr, L"[FAIL] invalid tracking did not fail closed\n");
        return 1;
    }

    std::wprintf(L"[PASS] Digital locomotion uses stable eight-way sectors with jitter hysteresis.\n");
    return 0;
}
