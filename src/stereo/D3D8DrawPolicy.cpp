#include "stereo/D3D8DrawPolicy.h"

#include <cmath>

namespace
{

constexpr std::uint32_t kD3DFvfPositionMask = 0x400E;
constexpr std::uint32_t kD3DFvfXyzRhw = 0x0004;
constexpr std::uint32_t kD3DVertexShaderHandleBit = 0x80000000;
constexpr float kProjectionTolerance = 0.0001F;

bool IsFinite(const bfvr::stereo::Matrix4& matrix) noexcept
{
    for (const auto& row : matrix.values)
    {
        for (float element : row)
        {
            if (!std::isfinite(element))
            {
                return false;
            }
        }
    }
    return true;
}

bool IsPretransformedFvf(std::uint32_t vertexShaderOrFvf) noexcept
{
    return (vertexShaderOrFvf & kD3DVertexShaderHandleBit) == 0 &&
        (vertexShaderOrFvf & kD3DFvfPositionMask) == kD3DFvfXyzRhw;
}

bool IsPerspectiveProjection(const bfvr::stereo::Matrix4& projection) noexcept
{
    return IsFinite(projection) &&
        std::fabs(projection.values[2][3]) > kProjectionTolerance &&
        std::fabs(projection.values[3][3]) <= kProjectionTolerance;
}

} // namespace

namespace bfvr::stereo
{

D3D8DrawPolicy ClassifyD3D8DrawPolicy(
    std::uint32_t vertexShaderOrFvf,
    const Matrix4& projection) noexcept
{
    if (IsPretransformedFvf(vertexShaderOrFvf))
    {
        return D3D8DrawPolicy::MonoPretransformed;
    }
    return IsPerspectiveProjection(projection)
        ? D3D8DrawPolicy::StereoPerspective
        : D3D8DrawPolicy::MonoNonPerspective;
}

bool UsesStereoTransforms(D3D8DrawPolicy policy) noexcept
{
    return policy == D3D8DrawPolicy::StereoPerspective;
}

} // namespace bfvr::stereo
