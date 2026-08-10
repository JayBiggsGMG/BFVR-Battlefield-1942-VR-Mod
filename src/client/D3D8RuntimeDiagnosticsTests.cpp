#include "client/D3D8RuntimeDiagnostics.h"

#include <cstdio>

int wmain()
{
    using bfvr::D3D8RuntimeDiagnosticLevel;
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
            D3D8RuntimeDiagnosticLevel::Deep;
    if (!passed)
    {
        std::fwprintf(stderr, L"BFVR diagnostics-level parsing failed.\n");
        return 1;
    }
    std::wprintf(L"BFVR diagnostics-level tests passed.\n");
    return 0;
}
