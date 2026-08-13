#pragma once

#include "settings/UserSettings.h"

#include <cstdint>

namespace bfvr::stereo
{

// Bright base tints for the existing premultiplied grayscale crosshair art.
// The D3D8 renderer modulates RGB only and retains each PNG's authored alpha.
[[nodiscard]] constexpr std::uint32_t CrosshairTintArgb(
    settings::CrosshairColor color) noexcept
{
    switch (color)
    {
    case settings::CrosshairColor::White: return 0xFFFFFFFFU;
    case settings::CrosshairColor::Blue: return 0xFF40A0FFU;
    case settings::CrosshairColor::Purple: return 0xFF9A50FFU;
    case settings::CrosshairColor::Red: return 0xFFFF4040U;
    case settings::CrosshairColor::Pink: return 0xFFFF69B4U;
    case settings::CrosshairColor::Orange: return 0xFFFF8A30U;
    case settings::CrosshairColor::Yellow: return 0xFFFFFF00U;
    case settings::CrosshairColor::Green:
    default: return 0xFF06FF00U;
    }
}

} // namespace bfvr::stereo
