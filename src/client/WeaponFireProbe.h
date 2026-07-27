#pragma once

namespace bfvr
{

// Installs a bounded, forwarding-only observation hook on the profiled native
// weapon-fire core. It records the caller-supplied fire matrix and barrel
// index but never writes a transform, game object, projectile, or input.
void StartWeaponFireProbe(
    void* gameImage,
    void (*appendLog)(const wchar_t* message));

} // namespace bfvr
