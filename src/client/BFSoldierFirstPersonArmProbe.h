#pragma once

namespace bfvr
{

// Installs a bounded, forwarding-only trace of BF1942's own local first-person
// part-visibility traversal. It observes child/marker ordering and the native
// skeleton/binding pointers; it never invokes game methods or writes game data.
void StartBFSoldierFirstPersonArmProbe(
    void* gameImage,
    void (*appendLog)(const wchar_t* message),
    void (*signalCompletion)());

} // namespace bfvr
