#pragma once

#include "settings/UserSettings.h"

#include <cstdint>

namespace bfvr::stereo
{

// Bright base tints for the existing premultiplied grayscale crosshair art.
[[nodiscard]] constexpr std::uint32_t CrosshairTintArgb(
    settings::CrosshairColor color,
    std::uint32_t opacityPercent =
        settings::kDefaultCrosshairOpacityPercent) noexcept
{
    const std::uint32_t clampedOpacity = opacityPercent <
            settings::kMinimumCrosshairOpacityPercent
        ? settings::kMinimumCrosshairOpacityPercent
        : opacityPercent > settings::kMaximumCrosshairOpacityPercent
        ? settings::kMaximumCrosshairOpacityPercent
        : opacityPercent;
    const std::uint32_t alpha =
        (clampedOpacity * 255U + 50U) / 100U;
    std::uint32_t rgb = 0;
    switch (color)
    {
    case settings::CrosshairColor::White: rgb = 0x00FFFFFFU; break;
    case settings::CrosshairColor::Blue: rgb = 0x0040A0FFU; break;
    case settings::CrosshairColor::Purple: rgb = 0x009A50FFU; break;
    case settings::CrosshairColor::Red: rgb = 0x00FF4040U; break;
    case settings::CrosshairColor::Pink: rgb = 0x00FF69B4U; break;
    case settings::CrosshairColor::Orange: rgb = 0x00FF8A30U; break;
    case settings::CrosshairColor::Yellow: rgb = 0x00FFFF00U; break;
    case settings::CrosshairColor::Green:
    default: rgb = 0x0006FF00U; break;
    }
    const auto scaleChannel = [alpha](std::uint32_t channel) constexpr {
        return (channel * alpha + 127U) / 255U;
    };
    const std::uint32_t red = scaleChannel((rgb >> 16U) & 0xFFU);
    const std::uint32_t green = scaleChannel((rgb >> 8U) & 0xFFU);
    const std::uint32_t blue = scaleChannel(rgb & 0xFFU);
    return (alpha << 24U) | (red << 16U) | (green << 8U) | blue;
}

} // namespace bfvr::stereo
