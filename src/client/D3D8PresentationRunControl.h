#pragma once

#include "client/D3D8StereoProbeReporting.h"

#include <windows.h>

#include <string>

namespace bfvr::d3d8probe
{

[[nodiscard]] std::wstring MakePresentationStopEventName(
    DWORD processId);

[[nodiscard]] bool CheckPresentationStopRequested(
    HANDLE stopEvent,
    volatile LONG& stopRequested) noexcept;

[[nodiscard]] HANDLE OpenAndLogPresentationStopEvent(
    FormattedLogCallback appendLog);

} // namespace bfvr::d3d8probe
