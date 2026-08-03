#pragma once

#include <cstdint>

namespace bfvr::audio
{
struct HrtfBufferPolicyInput
{
    bool primary = false;
    bool threeDimensional = false;
    bool panControl = false;
    std::uint16_t channels = 0;
    long pan = 0;
};

// BF1942's short interface/weapon-selection sounds are mono, non-positional
// buffers. Under DSOAL, a stale DirectSound pan value can place those sounds
// entirely in one headphone. Only that exact class is centred; stereo and 3D
// content retain the game's original channel layout and spatial parameters.
[[nodiscard]] bool ShouldCenterHrtfBuffer(
    const HrtfBufferPolicyInput& input) noexcept;
} // namespace bfvr::audio
