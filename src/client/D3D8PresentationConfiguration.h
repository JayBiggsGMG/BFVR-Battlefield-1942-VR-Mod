#pragma once

namespace bfvr
{

enum class D3D8PresentationScaleSource
{
    Default,
    Environment,
    InvalidEnvironment
};

enum class D3D8NativeFirstPersonArmsSource
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
    // Native game-selected first-person arms are part of the default VR
    // presentation. Set BFVR_NATIVE_1P_ARMS=0 only to opt out.
    bool nativeFirstPersonArmsEnabled = true;
    D3D8NativeFirstPersonArmsSource nativeFirstPersonArmsSource =
        D3D8NativeFirstPersonArmsSource::Default;
};

[[nodiscard]] D3D8PresentationConfiguration
ReadD3D8PresentationConfiguration() noexcept;

} // namespace bfvr
