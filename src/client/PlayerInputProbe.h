#pragma once

namespace bfvr
{

// Starts one bounded observation-only trace of BF1942's verified 55-slot
// PlayerInputMap conversion boundary. The supplied game image must be the main
// BF1942.exe module base. The probe never authors a PlayerInput value; both
// detours forward the original function unchanged before returning.
void StartPlayerInputProbe(
    void* gameImage,
    void (*appendLog)(const wchar_t* message));

} // namespace bfvr
