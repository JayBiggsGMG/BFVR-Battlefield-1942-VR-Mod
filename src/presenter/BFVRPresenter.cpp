#include "openxr/OpenXRPresentation.h"
#include "presenter/DesktopMirror.h"
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
constexpr DWORD kRuntimeTimedSourceGraceMs = 100;

std::int64_t ReadPerformanceCounter() noexcept
{
    LARGE_INTEGER counter = {};
    return QueryPerformanceCounter(&counter) ? counter.QuadPart : 0;
}

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
    flags |= hand.aimPositionTracked
        ? bfvr::shared::kControllerHandFlagAimPositionTracked
        : 0;
    flags |= hand.aimOrientationTracked
        ? bfvr::shared::kControllerHandFlagAimOrientationTracked
        : 0;
    flags |= hand.gripActive ? bfvr::shared::kControllerHandFlagGripActive : 0;
    flags |= hand.gripPositionValid
        ? bfvr::shared::kControllerHandFlagGripPositionValid
        : 0;
    flags |= hand.gripOrientationValid
        ? bfvr::shared::kControllerHandFlagGripOrientationValid
        : 0;
    flags |= hand.gripPositionTracked
        ? bfvr::shared::kControllerHandFlagGripPositionTracked
        : 0;
    flags |= hand.gripOrientationTracked
        ? bfvr::shared::kControllerHandFlagGripOrientationTracked
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
    block.renderRequest.headPoseValid = frame.headPoseValid ? 1 : 0;
    block.renderRequest.headPoseTracked = frame.headPoseTracked ? 1 : 0;
    CopyControllerPose(frame.headPose, block.renderRequest.headPose);
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

    bfvr::DesktopMirror desktopMirror;
    desktopMirror.Initialize(
        presentation.GetD3D11Device(),
        presentation.GetD3D11Context(),
        block->producerProcessId,
        WriteLog,
        nullptr);

    bfvr::shared::PublishState(
        &block->presenterState,
        bfvr::shared::ProcessState::Running);
    LONG consumedSequence = 0;
    bool haveFrame = false;
    bool sampledPixels = false;
    bool healthy = true;
    bfvr::OpenXRUiReferenceMode acceptedUiReferenceMode =
        bfvr::OpenXRUiReferenceMode::WorldLocked;
    bfvr::OpenXRPresentationPose acceptedUiWorldAnchor = {};
    bool acceptedUiWorldAnchorValid = false;
    const bool runtimeTimedProducer =
        (block->producerFlags &
         bfvr::shared::kProducerFlagRuntimeTimedRender) != 0;
    LONG completedRenderRequest = 0;
    LONG pendingSourceSequence = 0;
    bool sourceGapReported = false;
    std::int64_t totalSourceConsumeQpcTicks = 0;
    std::int64_t totalDesktopMirrorQpcTicks = 0;
    std::int64_t totalOpenXrBeginQpcTicks = 0;
    std::int64_t totalOpenXrSubmitOrEndQpcTicks = 0;
    LONG sourceConsumeCount = 0;
    LONG desktopMirrorCount = 0;
    LONG openXrBeginCount = 0;
    LONG openXrSubmitOrEndCount = 0;
    auto consumeSequence = [&](LONG availableSequence)
    {
        const std::int64_t consumeStarted = ReadPerformanceCounter();
        if (!consumer.ConsumeFrame())
        {
            return false;
        }
        totalSourceConsumeQpcTicks +=
            ReadPerformanceCounter() - consumeStarted;
        ++sourceConsumeCount;
        consumedSequence = availableSequence;
        // ConsumeFrame waits for the legacy D3D9 source reads to complete
        // before it returns. The x86 producer may now reuse its shared frame
        // targets; it does not need to wait for the later OpenXR copy/submit.
        MemoryBarrier();
        InterlockedExchange(&block->consumedFrameSequence, consumedSequence);
        const std::int64_t mirrorStarted = ReadPerformanceCounter();
        desktopMirror.Render(consumer.GetLocalTextures());
        totalDesktopMirrorQpcTicks +=
            ReadPerformanceCounter() - mirrorStarted;
        ++desktopMirrorCount;
        // The x86 producer publishes placement before frameSequence. Pair the
        // payload with the just-consumed frame so the OpenXR panel and the
        // client-side controller ray always use one menu anchor.
        MemoryBarrier();
        acceptedUiReferenceMode =
            InterlockedCompareExchange(
                &block->frameUiReferenceMode,
                0,
                0) ==
                static_cast<LONG>(
                    bfvr::shared::UiReferenceMode::HeadLocked)
            ? bfvr::OpenXRUiReferenceMode::HeadLocked
            : bfvr::OpenXRUiReferenceMode::WorldLocked;
        acceptedUiWorldAnchorValid =
            acceptedUiReferenceMode ==
                bfvr::OpenXRUiReferenceMode::WorldLocked &&
            InterlockedCompareExchange(
                &block->frameUiWorldAnchorValid,
                0,
                0) != 0;
        if (acceptedUiWorldAnchorValid)
        {
            const bfvr::shared::SharedPresentationPose& source =
                block->frameUiWorldAnchor;
            acceptedUiWorldAnchor.orientationX = source.orientationX;
            acceptedUiWorldAnchor.orientationY = source.orientationY;
            acceptedUiWorldAnchor.orientationZ = source.orientationZ;
            acceptedUiWorldAnchor.orientationW = source.orientationW;
            acceptedUiWorldAnchor.positionX = source.positionX;
            acceptedUiWorldAnchor.positionY = source.positionY;
            acceptedUiWorldAnchor.positionZ = source.positionZ;
        }
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
    const auto currentUiWorldAnchor = [&]()
        -> const bfvr::OpenXRPresentationPose*
    {
        return acceptedUiWorldAnchorValid
            ? &acceptedUiWorldAnchor
            : nullptr;
    };
    const auto submitFrame = [&]()
    {
        const std::int64_t submitStarted = ReadPerformanceCounter();
        const bool submitted = presentation.SubmitFrame(
            consumer.GetLocalTextures(),
            acceptedUiReferenceMode,
            currentUiWorldAnchor());
        totalOpenXrSubmitOrEndQpcTicks +=
            ReadPerformanceCounter() - submitStarted;
        ++openXrSubmitOrEndCount;
        return submitted;
    };
    const auto endFrame = [&](const bfvr::OpenXRPresentationTextures& textures)
    {
        const std::int64_t endStarted = ReadPerformanceCounter();
        const bool ended = presentation.EndFrame(
            textures,
            acceptedUiReferenceMode,
            currentUiWorldAnchor());
        totalOpenXrSubmitOrEndQpcTicks +=
            ReadPerformanceCounter() - endStarted;
        ++openXrSubmitOrEndCount;
        return ended;
    };
    const auto beginFrame = [&](bfvr::OpenXRPresentationFrameState& frame)
    {
        const std::int64_t beginStarted = ReadPerformanceCounter();
        const bool began = presentation.BeginFrame(frame);
        totalOpenXrBeginQpcTicks +=
            ReadPerformanceCounter() - beginStarted;
        ++openXrBeginCount;
        return began;
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
        desktopMirror.PumpMessages();
        if (!presentation.PollEvents())
        {
            healthy = false;
            break;
        }

        if (runtimeTimedProducer)
        {
            if (pendingSourceSequence != 0)
            {
                const LONG availableSequence =
                    InterlockedCompareExchange(&block->frameSequence, 0, 0);
                if (availableSequence == pendingSourceSequence)
                {
                    if (!consumeSequence(availableSequence) ||
                        !submitFrame())
                    {
                        healthy = false;
                        break;
                    }
                    InterlockedIncrement(&block->presentedFrameCount);
                    InterlockedExchange(
                        &block->renderedFrameSequence,
                        pendingSourceSequence);
                    if (sourceGapReported)
                    {
                        fwprintf(
                            g_output,
                            L"[PRESENTER] Source frame %ld arrived after a transition gap; normal runtime-timed presentation resumed.\n",
                            pendingSourceSequence);
                        fflush(g_output);
                    }
                    completedRenderRequest = pendingSourceSequence;
                    pendingSourceSequence = 0;
                    sourceGapReported = false;
                    continue;
                }

                // BF1942 can legitimately finish a Present without an
                // eligible replay draw while it leaves a round, loads a
                // server, or rebuilds the spawn UI.  The x86 producer will
                // then request a newer runtime frame rather than publishing
                // pixels for the old one.  Do not wait forever for that
                // obsolete sequence: keep the OpenXR session alive, discard
                // only the unanswered request, and service the latest one.
                const LONG newestReadySequence = InterlockedCompareExchange(
                    &block->renderReadySequence,
                    0,
                    0);
                if (newestReadySequence > pendingSourceSequence)
                {
                    fwprintf(
                        g_output,
                        L"[PRESENTER] Superseding unanswered transition source frame %ld with newer render request %ld; OpenXR session remains active.\n",
                        pendingSourceSequence,
                        newestReadySequence);
                    fflush(g_output);
                    pendingSourceSequence = 0;
                    sourceGapReported = false;
                    continue;
                }

                // Do not leave an OpenXR frame open merely because BF1942 has
                // no eligible world draw during death, spectator, or loading.
                // Re-submit the last accepted textures while preserving the
                // outstanding source sequence for a later acknowledgement.
                if (haveFrame)
                {
                    if (!submitFrame())
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
                continue;
            }

            const LONG readySequence =
                InterlockedCompareExchange(&block->renderReadySequence, 0, 0);
            if (!presentation.IsSessionRunning() ||
                readySequence == completedRenderRequest)
            {
                Sleep(1);
                continue;
            }

            bfvr::OpenXRPresentationFrameState frame = {};
            if (!beginFrame(frame))
            {
                healthy = false;
                break;
            }
            if (!frame.shouldRender || !frame.viewsValid)
            {
                endFrame({});
                continue;
            }
            PublishRenderRequest(*block, readySequence, frame);

            const DWORD renderWaitStarted = GetTickCount();
            LONG availableSequence = 0;
            const DWORD sourceWaitLimit = runUntilStopped
                ? kRuntimeTimedSourceGraceMs
                : 2000;
            while (GetTickCount() - renderWaitStarted < sourceWaitLimit &&
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
                if (!runUntilStopped)
                {
                    healthy = false;
                    break;
                }
                // End this runtime frame with the last accepted scene instead
                // of holding xrBeginFrame open. The matching source remains
                // outstanding and is acknowledged when BF1942 renders again.
                const bool submittedFallback = haveFrame
                    ? endFrame(consumer.GetLocalTextures())
                    : endFrame({});
                if (haveFrame && !submittedFallback)
                {
                    healthy = false;
                    break;
                }
                if (haveFrame)
                {
                    InterlockedIncrement(&block->presentedFrameCount);
                }
                pendingSourceSequence = readySequence;
                if (!sourceGapReported)
                {
                    fwprintf(
                        g_output,
                        L"[PRESENTER] Runtime source frame %ld did not arrive within %lu ms; continuing to submit the last accepted image until BF1942 produces a new eligible draw.\n",
                        readySequence,
                        static_cast<unsigned long>(sourceWaitLimit));
                    fflush(g_output);
                    sourceGapReported = true;
                }
                continue;
            }
            if (!consumeSequence(availableSequence) ||
                !endFrame(consumer.GetLocalTextures()))
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
            if (!submitFrame())
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
    desktopMirror.Shutdown();
    consumer.Shutdown();
    presentation.Shutdown();
    const LONG presentedFrames =
        InterlockedCompareExchange(&block->presentedFrameCount, 0, 0);
    LARGE_INTEGER frequency = {};
    if (QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0)
    {
        const double millisecondsPerTick =
            1000.0 / static_cast<double>(frequency.QuadPart);
        const auto averageMilliseconds = [millisecondsPerTick](
            std::int64_t ticks,
            LONG count)
        {
            return count > 0
                ? static_cast<double>(ticks) * millisecondsPerTick /
                    static_cast<double>(count)
                : 0.0;
        };
        fwprintf(
            g_output,
            L"[PRESENTER] Stage timing: sourceConsume=%.3f ms/source (n=%ld) desktopMirror=%.3f ms/source (n=%ld) xrBegin=%.3f ms/call (n=%ld) xrSubmitOrEnd=%.3f ms/call (n=%ld).\n",
            averageMilliseconds(totalSourceConsumeQpcTicks, sourceConsumeCount),
            sourceConsumeCount,
            averageMilliseconds(totalDesktopMirrorQpcTicks, desktopMirrorCount),
            desktopMirrorCount,
            averageMilliseconds(totalOpenXrBeginQpcTicks, openXrBeginCount),
            openXrBeginCount,
            averageMilliseconds(
                totalOpenXrSubmitOrEndQpcTicks,
                openXrSubmitOrEndCount),
            openXrSubmitOrEndCount);
        fflush(g_output);
    }
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
