#pragma once

namespace bfvr
{

// World-space head basis in BF1942 world coordinates, taken from the same
// camera-to-world transform the renderer hands to RenderView. Axes follow the
// D3D8 convention the game already uses: left-handed, +Y up, +Z forward.
struct VrListenerBasis
{
    float forwardX = 0.0F;
    float forwardY = 0.0F;
    float forwardZ = 1.0F;
    float upX = 0.0F;
    float upY = 1.0F;
    float upZ = 0.0F;
    float positionX = 0.0F;
    float positionY = 0.0F;
    float positionZ = 0.0F;
};

// Published by the render-view pose hook once per composed camera, consumed by
// the DirectSound listener hooks. BF1942 drives both from the same thread, but
// this channel does not assume that: it is a lock-free sequence-guarded
// snapshot, so a package or mod that services audio elsewhere still reads a
// coherent basis rather than a half-written one.
void PublishVrListenerBasis(const VrListenerBasis& basis) noexcept;

// Returns false when nothing has been published yet or the last publication is
// older than maxAgeMs. Callers must then leave the game's own values alone -
// menus, loading, and any non-VR path stop publishing, and a frozen audio
// basis would be worse than BF1942's own.
[[nodiscard]] bool ReadVrListenerBasis(
    VrListenerBasis& basis,
    unsigned long maxAgeMs) noexcept;

} // namespace bfvr
