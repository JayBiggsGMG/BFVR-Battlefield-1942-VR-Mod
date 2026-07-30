#include "client/D3D8PresentationConfiguration.h"

#include <windows.h>

#include <array>
#include <cmath>
#include <cwchar>

namespace bfvr
{

namespace
{

D3D8NativeFirstPersonArmsSource ReadNativeFirstPersonArmsConfiguration(
    bool& enabled) noexcept
{
    constexpr wchar_t kVariableName[] = L"BFVR_NATIVE_1P_ARMS";
    std::array<wchar_t, 4> buffer = {};
    const DWORD length = GetEnvironmentVariableW(
        kVariableName,
        buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if (length == 0)
    {
        return D3D8NativeFirstPersonArmsSource::Default;
    }
    if (length != 1)
    {
        return D3D8NativeFirstPersonArmsSource::InvalidEnvironment;
    }
    if (buffer[0] == L'0')
    {
        enabled = false;
        return D3D8NativeFirstPersonArmsSource::Environment;
    }
    if (buffer[0] == L'1')
    {
        enabled = true;
        return D3D8NativeFirstPersonArmsSource::Environment;
    }
    return D3D8NativeFirstPersonArmsSource::InvalidEnvironment;
}

} // namespace

D3D8PresentationConfiguration
ReadD3D8PresentationConfiguration() noexcept
{
    constexpr wchar_t kVariableName[] =
        L"BFVR_OPENXR_WORLD_RENDER_SCALE";
    D3D8PresentationConfiguration configuration = {};
    configuration.nativeFirstPersonArmsSource =
        ReadNativeFirstPersonArmsConfiguration(
            configuration.nativeFirstPersonArmsEnabled);
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
