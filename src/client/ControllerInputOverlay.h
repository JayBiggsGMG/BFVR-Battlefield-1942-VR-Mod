#pragma once

namespace bfvr
{

// Installs the profiled, local-player-only native PlayerInput overlay used
// by the OpenXR presentation path. It never sends Windows input, calls a
// weapon method, or persists a game-memory value: only the current temporary
// PlayerInput frame is changed after the game has built it itself.
void StartControllerInputOverlay(
    void* gameImage,
    void (*appendLog)(const wchar_t* message));
void StopControllerInputOverlay();

} // namespace bfvr
