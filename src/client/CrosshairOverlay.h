#pragma once

#include "stereo/StereoMath.h"

#include <cstdint>

namespace bfvr
{

// Suppresses BF1942's native flat crosshair through the profiled HudManager
// setter without changing global HUD state or unrelated Ref2 draw families.
void StartCrosshairOverlay(
    void* gameImage,
    void (*appendLog)(const wchar_t* message));
void StopCrosshairOverlay();

struct WorldCrosshairFrameState
{
    stereo::Vec3 endpoint = {};
    float angularDiameterDegrees = 2.0F;
    std::uint32_t tintArgb = 0xFF06FF00U;
    bool crosshairVisible = true;
    bool hitMarkerVisible = false;
};

// Samples only BF1942-owned local state. Infantry eligibility comes from the
// exact selected shooting-weapon or gadget slot published by native arm
// handling; mounted eligibility comes from BF1942's own HudManager visibility
// request plus the current PlayerControlObject weapon's authoritative firing
// transformation. Each source category has an independent display mode.
// The hit marker observes BFPlayer's existing local hit-indication timer
// without extending or mutating it.
[[nodiscard]] bool ReadWorldCrosshairFrameState(
    WorldCrosshairFrameState& state) noexcept;

} // namespace bfvr
