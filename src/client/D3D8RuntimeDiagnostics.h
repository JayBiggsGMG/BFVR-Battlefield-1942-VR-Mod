#pragma once

#include <string_view>

namespace bfvr
{

enum class D3D8RuntimeDiagnosticLevel
{
    Normal,
    Deep
};

[[nodiscard]] D3D8RuntimeDiagnosticLevel ParseD3D8RuntimeDiagnosticLevel(
    std::wstring_view value) noexcept;

[[nodiscard]] D3D8RuntimeDiagnosticLevel ReadD3D8RuntimeDiagnosticLevel() noexcept;

[[nodiscard]] constexpr bool IsDeepD3D8RuntimeDiagnostics(
    D3D8RuntimeDiagnosticLevel level) noexcept
{
    return level == D3D8RuntimeDiagnosticLevel::Deep;
}

[[nodiscard]] const wchar_t* DescribeD3D8RuntimeDiagnosticLevel(
    D3D8RuntimeDiagnosticLevel level) noexcept;

} // namespace bfvr
