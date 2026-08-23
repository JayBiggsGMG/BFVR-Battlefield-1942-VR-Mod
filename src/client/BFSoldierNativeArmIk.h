#pragma once

namespace bfvr
{

// Arms-only native motion path. It is available only during the continuous
// OpenXR presentation run and only when the loader explicitly enables it. The
// implementation drives BF1942's own right-hand solver plus a separately
// restored free-left-hand target for one animation update at a time. The left
// wrist applies tracked grip rotation relative to a native per-item zero pose
// plus a reference-zeroed controller-local anatomical-wrist lever arm;
// a proximity-gated squeeze may constrain only that visual target. Primary
// slot 3 preserves the native left-to-right-hand span; close sidearm slot 2
// captures the user's current cup pose. The right controller remains the sole
// gun/fire authority. At the exact final 1P pass, BFVR temporarily owns only
// each upper-arm origin and elbow intent from a tracked-head body frame; Maya
// still solves to the controller wrist and BF1942 still supplies the hand,
// fingers, item relation, and animation state. It never supplies a mesh, hand
// asset, reload, item, projectile, startup, or runtime-selection behavior.
[[nodiscard]] bool StartBFSoldierNativeArmIk(
    void* gameImage,
    void (*appendLog)(const wchar_t* message));
void StopBFSoldierNativeArmIk();

} // namespace bfvr
