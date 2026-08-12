#pragma once

#include "stereo/StereoMath.h"

#include <windows.h>

namespace bfvr
{

// Routes the fresh right-hand OpenXR aim pose through BF1942's native
// BfMenu::setGameInput boundary. It does not synthesize a Windows cursor or
// directly post window messages; Ref2 continues to own hit testing, focus, and
// click dispatch. Exact frontend state 0 may additionally emit foreground-
// checked SendInput mouse-wheel detents from the right stick.
void StartMenuPointerOverlay(
    void* gameImage,
    UINT runtimeUiWidth,
    UINT runtimeUiHeight,
    UINT sourceUiWidth,
    UINT sourceUiHeight,
    void (*appendLog)(const wchar_t* message));
void StopMenuPointerOverlay();
[[nodiscard]] bool IsMenuPointerOverlayActive() noexcept;
// Only BfMenu state 0 is the retained Battlefield frontend. Ready/deploy and
// other native menu states contain moving translucent HUD content and must be
// rendered onto a freshly cleared Ref2 target every frame.
[[nodiscard]] bool ShouldRetainNativeMenuColor() noexcept;

struct MainMenuOverlayInteractionState
{
    bool visible = false;
    bool hovered = false;
};

// Snapshot produced by the native BfMenu input hook. Visibility is narrower
// than IsMenuPointerOverlayActive: only BfMenu state 0 (the "Battlefield"
// frontend) is admitted. Hover uses the same 800x600 rectangle as rendering.
[[nodiscard]] MainMenuOverlayInteractionState
GetMainMenuOverlayInteractionState() noexcept;
void SetMainMenuOverlayAvailable(bool available) noexcept;

// The D3D8 presentation thread owns the yaw-only LOCAL anchor for a visible
// native menu. The input hook consumes the same pose so controller rays and
// the OpenXR panel cannot diverge when comfort follow moves it.
void PublishActiveMenuWorldAnchor(const stereo::Pose& anchor) noexcept;
void ClearActiveMenuWorldAnchor() noexcept;
[[nodiscard]] bool TryGetActiveMenuWorldAnchor(
    stereo::Pose& anchor) noexcept;

} // namespace bfvr
