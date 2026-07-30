#pragma once

namespace bfvr::stereo
{

// The native first-person arms share BF1942's AnimatedMesh skinning route.
// This policy governs only whether an already-classified arm draw is omitted
// from the stereo replay. It does not identify game objects or alter them.
[[nodiscard]] bool ShouldSuppressBF1942FirstPersonArmDraw(
    bool presentationMode,
    bool firstPersonArmDraw,
    bool nativeFirstPersonArmsEnabled) noexcept;

} // namespace bfvr::stereo
