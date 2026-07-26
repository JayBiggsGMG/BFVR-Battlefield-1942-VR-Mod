#include "client/D3D8PresentationRunControl.h"

#include <array>
#include <cwchar>
#include <iterator>

namespace bfvr::d3d8probe
{

std::wstring MakePresentationStopEventName(DWORD processId)
{
    std::array<wchar_t, 96> eventName = {};
    if (swprintf_s(
            eventName.data(),
            eventName.size(),
            L"Local\\BFVRPresentationStop-%lu",
            processId) < 0)
    {
        return {};
    }
    return eventName.data();
}

bool CheckPresentationStopRequested(
    HANDLE stopEvent,
    volatile LONG& stopRequested) noexcept
{
    if (InterlockedCompareExchange(&stopRequested, 0, 0) != 0)
    {
        return true;
    }
    if (stopEvent != nullptr &&
        WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0)
    {
        InterlockedExchange(&stopRequested, 1);
        return true;
    }
    return false;
}

HANDLE OpenAndLogPresentationStopEvent(
    FormattedLogCallback appendLog)
{
    const std::wstring stopEventName =
        MakePresentationStopEventName(GetCurrentProcessId());
    HANDLE stopEvent = stopEventName.empty()
        ? nullptr
        : OpenEventW(SYNCHRONIZE, FALSE, stopEventName.c_str());
    if (appendLog != nullptr)
    {
        if (stopEvent != nullptr)
        {
            appendLog(
                L"Continuous OpenXR presentation will run until the game exits or external stop event '%s' is signaled.",
                stopEventName.c_str());
        }
        else
        {
            appendLog(
                L"Continuous OpenXR presentation will run until the game exits; the loader stop event could not be opened (error %lu).",
                GetLastError());
        }
    }
    return stopEvent;
}

} // namespace bfvr::d3d8probe
