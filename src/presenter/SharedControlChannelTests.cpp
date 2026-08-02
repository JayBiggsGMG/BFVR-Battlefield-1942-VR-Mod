#include "presenter/SharedControlChannel.h"

#include <windows.h>

#include <cstdio>

namespace
{
bool Check(bool condition, const wchar_t* message)
{
    if (!condition)
    {
        fwprintf(stderr, L"[FAIL] %s\n", message);
    }
    return condition;
}
} // namespace

int main()
{
    wchar_t channelName[128] = {};
    swprintf_s(
        channelName,
        L"Local\\BFVR-ControlChannelTest-%08lX-%08lX",
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long>(GetTickCount()));

    bfvr::shared::SharedControlChannel producer;
    bfvr::shared::SharedControlChannel presenter;
    bool passed = Check(
        producer.Create(channelName, GetCurrentProcessId()),
        L"producer channel creation failed");
    passed = Check(
        passed && presenter.Open(channelName),
        L"presenter channel open failed") && passed;
    passed = Check(
        producer.HasUpdateEvents() && presenter.HasUpdateEvents(),
        L"cross-process update events were not opened") && passed;

    passed = Check(
        producer.SignalProducerUpdate(),
        L"producer update signal failed") && passed;
    passed = Check(
        presenter.WaitForProducerUpdate(1000) == WAIT_OBJECT_0,
        L"presenter did not receive producer update") && passed;
    passed = Check(
        presenter.WaitForProducerUpdate(0) == WAIT_TIMEOUT,
        L"producer update event did not auto-reset") && passed;

    passed = Check(
        presenter.SignalPresenterUpdate(),
        L"presenter update signal failed") && passed;
    passed = Check(
        producer.WaitForPresenterUpdate(1000) == WAIT_OBJECT_0,
        L"producer did not receive presenter update") && passed;
    passed = Check(
        producer.WaitForPresenterUpdate(0) == WAIT_TIMEOUT,
        L"presenter update event did not auto-reset") && passed;

    if (passed)
    {
        wprintf(L"[PASS] Shared control channel update events are bidirectional and auto-reset.\n");
    }
    return passed ? 0 : 1;
}
