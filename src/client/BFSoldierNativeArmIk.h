#pragma once

namespace bfvr
{

// Arms-only native motion path. It is available only during the continuous
// OpenXR presentation run and only when the loader explicitly enables it. The
// implementation drives BF1942's own right-hand two-bone solver for one
// animation update at a time; it never supplies a mesh, hand asset, reload,
// item, or projectile implementation.
[[nodiscard]] bool StartBFSoldierNativeArmIk(
    void* gameImage,
    void (*appendLog)(const wchar_t* message));
void StopBFSoldierNativeArmIk();

} // namespace bfvr
