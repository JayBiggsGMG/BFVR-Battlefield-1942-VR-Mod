#pragma once

#include "stereo/StereoMath.h"

#include <cstdint>

namespace bfvr::stereo
{

enum class D3D8DrawPolicy
{
    StereoPerspective,
    MonoPretransformed,
    MonoNonPerspective
};

// Classifies only transform behavior. It does not claim semantic identities
// such as sky, particle, text, or crosshair.
[[nodiscard]] D3D8DrawPolicy ClassifyD3D8DrawPolicy(
    std::uint32_t vertexShaderOrFvf,
    const Matrix4& projection) noexcept;

[[nodiscard]] bool UsesStereoTransforms(D3D8DrawPolicy policy) noexcept;

} // namespace bfvr::stereo
