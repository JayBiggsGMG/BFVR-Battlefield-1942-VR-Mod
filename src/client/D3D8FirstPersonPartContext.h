#pragma once

#include "stereo/D3D8FirstPersonArmPolicy.h"

namespace bfvr
{

// Installs a forwarding hook at BF1942's verified AnimatedMesh draw boundary.
// During the original call, nested D3D8 draws can query whether the
// game-selected mesh has an explicit left/right-hand template label. The hook
// never changes the game mesh, skeleton, visibility set, or draw arguments.
[[nodiscard]] bool StartD3D8FirstPersonPartContext(
    void* gameImage,
    void (*appendLog)(const wchar_t* message)) noexcept;
void StopD3D8FirstPersonPartContext() noexcept;

[[nodiscard]] stereo::D3D8FirstPersonPartKind
ReadD3D8FirstPersonPartKind() noexcept;

} // namespace bfvr
