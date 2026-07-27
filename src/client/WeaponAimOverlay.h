#pragma once

namespace bfvr
{

// Reorients the proven local-infantry WeaponFire_Core matrix with the exact
// fresh grip-driven orientation used by the rendered weapon. The overlay is
// fail-closed and enabled only with the same development flag as weapon motion.
void StartWeaponAimOverlay(
    void* gameImage,
    void (*appendLog)(const wchar_t* message));
void StopWeaponAimOverlay();

} // namespace bfvr
