#pragma once

#include <string_view>

namespace bfvr
{

enum class D3D8RuntimeDiagnosticLevel
{
    Off,
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

[[nodiscard]] constexpr bool IsD3D8RuntimeDiagnosticsEnabled(
    D3D8RuntimeDiagnosticLevel level) noexcept
{
    return level != D3D8RuntimeDiagnosticLevel::Off;
}

// Calibration captures are sparse owner-authored data, not continuous runtime
// diagnostics. Keep this narrowly prefix-gated so an ordinary diagnostics-off
// headset run can preserve a requested socket without enabling the observer's
// high-volume diagnostic families.
[[nodiscard]] constexpr bool IsOffHandCalibrationAuditMessage(
    std::wstring_view message) noexcept
{
    constexpr std::wstring_view prefix = L"OFFHAND_CALIBRATION_";
    return message.size() >= prefix.size() &&
        message.substr(0, prefix.size()) == prefix;
}

// Projected-shadow proof is a bounded startup record, first-hit records, and a
// shutdown summary when teardown runs. Preserve only this prefix in
// diagnostics-off headset runs so visual reports can be correlated with
// whether the exact draw/state correction ran.
[[nodiscard]] constexpr bool IsProjectedShadowAuditMessage(
    std::wstring_view message) noexcept
{
    constexpr std::wstring_view prefix = L"PROJECTED_SHADOW_AUDIT ";
    return message.size() >= prefix.size() &&
        message.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] const wchar_t* DescribeD3D8RuntimeDiagnosticLevel(
    D3D8RuntimeDiagnosticLevel level) noexcept;

} // namespace bfvr
