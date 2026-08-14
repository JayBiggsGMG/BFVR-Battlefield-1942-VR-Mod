#include "client/ControllerHaptics.h"
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

    InterlockedIncrement(&producerBlock->hapticShotRightSequence);
    InterlockedIncrement(&producerBlock->hapticShotBothSequence);
    InterlockedIncrement(&producerBlock->hapticDeathSequence);
    InterlockedExchange(
        &producerBlock->localPlayerLifeState,
        static_cast<LONG>(bfvr::shared::LocalPlayerLifeState::Dead));
    InterlockedIncrement(&producerBlock->hapticNativeMenuHoverSequence);
    InterlockedIncrement(&producerBlock->killSoundSequence);
    passed = Check(
        producer.SignalProducerUpdate() &&
            presenter.WaitForProducerUpdate(1000) == WAIT_OBJECT_0 &&
            InterlockedCompareExchange(
                &presenterBlock->hapticShotRightSequence, 0, 0) == 1 &&
            InterlockedCompareExchange(
                &presenterBlock->hapticShotBothSequence, 0, 0) == 1 &&
            InterlockedCompareExchange(
                &presenterBlock->hapticDeathSequence, 0, 0) == 1 &&
            InterlockedCompareExchange(
                &presenterBlock->localPlayerLifeState, 0, 0) ==
                static_cast<LONG>(
                    bfvr::shared::LocalPlayerLifeState::Dead) &&
            InterlockedCompareExchange(
                &presenterBlock->hapticNativeMenuHoverSequence,
                0,
                0) == 1 &&
            InterlockedCompareExchange(
                &presenterBlock->killSoundSequence, 0, 0) == 1,
        L"producer-to-presenter haptic and kill-sound event counters failed") && passed;

    bfvr::RegisterControllerHapticTransport(producerBlock);
    const LONG initialHoverSequence = InterlockedCompareExchange(
        &producerBlock->hapticNativeMenuHoverSequence,
        0,
        0);
    bfvr::NotifyControllerNativeMenuHover();
    const LONG initialKillSequence = InterlockedCompareExchange(
        &producerBlock->killSoundSequence, 0, 0);
    bfvr::NotifyLocalPlayerKillSound();
    InterlockedIncrement(&presenterBlock->nativeMenuSoundHighlightSequence);
    InterlockedIncrement(&presenterBlock->nativeMenuSoundOkSequence);
    InterlockedIncrement(&presenterBlock->nativeMenuSoundCancelSequence);
    const bfvr::NativeMenuSoundRequests menuSounds =
        bfvr::ConsumeNativeMenuSoundRequests();
    bfvr::RegisterControllerHapticTransport(nullptr);
    bfvr::NotifyControllerNativeMenuHover();
    bfvr::NotifyLocalPlayerKillSound();
    passed = Check(
        InterlockedCompareExchange(
            &producerBlock->hapticNativeMenuHoverSequence,
            0,
            0) == initialHoverSequence + 1 &&
        InterlockedCompareExchange(
            &producerBlock->killSoundSequence,
            0,
            0) == initialKillSequence + 1 &&
        menuSounds.highlight == 1 && menuSounds.ok == 1 &&
        menuSounds.cancel == 1,
        L"registered kill and bidirectional native-menu audio events did not publish exactly once") && passed;

    if (passed)
    {
        wprintf(L"[PASS] Shared control events, haptic counters, context recenter/STAGE-height commands, and mounted-camera feedback are bidirectional.\n");
    }
    return passed ? 0 : 1;
}
