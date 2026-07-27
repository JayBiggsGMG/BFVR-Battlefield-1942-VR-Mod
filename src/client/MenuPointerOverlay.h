#pragma once

#include <windows.h>

namespace bfvr
{

// Routes the fresh right-hand OpenXR aim pose through BF1942's native
// BfMenu::setGameInput boundary. It does not synthesize a Windows cursor or
// window message; Ref2 continues to own hit testing, focus, and click dispatch.
void StartMenuPointerOverlay(
    void* gameImage,
    UINT runtimeUiWidth,
    UINT runtimeUiHeight,
    UINT sourceUiWidth,
    UINT sourceUiHeight,
    void (*appendLog)(const wchar_t* message));
void StopMenuPointerOverlay();
[[nodiscard]] bool IsMenuPointerOverlayActive() noexcept;

} // namespace bfvr
