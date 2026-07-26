#include "openxr/OpenXRPresentation.h"
#include "presenter/SharedControlChannel.h"
#include "presenter/SharedTextureConsumer.h"

#include <windows.h>

#include <array>
#include <cstdio>
#include <cwchar>
#include <iterator>

namespace
{
FILE* g_output = stdout;

void WriteLog(void*, const wchar_t* message)
{
    fwprintf(g_output, L"[PRESENTER] %s\n", message);
    fflush(g_output);
}

bool GetExecutableDirectory(wchar_t* directory, std::size_t directorySize)
{
    if (GetModuleFileNameW(nullptr, directory, static_cast<DWORD>(directorySize)) == 0)
    {
        return false;
    }
    wchar_t* separator = wcsrchr(directory, L'\\');
    if (separator == nullptr)
    {
        return false;
    }
    *separator = L'\0';
    return true;
}

class ScopedComInitialization
{
public:
    ScopedComInitialization()
        : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED))
        , shouldUninitialize_(SUCCEEDED(result_))
    {
    }

    ~ScopedComInitialization()
    {
        if (shouldUninitialize_)
        {
            CoUninitialize();
        }
    }

    [[nodiscard]] HRESULT Result() const noexcept
    {
        return result_;
    }

private:
    HRESULT result_ = E_FAIL;
    bool shouldUninitialize_ = false;
};

class ScopedHandle
{
public:
    explicit ScopedHandle(HANDLE handle)
        : handle_(handle)
    {
    }

    ~ScopedHandle()
    {
        if (handle_ != nullptr)
        {
            CloseHandle(handle_);
        }
    }

    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;

    [[nodiscard]] HANDLE Get() const noexcept
    {
        return handle_;
    }

private:
    HANDLE handle_ = nullptr;
};

void PublishRequirements(
    bfvr::shared::ControlBlock& block,
    const bfvr::OpenXRPresentationTextureRequirements& requirements)
{
    block.requirements.leftWorldWidth = requirements.leftWorldWidth;
    block.requirements.leftWorldHeight = requirements.leftWorldHeight;
    block.requirements.rightWorldWidth = requirements.rightWorldWidth;
    block.requirements.rightWorldHeight = requirements.rightWorldHeight;
    block.requirements.uiWidth = requirements.uiWidth;
    block.requirements.uiHeight = requirements.uiHeight;
    block.requirements.format = static_cast<DWORD>(requirements.format);
    block.requirements.adapterLuidHigh = requirements.adapterLuid.HighPart;
    block.requirements.adapterLuidLow = requirements.adapterLuid.LowPart;
    block.requirements.minimumFeatureLevel =
        static_cast<DWORD>(requirements.minimumFeatureLevel);
    block.requirements.deviceFeatureLevel =
        static_cast<DWORD>(requirements.deviceFeatureLevel);
    bfvr::shared::PublishState(
        &block.presenterState,
        bfvr::shared::ProcessState::RequirementsReady);
}

void CopyControllerPose(
    const bfvr::OpenXRPresentationPose& source,
    bfvr::shared::SharedPresentationPose& destination)
{
    destination.orientationX = source.orientationX;
    destination.orientationY = source.orientationY;
    destination.orientationZ = source.orientationZ;
    destination.orientationW = source.orientationW;
    destination.positionX = source.positionX;
    destination.positionY = source.positionY;
    destination.positionZ = source.positionZ;
}

DWORD MakeControllerHandFlags(const bfvr::OpenXRControllerHandState& hand)
{
    DWORD flags = 0;
    flags |= hand.aimActive ? bfvr::shared::kControllerHandFlagAimActive : 0;
    flags |= hand.aimPositionValid
        ? bfvr::shared::kControllerHandFlagAimPositionValid
        : 0;
    flags |= hand.aimOrientationValid
        ? bfvr::shared::kControllerHandFlagAimOrientationValid
        : 0;
    flags |= hand.gripActive ? bfvr::shared::kControllerHandFlagGripActive : 0;
    flags |= hand.gripPositionValid
        ? bfvr::shared::kControllerHandFlagGripPositionValid
        : 0;
    flags |= hand.gripOrientationValid
        ? bfvr::shared::kControllerHandFlagGripOrientationValid
        : 0;
    flags |= hand.triggerActive
        ? bfvr::shared::kControllerHandFlagTriggerActive
        : 0;
    flags |= hand.squeezeActive
        ? bfvr::shared::kControllerHandFlagSqueezeActive
        : 0;
    flags |= hand.thumbstickActive
        ? bfvr::shared::kControllerHandFlagThumbstickActive
        : 0;
    return flags;
}

DWORD MakeControllerButtons(const bfvr::OpenXRControllerHandState& hand)
{
    DWORD buttons = 0;
    buttons |= hand.primaryPressed
        ? bfvr::shared::kControllerHandButtonPrimary
        : 0;
    buttons |= hand.secondaryPressed
        ? bfvr::shared::kControllerHandButtonSecondary
        : 0;
    buttons |= hand.menuPressed
        ? bfvr::shared::kControllerHandButtonMenu
        : 0;
    return buttons;
}

void PublishControllerSample(
    bfvr::shared::ControlBlock& block,
    LONG sequence,
    const bfvr::OpenXRControllerInputState& input)
{
    bfvr::shared::SharedControllerSample& destination = block.controllerSample;
    destination.predictedDisplayTime = input.predictedDisplayTime;
    destination.flags = input.sessionFocused
        ? bfvr::shared::kControllerSampleFlagSessionFocused
        : 0;
    for (std::size_t hand = 0; hand < input.hands.size(); ++hand)
    {
        const bfvr::OpenXRControllerHandState& sourceHand = input.hands[hand];
        bfvr::shared::SharedControllerHandSample& destinationHand =
            destination.hands[hand];
        destinationHand.flags = MakeControllerHandFlags(sourceHand);
        destinationHand.buttons = MakeControllerButtons(sourceHand);
        CopyControllerPose(sourceHand.aimPose, destinationHand.aimPose);
        CopyControllerPose(sourceHand.gripPose, destinationHand.gripPose);
        destinationHand.triggerValue = sourceHand.triggerValue;
        destinationHand.squeezeValue = sourceHand.squeezeValue;
        destinationHand.thumbstickX = sourceHand.thumbstickX;
        destinationHand.thumbstickY = sourceHand.thumbstickY;
    }
    MemoryBarrier();
    InterlockedExchange(&block.controllerSampleSequence, sequence);
}

void PublishRenderRequest(
    bfvr::shared::ControlBlock& block,
    LONG sequence,
    const bfvr::OpenXRPresentationFrameState& frame)
{
    PublishControllerSample(block, sequence, frame.controllerInput);
    block.renderRequest.predictedDisplayTime = frame.predictedDisplayTime;
    block.renderRequest.shouldRender = frame.shouldRender ? 1 : 0;
    block.renderRequest.viewsValid = frame.viewsValid ? 1 : 0;
    for (std::size_t eye = 0; eye < frame.views.size(); ++eye)
    {
        const bfvr::OpenXRPresentationView& source = frame.views[eye];
        bfvr::shared::SharedPresentationView& destination =
            block.renderRequest.views[eye];
        destination.pose.orientationX = source.pose.orientationX;
        destination.pose.orientationY = source.pose.orientationY;
        destination.pose.orientationZ = source.pose.orientationZ;
        destination.pose.orientationW = source.pose.orientationW;
        destination.pose.positionX = source.pose.positionX;
        destination.pose.positionY = source.pose.positionY;
        destination.pose.positionZ = source.pose.positionZ;
        destination.fov.angleLeft = source.fov.angleLeft;
        destination.fov.angleRight = source.fov.angleRight;
        destination.fov.angleUp = source.fov.angleUp;
        destination.fov.angleDown = source.fov.angleDown;
    }
    MemoryBarrier();
    InterlockedExchange(&block.renderRequestSequence, sequence);
}

int RunPresenter(
    const wchar_t* channelName,
    bool useCylinderUi,
    DWORD durationMs,
    bool runUntilStopped,
    const wchar_t* payloadDirectory)
{
    bfvr::shared::SharedControlChannel channel;
    if (!channel.Open(channelName))
    {
        fwprintf(
            g_output,
            L"[FAIL] Could not open shared channel '%s' (error %lu).\n",
            channelName,
            channel.LastErrorCode());
        return 3;
    }
    bfvr::shared::ControlBlock* const block = channel.Get();
    const ScopedHandle producerProcess(
        OpenProcess(
            SYNCHRONIZE,
            FALSE,
            block->producerProcessId));
    if (runUntilStopped &&
        producerProcess.Get() == nullptr)
    {
        fwprintf(
            g_output,
            L"[FAIL] Could not monitor producer process %lu (error %lu).\n",
            static_cast<unsigned long>(block->producerProcessId),
            GetLastError());
        return 3;
    }
    const auto producerIsAlive = [&producerProcess]() {
        return producerProcess.Get() == nullptr ||
            WaitForSingleObject(
                producerProcess.Get(),
                0) == WAIT_TIMEOUT;
    };
    block->presenterProcessId = GetCurrentProcessId();
    bfvr::shared::PublishState(
        &block->presenterState,
        bfvr::shared::ProcessState::Starting);

    const ScopedComInitialization comInitialization;
    if (FAILED(comInitialization.Result()) &&
        comInitialization.Result() != RPC_E_CHANGED_MODE)
    {
        InterlockedExchange(&block->presenterError, 4);
        bfvr::shared::PublishState(
            &block->presenterState,
            bfvr::shared::ProcessState::Failed);
        WriteLog(nullptr, L"COM initialization failed.");
        return 4;
    }

    bfvr::OpenXRPresentationConfiguration configuration = {};
    configuration.uiLayerMode = useCylinderUi
        ? bfvr::OpenXRUiLayerMode::Cylinder
        : bfvr::OpenXRUiLayerMode::Quad;
    bfvr::OpenXRPresentation presentation;
    if (!presentation.Initialize(payloadDirectory, configuration, WriteLog, nullptr))
    {
        InterlockedExchange(&block->presenterError, 5);
        bfvr::shared::PublishState(
            &block->presenterState,
            bfvr::shared::ProcessState::Failed);
        return 5;
    }

    const bfvr::OpenXRPresentationTextureRequirements requirements =
        presentation.GetTextureRequirements();
    PublishRequirements(*block, requirements);
    fwprintf(
        g_output,
        L"[PRESENTER] Published x64 requirements: adapter=%08lX:%08lX format=%u eyes=%ux%u/%ux%u UI=%ux%u.\n",
        static_cast<unsigned long>(requirements.adapterLuid.HighPart),
        static_cast<unsigned long>(requirements.adapterLuid.LowPart),
        static_cast<unsigned int>(requirements.format),
        requirements.leftWorldWidth,
        requirements.leftWorldHeight,
        requirements.rightWorldWidth,
        requirements.rightWorldHeight,
        requirements.uiWidth,
        requirements.uiHeight);
    fflush(g_output);

    const DWORD textureWaitStarted = GetTickCount();
    const DWORD textureWaitTimeoutMs =
        (block->producerFlags &
            bfvr::shared::kProducerFlagRuntimeTimedRender) != 0
        ? 60000
        : 15000;
    while (bfvr::shared::ReadState(&block->producerState) !=
        bfvr::shared::ProcessState::TexturesReady)
    {
        if (InterlockedCompareExchange(&block->shutdownRequested, 0, 0) != 0 ||
            GetTickCount() - textureWaitStarted >= textureWaitTimeoutMs ||
            !producerIsAlive() ||
            bfvr::shared::ReadState(&block->producerState) ==
                bfvr::shared::ProcessState::Failed ||
            !presentation.PollEvents())
        {
            InterlockedExchange(&block->presenterError, 6);
            bfvr::shared::PublishState(
                &block->presenterState,
                bfvr::shared::ProcessState::Failed);
            presentation.Shutdown();
            return 6;
        }
        Sleep(5);
    }

    bfvr::shared::SharedTextureConsumer consumer;
    if (!consumer.Initialize(
            presentation.GetD3D11Device(),
            presentation.GetD3D11Context(),
            block->textures,
            bfvr::shared::kTextureCount,
            block->requirements,
            WriteLog,
            nullptr))
    {
        InterlockedExchange(&block->presenterError, 7);
        bfvr::shared::PublishState(
            &block->presenterState,
            bfvr::shared::ProcessState::Failed);
        presentation.Shutdown();
        return 7;
    }

    bfvr::shared::PublishState(
        &block->presenterState,
        bfvr::shared::ProcessState::Running);
    LONG consumedSequence = 0;
    bool haveFrame = false;
    bool sampledPixels = false;
    bool healthy = true;
    const bool runtimeTimedProducer =
        (block->producerFlags &
         bfvr::shared::kProducerFlagRuntimeTimedRender) != 0;
    LONG completedRenderRequest = 0;
    auto consumeSequence = [&](LONG availableSequence)
    {
        if (!consumer.ConsumeFrame())
        {
            return false;
        }
        consumedSequence = availableSequence;
        InterlockedExchange(&block->consumedFrameSequence, consumedSequence);
        InterlockedIncrement(&block->transportedFrameCount);
        haveFrame = true;
        if (!sampledPixels)
        {
            std::array<DWORD, bfvr::shared::kTextureCount> pixels = {};
            if (consumer.ReadCenterPixels(pixels.data(), pixels.size()))
            {
                for (std::size_t index = 0; index < pixels.size(); ++index)
                {
                    block->firstConsumedPixels[index] = pixels[index];
                }
                fwprintf(
                    g_output,
                    L"[PRESENTER] First x64 local-copy pixels: left=%08lX right=%08lX UI=%08lX.\n",
                    static_cast<unsigned long>(pixels[0]),
                    static_cast<unsigned long>(pixels[1]),
                    static_cast<unsigned long>(pixels[2]));
                fflush(g_output);
                sampledPixels = true;
            }
        }
        return true;
    };
    const DWORD startedAt = GetTickCount();
    while ((runUntilStopped ||
            GetTickCount() - startedAt < durationMs) &&
        InterlockedCompareExchange(
            &block->shutdownRequested,
            0,
            0) == 0 &&
        producerIsAlive())
    {
        if (!presentation.PollEvents())
        {
            healthy = false;
            break;
        }

        if (runtimeTimedProducer)
        {
            const LONG readySequence =
                InterlockedCompareExchange(&block->renderReadySequence, 0, 0);
            if (!presentation.IsSessionRunning() ||
                readySequence == completedRenderRequest)
            {
                Sleep(1);
                continue;
            }

            bfvr::OpenXRPresentationFrameState frame = {};
            if (!presentation.BeginFrame(frame))
            {
                healthy = false;
                break;
            }
            if (!frame.shouldRender || !frame.viewsValid)
            {
                presentation.EndFrame({});
                continue;
            }
            PublishRenderRequest(*block, readySequence, frame);

            const DWORD renderWaitStarted = GetTickCount();
            LONG availableSequence = 0;
            while ((runUntilStopped ||
                    GetTickCount() - renderWaitStarted < 2000) &&
                InterlockedCompareExchange(&block->shutdownRequested, 0, 0) == 0 &&
                producerIsAlive())
            {
                availableSequence =
                    InterlockedCompareExchange(&block->frameSequence, 0, 0);
                if (availableSequence == readySequence ||
                    InterlockedCompareExchange(&block->shutdownRequested, 0, 0) != 0)
                {
                    break;
                }
                Sleep(1);
            }
            if (availableSequence != readySequence)
            {
                // A continuous owner-requested session has no presentation
                // deadline.  A game pause may leave this frame request
                // outstanding until the producer resumes or requests a
                // clean stop; neither case is a presentation fault.
                if (!runUntilStopped)
                {
                    healthy = false;
                }
                break;
            }
            if (!consumeSequence(availableSequence) ||
                !presentation.EndFrame(consumer.GetLocalTextures()))
            {
                healthy = false;
                break;
            }
            InterlockedIncrement(&block->presentedFrameCount);
            InterlockedExchange(&block->renderedFrameSequence, readySequence);
            completedRenderRequest = readySequence;
            continue;
        }

        const LONG availableSequence =
            InterlockedCompareExchange(&block->frameSequence, 0, 0);
        if (availableSequence != consumedSequence &&
            !consumeSequence(availableSequence))
        {
            healthy = false;
            break;
        }
        if (presentation.IsSessionRunning() && haveFrame)
        {
            if (!presentation.SubmitFrame(consumer.GetLocalTextures()))
            {
                healthy = false;
                break;
            }
            InterlockedIncrement(&block->presentedFrameCount);
        }
        else
        {
            Sleep(2);
        }
    }

    bfvr::shared::PublishState(
        &block->presenterState,
        bfvr::shared::ProcessState::Stopping);
    consumer.Shutdown();
    presentation.Shutdown();
    const LONG presentedFrames =
        InterlockedCompareExchange(&block->presentedFrameCount, 0, 0);
    const bool succeeded = healthy && consumedSequence > 0 && presentedFrames > 0;
    if (!succeeded)
    {
        InterlockedExchange(&block->presenterError, 8);
    }
    bfvr::shared::PublishState(
        &block->presenterState,
        succeeded
            ? bfvr::shared::ProcessState::Stopped
            : bfvr::shared::ProcessState::Failed);
    fwprintf(
        g_output,
        L"[PRESENTER] Exit summary: healthy=%d consumedSequence=%ld presentedFrames=%ld.\n",
        healthy ? 1 : 0,
        consumedSequence,
        presentedFrames);
    fflush(g_output);
    return succeeded ? 0 : 8;
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    const wchar_t* channelName = nullptr;
    bool useCylinderUi = false;
    DWORD durationMs = 60000;
    bool durationSpecified = false;
    bool runUntilStopped = false;
    for (int index = 1; index < argc; ++index)
    {
        if (wcscmp(argv[index], L"--channel") == 0 && index + 1 < argc)
        {
            channelName = argv[++index];
        }
        else if (wcscmp(argv[index], L"--duration-ms") == 0 && index + 1 < argc)
        {
            const unsigned long parsed = wcstoul(argv[++index], nullptr, 10);
            if (parsed < 1000 || parsed > 120000)
            {
                fwprintf(stderr, L"[FAIL] --duration-ms must be from 1000 through 120000.\n");
                return 2;
            }
            durationMs = static_cast<DWORD>(parsed);
            durationSpecified = true;
        }
        else if (wcscmp(argv[index], L"--run-until-stopped") == 0)
        {
            runUntilStopped = true;
        }
        else if (wcscmp(argv[index], L"--ui-cylinder") == 0)
        {
            useCylinderUi = true;
        }
        else if (wcscmp(argv[index], L"--log") == 0 && index + 1 < argc)
        {
            if (_wfreopen_s(&g_output, argv[++index], L"w, ccs=UTF-8", stdout) != 0)
            {
                fwprintf(stderr, L"[FAIL] Could not open the presenter log.\n");
                return 2;
            }
        }
        else
        {
            fwprintf(
                stderr,
                L"Usage: BFVRPresenter --channel <name> [--duration-ms <1000-120000> | --run-until-stopped] [--ui-cylinder] [--log <path>]\n");
            return 2;
        }
    }
    if (channelName == nullptr)
    {
        fwprintf(stderr, L"[FAIL] --channel is required.\n");
        return 2;
    }
    if (runUntilStopped && durationSpecified)
    {
        fwprintf(
            stderr,
            L"[FAIL] Select either --duration-ms or --run-until-stopped.\n");
        return 2;
    }

    wchar_t payloadDirectory[MAX_PATH] = {};
    if (!GetExecutableDirectory(payloadDirectory, std::size(payloadDirectory)))
    {
        fwprintf(stderr, L"[FAIL] Could not determine the presenter directory.\n");
        return 2;
    }
    return RunPresenter(
        channelName,
        useCylinderUi,
        durationMs,
        runUntilStopped,
        payloadDirectory);
}
