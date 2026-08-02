#include "presenter/SharedControlChannel.h"
#include "presenter/SharedTextureProducer.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

namespace
{
void WriteLog(void*, const wchar_t* message)
{
    wprintf(L"[PRODUCER] %s\n", message);
    fflush(stdout);
}

std::wstring QuoteArgument(const wchar_t* argument)
{
    return std::wstring(L"\"") + argument + L"\"";
}

bool IsProcessRunning(HANDLE process)
{
    return process != nullptr && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
}

bool WaitForPresenterRequirements(
    bfvr::shared::ControlBlock& block,
    HANDLE presenterProcess,
    DWORD timeoutMs)
{
    const DWORD startedAt = GetTickCount();
    while (GetTickCount() - startedAt < timeoutMs)
    {
        const bfvr::shared::ProcessState state =
            bfvr::shared::ReadState(&block.presenterState);
        if (state == bfvr::shared::ProcessState::RequirementsReady)
        {
            return true;
        }
        if (state == bfvr::shared::ProcessState::Failed ||
            !IsProcessRunning(presenterProcess))
        {
            return false;
        }
        Sleep(5);
    }
    return false;
}

bfvr::shared::SharedTextureRequirements ReadRequirements(
    const bfvr::shared::ControlBlock& block)
{
    bfvr::shared::SharedTextureRequirements requirements = {};
    requirements.adapterLuid.HighPart = block.requirements.adapterLuidHigh;
    requirements.adapterLuid.LowPart = block.requirements.adapterLuidLow;
    requirements.minimumFeatureLevel =
        static_cast<D3D_FEATURE_LEVEL>(block.requirements.minimumFeatureLevel);
    requirements.leftWorldWidth = block.requirements.leftWorldWidth;
    requirements.leftWorldHeight = block.requirements.leftWorldHeight;
    requirements.rightWorldWidth = block.requirements.rightWorldWidth;
    requirements.rightWorldHeight = block.requirements.rightWorldHeight;
    requirements.uiWidth = block.requirements.uiWidth;
    requirements.uiHeight = block.requirements.uiHeight;
    requirements.format = static_cast<DXGI_FORMAT>(block.requirements.format);
    return requirements;
}

bool LaunchPresenter(
    const wchar_t* presenterPath,
    const wchar_t* channelName,
    const wchar_t* logPath,
    bool useCylinderUi,
    DWORD durationMs,
    PROCESS_INFORMATION& processInformation)
{
    std::wstring command =
        QuoteArgument(presenterPath) +
        L" --channel " + QuoteArgument(channelName) +
        L" --duration-ms " + std::to_wstring(durationMs) +
        L" --log " + QuoteArgument(logPath);
    if (useCylinderUi)
    {
        command += L" --ui-cylinder";
    }
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    ZeroMemory(&processInformation, sizeof(processInformation));
    return CreateProcessW(
        presenterPath,
        mutableCommand.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startupInfo,
        &processInformation) != FALSE;
}

int RunProbe(
    const wchar_t* presenterPath,
    const wchar_t* presenterLogPath,
    bool useCylinderUi,
    bool transportOnly,
    bool runtimeTimed,
    bool brightWorld,
    bool backToGameOverlay,
    DWORD durationMs,
    UINT sourceWidth,
    UINT sourceHeight)
{
    wchar_t channelName[96] = {};
    swprintf_s(
        channelName,
        L"Local\\BFVR-Presenter-%08lX-%08lX",
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long>(GetTickCount()));

    bfvr::shared::SharedControlChannel channel;
    if (!channel.Create(channelName, GetCurrentProcessId()))
    {
        wprintf(
            L"[FAIL] Could not create shared control channel (error %lu).\n",
            channel.LastErrorCode());
        return 3;
    }
    bfvr::shared::ControlBlock* const block = channel.Get();
    if (runtimeTimed)
    {
        block->producerFlags =
            bfvr::shared::kProducerFlagRuntimeTimedRender;
    }

    PROCESS_INFORMATION presenterProcess = {};
    const DWORD presenterDurationMs = durationMs + 20000;
    if (!LaunchPresenter(
            presenterPath,
            channelName,
            presenterLogPath,
            useCylinderUi,
            presenterDurationMs,
            presenterProcess))
    {
        wprintf(L"[FAIL] Could not launch BFVRPresenter (error %lu).\n", GetLastError());
        bfvr::shared::PublishState(
            &block->producerState,
            bfvr::shared::ProcessState::Failed);
        return 4;
    }
    CloseHandle(presenterProcess.hThread);

    int resultCode = 5;
    bfvr::shared::SharedTextureProducer producer;
    if (!WaitForPresenterRequirements(*block, presenterProcess.hProcess, 20000))
    {
        wprintf(
            L"[FAIL] Presenter did not publish requirements (state=%ld error=%ld).\n",
            static_cast<long>(bfvr::shared::ReadState(&block->presenterState)),
            InterlockedCompareExchange(&block->presenterError, 0, 0));
    }
    else
    {
        bfvr::shared::SharedTextureRequirements requirements =
            ReadRequirements(*block);
        if (sourceWidth != 0 && sourceHeight != 0)
        {
            requirements.leftWorldWidth = sourceWidth;
            requirements.leftWorldHeight = sourceHeight;
            requirements.rightWorldWidth = sourceWidth;
            requirements.rightWorldHeight = sourceHeight;
            requirements.uiWidth = sourceWidth;
            requirements.uiHeight = sourceHeight;
        }
        wprintf(
            L"[PRODUCER] Using x64 adapter requirements with producer textures: adapter=%08lX:%08lX format=%u eyes=%ux%u/%ux%u UI=%ux%u.\n",
            static_cast<unsigned long>(requirements.adapterLuid.HighPart),
            static_cast<unsigned long>(requirements.adapterLuid.LowPart),
            static_cast<unsigned int>(requirements.format),
            requirements.leftWorldWidth,
            requirements.leftWorldHeight,
            requirements.rightWorldWidth,
            requirements.rightWorldHeight,
            requirements.uiWidth,
            requirements.uiHeight);
        fflush(stdout);

        if (!producer.Initialize(
                channelName,
                requirements,
                WriteLog,
                nullptr))
        {
            wprintf(L"[FAIL] Could not create x86 shared textures.\n");
        }
        else
        {
            producer.CopyDescriptions(
                block->textures,
                bfvr::shared::kTextureCount);
            bfvr::shared::PublishState(
                &block->producerState,
                bfvr::shared::ProcessState::TexturesReady);
            (void)channel.SignalProducerUpdate();

            bool healthy = true;
            const DWORD startedAt = GetTickCount();
            DWORD localFrameCount = 0;
            while (GetTickCount() - startedAt < durationMs &&
                IsProcessRunning(presenterProcess.hProcess) &&
                bfvr::shared::ReadState(&block->presenterState) !=
                    bfvr::shared::ProcessState::Failed)
            {
                ++localFrameCount;
                LONG frameSequence = 0;
                if (runtimeTimed)
                {
                    frameSequence =
                        InterlockedIncrement(&block->renderReadySequence);
                    (void)channel.SignalProducerUpdate();
                    const DWORD requestWaitStarted = GetTickCount();
                    while (GetTickCount() - requestWaitStarted < 1000 &&
                        InterlockedCompareExchange(
                            &block->renderRequestSequence,
                            0,
                            0) != frameSequence)
                    {
                        const DWORD elapsed =
                            GetTickCount() - requestWaitStarted;
                        const DWORD remaining =
                            elapsed < 1000 ? 1000 - elapsed : 0;
                        if (channel.WaitForPresenterUpdate(
                                (std::min)(remaining, 20UL)) == WAIT_FAILED)
                        {
                            Sleep((std::min)(remaining, 1UL));
                        }
                    }
                    if (InterlockedCompareExchange(
                            &block->renderRequestSequence,
                            0,
                            0) != frameSequence ||
                        block->renderRequest.shouldRender == 0 ||
                        block->renderRequest.viewsValid == 0)
                    {
                        healthy = false;
                        break;
                    }
                }
                if (!producer.PublishSyntheticFrame(localFrameCount, brightWorld))
                {
                    healthy = false;
                    break;
                }
                InterlockedExchange(
                    &block->frameOverlayFlags,
                    backToGameOverlay
                        ? bfvr::shared::kFrameOverlayBackToGameVisible |
                            ((localFrameCount / 30U) % 2U != 0
                                ? bfvr::shared::kFrameOverlayBackToGameHovered
                                : 0)
                        : 0);
                InterlockedExchange(
                    &block->producedFrameCount,
                    static_cast<LONG>(localFrameCount));
                if (runtimeTimed)
                {
                    InterlockedExchange(&block->frameSequence, frameSequence);
                    (void)channel.SignalProducerUpdate();
                    const DWORD submitWaitStarted = GetTickCount();
                    while (GetTickCount() - submitWaitStarted < 1000 &&
                        InterlockedCompareExchange(
                            &block->renderedFrameSequence,
                            0,
                            0) != frameSequence)
                    {
                        const DWORD elapsed =
                            GetTickCount() - submitWaitStarted;
                        const DWORD remaining =
                            elapsed < 1000 ? 1000 - elapsed : 0;
                        if (channel.WaitForPresenterUpdate(
                                (std::min)(remaining, 20UL)) == WAIT_FAILED)
                        {
                            Sleep((std::min)(remaining, 1UL));
                        }
                    }
                    if (InterlockedCompareExchange(
                            &block->renderedFrameSequence,
                            0,
                            0) != frameSequence)
                    {
                        healthy = false;
                        break;
                    }
                }
                else
                {
                    InterlockedIncrement(&block->frameSequence);
                    (void)channel.SignalProducerUpdate();
                }
                Sleep(2);
            }

            const LONG consumedFrames =
                InterlockedCompareExchange(&block->consumedFrameSequence, 0, 0);
            const LONG transportedFrames =
                InterlockedCompareExchange(&block->transportedFrameCount, 0, 0);
            const LONG presentedFrames =
                InterlockedCompareExchange(&block->presentedFrameCount, 0, 0);
            const std::array<DWORD, bfvr::shared::kTextureCount> pixels = {
                block->firstConsumedPixels[0],
                block->firstConsumedPixels[1],
                block->firstConsumedPixels[2]};
            const bool pixelsValid =
                pixels[0] != 0 && pixels[1] != 0 && pixels[2] != 0 &&
                pixels[0] != pixels[1] && pixels[1] != pixels[2];
            wprintf(
                L"[PRODUCER] Transport summary before shutdown: healthy=%d produced=%lu consumedSequence=%ld transported=%ld presented=%ld pixels=[%08lX,%08lX,%08lX].\n",
                healthy ? 1 : 0,
                static_cast<unsigned long>(localFrameCount),
                consumedFrames,
                transportedFrames,
                presentedFrames,
                static_cast<unsigned long>(pixels[0]),
                static_cast<unsigned long>(pixels[1]),
                static_cast<unsigned long>(pixels[2]));
            fflush(stdout);
            resultCode = healthy && localFrameCount > 0 &&
                consumedFrames > 0 && transportedFrames > 0 &&
                (transportOnly || presentedFrames > 0) && pixelsValid
                ? 0
                : 6;
        }
    }

    bfvr::shared::PublishState(
        &block->producerState,
        bfvr::shared::ProcessState::Stopping);
    InterlockedExchange(&block->shutdownRequested, 1);
    (void)channel.SignalProducerUpdate();
    const DWORD presenterWait = WaitForSingleObject(presenterProcess.hProcess, 5000);
    if (presenterWait == WAIT_TIMEOUT)
    {
        TerminateProcess(presenterProcess.hProcess, 9);
        WaitForSingleObject(presenterProcess.hProcess, 2000);
        resultCode = 9;
    }
    DWORD presenterExitCode = STILL_ACTIVE;
    GetExitCodeProcess(presenterProcess.hProcess, &presenterExitCode);
    CloseHandle(presenterProcess.hProcess);
    producer.Shutdown();
    bfvr::shared::PublishState(
        &block->producerState,
        bfvr::shared::ProcessState::Stopped);
    wprintf(
        L"[PRODUCER] Presenter exit code=%lu; producer resources released.\n",
        static_cast<unsigned long>(presenterExitCode));
    fflush(stdout);
    return resultCode == 0 && presenterExitCode == 0 ? 0 : resultCode;
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    const wchar_t* presenterPath = nullptr;
    const wchar_t* presenterLogPath = L"BFVRPresenter.log";
    bool useCylinderUi = false;
    bool transportOnly = false;
    bool runtimeTimed = false;
    bool brightWorld = false;
    bool backToGameOverlay = false;
    DWORD durationMs = 10000;
    UINT sourceWidth = 0;
    UINT sourceHeight = 0;
    for (int index = 1; index < argc; ++index)
    {
        if (wcscmp(argv[index], L"--presenter") == 0 && index + 1 < argc)
        {
            presenterPath = argv[++index];
        }
        else if (wcscmp(argv[index], L"--presenter-log") == 0 && index + 1 < argc)
        {
            presenterLogPath = argv[++index];
        }
        else if (wcscmp(argv[index], L"--duration-ms") == 0 && index + 1 < argc)
        {
            const unsigned long parsed = wcstoul(argv[++index], nullptr, 10);
            if (parsed < 1000 || parsed > 60000)
            {
                wprintf(L"[FAIL] --duration-ms must be from 1000 through 60000.\n");
                return 2;
            }
            durationMs = static_cast<DWORD>(parsed);
        }
        else if (wcscmp(argv[index], L"--ui-cylinder") == 0)
        {
            useCylinderUi = true;
        }
        else if (wcscmp(argv[index], L"--transport-only") == 0)
        {
            transportOnly = true;
        }
        else if (wcscmp(argv[index], L"--runtime-timed") == 0)
        {
            runtimeTimed = true;
        }
        else if (wcscmp(argv[index], L"--bright-world") == 0)
        {
            brightWorld = true;
        }
        else if (wcscmp(argv[index], L"--back-to-game-overlay") == 0)
        {
            backToGameOverlay = true;
        }
        else if (wcscmp(argv[index], L"--source-width") == 0 && index + 1 < argc)
        {
            sourceWidth = wcstoul(argv[++index], nullptr, 10);
        }
        else if (wcscmp(argv[index], L"--source-height") == 0 && index + 1 < argc)
        {
            sourceHeight = wcstoul(argv[++index], nullptr, 10);
        }
        else
        {
            wprintf(
                L"Usage: BFVRSharedTextureProducerProbe --presenter <x64-path> [--presenter-log <path>] [--duration-ms <1000-60000>] [--ui-cylinder] [--transport-only] [--runtime-timed] [--bright-world] [--back-to-game-overlay] [--source-width <pixels> --source-height <pixels>]\n");
            return 2;
        }
    }
    if (presenterPath == nullptr)
    {
        wprintf(L"[FAIL] --presenter is required.\n");
        return 2;
    }
    if ((sourceWidth == 0) != (sourceHeight == 0) ||
        sourceWidth > 8192 ||
        sourceHeight > 8192)
    {
        wprintf(L"[FAIL] Source width and height must both be from 1 through 8192, or both omitted.\n");
        return 2;
    }
    return RunProbe(
        presenterPath,
        presenterLogPath,
        useCylinderUi,
        transportOnly,
        runtimeTimed,
        brightWorld,
        backToGameOverlay,
        durationMs,
        sourceWidth,
        sourceHeight);
}
