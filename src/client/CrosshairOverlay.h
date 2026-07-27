#pragma once

namespace bfvr
{

// Suppresses BF1942's native flat crosshair through the profiled HudManager
// setter without changing global HUD state or unrelated Ref2 draw families.
void StartCrosshairOverlay(
    void* gameImage,
    void (*appendLog)(const wchar_t* message));
void StopCrosshairOverlay();

} // namespace bfvr
