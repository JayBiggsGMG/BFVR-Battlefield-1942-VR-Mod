#pragma once

namespace bfvr
{

enum class D3D8PresentationScaleSource
{
    Default,
    Environment,
    InvalidEnvironment
};

struct D3D8PresentationConfiguration
{
    float worldRenderScale = 0.75F;
    D3D8PresentationScaleSource scaleSource =
        D3D8PresentationScaleSource::Default;
};

[[nodiscard]] D3D8PresentationConfiguration
ReadD3D8PresentationConfiguration() noexcept;

} // namespace bfvr
