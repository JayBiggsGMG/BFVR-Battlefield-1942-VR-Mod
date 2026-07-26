#include "client/D3D8PresentationConfiguration.h"

#include <windows.h>

#include <array>
#include <cmath>
#include <cwchar>

namespace bfvr
{

D3D8PresentationConfiguration
ReadD3D8PresentationConfiguration() noexcept
{
    constexpr wchar_t kVariableName[] =
        L"BFVR_OPENXR_WORLD_RENDER_SCALE";
    D3D8PresentationConfiguration configuration = {};
    std::array<wchar_t, 64> buffer = {};
    const DWORD length = GetEnvironmentVariableW(
        kVariableName,
        buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if (length == 0)
    {
        return configuration;
    }
    if (length >= buffer.size())
    {
        configuration.scaleSource =
            D3D8PresentationScaleSource::InvalidEnvironment;
        return configuration;
    }

    wchar_t* parseEnd = nullptr;
    const float value = wcstof(buffer.data(), &parseEnd);
    if (parseEnd == buffer.data() ||
        parseEnd == nullptr ||
        *parseEnd != L'\0' ||
        !std::isfinite(value) ||
        value < 0.5F ||
        value > 1.25F)
    {
        configuration.scaleSource =
            D3D8PresentationScaleSource::InvalidEnvironment;
        return configuration;
    }

    configuration.worldRenderScale = value;
    configuration.scaleSource = D3D8PresentationScaleSource::Environment;
    return configuration;
}

} // namespace bfvr
