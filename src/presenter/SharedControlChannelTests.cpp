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

    bfvr::shared::ControlBlock* const producerBlock = producer.Get();
    bfvr::shared::ControlBlock* const presenterBlock = presenter.Get();
    presenterBlock->controllerSample.mountedCameraToggleSequence = 7;
    MemoryBarrier();
    InterlockedExchange(&presenterBlock->controllerSampleSequence, 11);
    passed = Check(
        presenter.SignalPresenterUpdate() &&
            producer.WaitForPresenterUpdate(1000) == WAIT_OBJECT_0 &&
            InterlockedCompareExchange(
                &producerBlock->controllerSampleSequence,
                0,
                0) == 11 &&
            producerBlock->controllerSample.mountedCameraToggleSequence == 7,
        L"presenter-to-producer mounted-camera toggle payload failed") &&
        passed;

    presenterBlock->renderRequest.recenterForwardSequence = 19;
    presenterBlock->renderRequest.standingHeightValid = 1;
    presenterBlock->renderRequest.standingHeightMeters = 1.73F;
    MemoryBarrier();
    InterlockedExchange(&presenterBlock->renderRequestSequence, 23);
    passed = Check(
        presenter.SignalPresenterUpdate() &&
            producer.WaitForPresenterUpdate(1000) == WAIT_OBJECT_0 &&
            InterlockedCompareExchange(
                &producerBlock->renderRequestSequence,
                0,
                0) == 23 &&
            producerBlock->renderRequest.recenterForwardSequence == 19 &&
            producerBlock->renderRequest.standingHeightValid == 1 &&
            producerBlock->renderRequest.standingHeightMeters == 1.73F,
        L"presenter-to-producer context recenter/STAGE-height payload failed") &&
        passed;

    InterlockedExchange(&producerBlock->mountedCameraDecoupled, 1);
    passed = Check(
        producer.SignalProducerUpdate() &&
            presenter.WaitForProducerUpdate(1000) == WAIT_OBJECT_0 &&
            InterlockedCompareExchange(
                &presenterBlock->mountedCameraDecoupled,
                0,
                0) == 1,
        L"producer-to-presenter mounted-camera state feedback failed") &&
        passed;

    if (passed)
    {
        wprintf(L"[PASS] Shared control events, context recenter/STAGE-height commands, and mounted-camera feedback are bidirectional.\n");
    }
    return passed ? 0 : 1;
}
