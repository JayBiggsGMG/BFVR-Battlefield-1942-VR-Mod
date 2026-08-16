#include "client/D3D8RuntimeDiagnostics.h"

#include <cstdio>

int wmain()
{
    using bfvr::D3D8RuntimeDiagnosticLevel;
    using bfvr::IsOffHandCalibrationAuditMessage;
    using bfvr::IsProjectedShadowAuditMessage;
    using bfvr::ParseD3D8RuntimeDiagnosticLevel;

    const bool passed =
        ParseD3D8RuntimeDiagnosticLevel(L"off") ==
            D3D8RuntimeDiagnosticLevel::Off &&
        ParseD3D8RuntimeDiagnosticLevel(L"OFF") ==
            D3D8RuntimeDiagnosticLevel::Off &&
        ParseD3D8RuntimeDiagnosticLevel(L"0") ==
            D3D8RuntimeDiagnosticLevel::Off &&
        ParseD3D8RuntimeDiagnosticLevel(L"") ==
            D3D8RuntimeDiagnosticLevel::Normal &&
        ParseD3D8RuntimeDiagnosticLevel(L"normal") ==
            D3D8RuntimeDiagnosticLevel::Normal &&
        ParseD3D8RuntimeDiagnosticLevel(L"unknown") ==
            D3D8RuntimeDiagnosticLevel::Normal &&
        ParseD3D8RuntimeDiagnosticLevel(L"deep") ==
            D3D8RuntimeDiagnosticLevel::Deep &&
        ParseD3D8RuntimeDiagnosticLevel(L"DEEP") ==
            D3D8RuntimeDiagnosticLevel::Deep &&
        ParseD3D8RuntimeDiagnosticLevel(L"1") ==
            D3D8RuntimeDiagnosticLevel::Deep &&
        IsOffHandCalibrationAuditMessage(
            L"OFFHAND_CALIBRATION_CAPTURE sequence=1") &&
        IsOffHandCalibrationAuditMessage(
            L"OFFHAND_CALIBRATION_REJECT reason=test") &&
        !IsOffHandCalibrationAuditMessage(L"OFFHAND_CALIBRATION") &&
        !IsOffHandCalibrationAuditMessage(
            L"Native 1P off-hand support acquired") &&
        IsProjectedShadowAuditMessage(
            L"PROJECTED_SHADOW_AUDIT summary draws=1") &&
        !IsProjectedShadowAuditMessage(L"PROJECTED_SHADOW_AUDIT") &&
        !IsProjectedShadowAuditMessage(
            L"D3D8 projected shadow summary");
    if (!passed)
    {
        std::fwprintf(stderr, L"BFVR diagnostics-level parsing failed.\n");
        return 1;
    }
    std::wprintf(L"BFVR diagnostics-level tests passed.\n");
    return 0;
}
