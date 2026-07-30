#pragma once

namespace bfvr
{

// Arms-only native motion path. It is available only during the continuous
// OpenXR presentation run and only when the loader explicitly enables it. The
// implementation drives BF1942's own right-hand solver plus a separately
// restored free-left-hand target for one animation update at a time. The left
// wrist applies tracked grip rotation relative to a native per-item zero pose;
// a proximity-gated squeeze may constrain only that visual target. Primary
// slot 3 preserves the native left-to-right-hand span; close sidearm slot 2
// captures the user's current cup pose. The right controller remains the sole
// gun/fire authority. Elbow/pole correction remains separate. It never supplies
// a mesh, hand asset, reload, item, or projectile implementation.
[[nodiscard]] bool StartBFSoldierNativeArmIk(
    void* gameImage,
    void (*appendLog)(const wchar_t* message));
void StopBFSoldierNativeArmIk();

} // namespace bfvr
