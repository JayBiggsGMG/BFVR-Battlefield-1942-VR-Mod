#include "client/D3D8RuntimeDiagnostics.h"

#include <windows.h>

#include <array>

namespace bfvr
{
namespace
{
constexpr wchar_t kDiagnosticsEnvironment[] = L"BFVR_DIAGNOSTICS";

constexpr wchar_t LowerAscii(wchar_t value) noexcept
{
    return value >= L'A' && value <= L'Z'
        ? static_cast<wchar_t>(value - L'A' + L'a')
        : value;
}

bool EqualsAsciiInsensitive(
    std::wstring_view left,
    std::wstring_view right) noexcept
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index)
    {
        if (LowerAscii(left[index]) != LowerAscii(right[index]))
        {
            return false;
        }
    }
    return true;
}
} // namespace

D3D8RuntimeDiagnosticLevel ParseD3D8RuntimeDiagnosticLevel(
    std::wstring_view value) noexcept
{
    return EqualsAsciiInsensitive(value, L"deep") || value == L"1"
        ? D3D8RuntimeDiagnosticLevel::Deep
        : D3D8RuntimeDiagnosticLevel::Normal;
}

D3D8RuntimeDiagnosticLevel ReadD3D8RuntimeDiagnosticLevel() noexcept
{
    std::array<wchar_t, 32> value = {};
    const DWORD length = GetEnvironmentVariableW(
        kDiagnosticsEnvironment,
        value.data(),
        static_cast<DWORD>(value.size()));
    if (length == 0 || length >= value.size())
    {
        return D3D8RuntimeDiagnosticLevel::Normal;
    }
    return ParseD3D8RuntimeDiagnosticLevel(
        std::wstring_view(value.data(), length));
}

const wchar_t* DescribeD3D8RuntimeDiagnosticLevel(
    D3D8RuntimeDiagnosticLevel level) noexcept
{
    return IsDeepD3D8RuntimeDiagnostics(level) ? L"deep" : L"normal";
}

} // namespace bfvr
