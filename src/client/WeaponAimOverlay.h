#pragma once

namespace bfvr
{

// Observes BF1942's accepted local weapon-fire boundary for haptics and recoil.
// During the infantry-authority proof it forwards every native fire matrix
// unchanged so gameplay aim, projectile creation, and networking remain one
// stock pipeline in both offline and multiplayer sessions.
void StartWeaponAimOverlay(
    void* gameImage,
    void (*appendLog)(const wchar_t* message));
void StopWeaponAimOverlay();

} // namespace bfvr
