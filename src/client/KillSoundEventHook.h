#pragma once

namespace bfvr
{

// Observes the verified retail GameClient score-event boundary for remote MP
// and GameServer score boundary for SP/listen-server play. Both resolve local
// identity through PlayerManager's current-player service and forward native
// behavior unchanged.
void StartKillSoundEventHook(
    void* gameImage,
    void (*appendLog)(const wchar_t* message));
void StopKillSoundEventHook();

} // namespace bfvr
