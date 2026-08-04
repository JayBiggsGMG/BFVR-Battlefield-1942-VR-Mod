#include "presenter/SharedControlChannel.h"
#include "presenter/SharedTextureConsumer.h"

#include <windows.h>

#include <d3d11.h>
#include <dxgi.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cwchar>

namespace
{
FILE* g_output = stdout;

void WriteLog(void*, const wchar_t* message)
{
    fwprintf(g_output, L"[X64-CONSUMER] %s\n", message);
    fflush(g_output);
}

bool CreateTestDevice(
    ID3D11Device** device,
    ID3D11DeviceContext** context,
    D3D_FEATURE_LEVEL& featureLevel,
    LUID& adapterLuid)
{
    constexpr std::array<D3D_FEATURE_LEVEL, 4> levels = {
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0};
    HRESULT result = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        levels.data(),
        static_cast<UINT>(levels.size()),
        D3D11_SDK_VERSION,
        device,
        &featureLevel,
        context);
    if (FAILED(result) || *device == nullptr || *context == nullptr)
    {
        return false;
    }

    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter* adapter = nullptr;
    DXGI_ADAPTER_DESC description = {};
    result = (*device)->QueryInterface(
        __uuidof(IDXGIDevice),
        reinterpret_cast<void**>(&dxgiDevice));
    if (SUCCEEDED(result))
    {
        result = dxgiDevice->GetAdapter(&adapter);
    }
    if (SUCCEEDED(result))
    {
        result = adapter->GetDesc(&description);
    }
    if (adapter != nullptr)
    {
        adapter->Release();
    }
    if (dxgiDevice != nullptr)
    {
        dxgiDevice->Release();
    }
    if (FAILED(result))
    {
        return false;
    }
    adapterLuid = description.AdapterLuid;
    return true;
}

void PublishTestRequirements(
    bfvr::shared::ControlBlock& block,
    const LUID& adapterLuid,
    D3D_FEATURE_LEVEL featureLevel)
{
    // Match the currently observed Quest 3 recommendation so this offline
    // control exercises realistic allocation/copy volume without opening XR.
    constexpr DWORD kWidth = 1872;
    constexpr DWORD kHeight = 2016;
    block.requirements.leftWorldWidth = kWidth;
    block.requirements.leftWorldHeight = kHeight;
    block.requirements.rightWorldWidth = kWidth;
    block.requirements.rightWorldHeight = kHeight;
    block.requirements.uiWidth = kWidth;
    block.requirements.uiHeight = kHeight;
    block.requirements.format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    block.requirements.adapterLuidHigh = adapterLuid.HighPart;
    block.requirements.adapterLuidLow = adapterLuid.LowPart;
    block.requirements.minimumFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    block.requirements.deviceFeatureLevel = static_cast<DWORD>(featureLevel);
    bfvr::shared::PublishState(
        &block.presenterState,
        bfvr::shared::ProcessState::RequirementsReady);
}

int RunConsumer(const wchar_t* channelName, DWORD durationMs)
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
    block->presenterProcessId = GetCurrentProcessId();
    bfvr::shared::PublishState(
        &block->presenterState,
        bfvr::shared::ProcessState::Starting);

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_9_1;
    LUID adapterLuid = {};
    if (!CreateTestDevice(&device, &context, featureLevel, adapterLuid))
    {
        InterlockedExchange(&block->presenterError, 20);
        bfvr::shared::PublishState(
            &block->presenterState,
            bfvr::shared::ProcessState::Failed);
        return 20;
    }
    PublishTestRequirements(*block, adapterLuid, featureLevel);
    (void)channel.SignalPresenterUpdate();
    fwprintf(
        g_output,
        L"[X64-CONSUMER] Published offline requirements: adapter=%08lX:%08lX featureLevel=0x%04X.\n",
        static_cast<unsigned long>(adapterLuid.HighPart),
        static_cast<unsigned long>(adapterLuid.LowPart),
        static_cast<unsigned int>(featureLevel));
    fflush(g_output);

    const DWORD waitStarted = GetTickCount();
    const DWORD textureWaitTimeoutMs =
        (block->producerFlags &
            bfvr::shared::kProducerFlagRuntimeTimedRender) != 0
        ? 60000
        : 15000;
    while (bfvr::shared::ReadState(&block->producerState) !=
        bfvr::shared::ProcessState::TexturesReady)
    {
        if (GetTickCount() - waitStarted >= textureWaitTimeoutMs ||
            bfvr::shared::ReadState(&block->producerState) ==
                bfvr::shared::ProcessState::Failed ||
            InterlockedCompareExchange(&block->shutdownRequested, 0, 0) != 0)
        {
            InterlockedExchange(&block->presenterError, 21);
            bfvr::shared::PublishState(
                &block->presenterState,
                bfvr::shared::ProcessState::Failed);
            context->Release();
            device->Release();
            return 21;
        }
        if (channel.WaitForProducerUpdate(2) == WAIT_FAILED)
        {
            Sleep(2);
        }
    }

    const LONG publishedDepthCount = InterlockedCompareExchange(
        &block->depthTextureCount,
        0,
        0);
    const LONG publishedDepthEncoding = InterlockedCompareExchange(
        &block->depthEncoding,
        0,
        0);
    bfvr::shared::SharedTextureConsumer consumer;
    if (!consumer.Initialize(
            device,
            context,
            block->textures,
            bfvr::shared::kTextureCount,
            block->depthTextures,
            publishedDepthCount > 0
                ? static_cast<std::size_t>(publishedDepthCount)
                : 0,
            static_cast<bfvr::shared::DepthEncoding>(
                publishedDepthEncoding),
            block->producerFlags,
            block->requirements,
            WriteLog,
            nullptr))
    {
        InterlockedExchange(&block->presenterError, 22);
        bfvr::shared::PublishState(
            &block->presenterState,
            bfvr::shared::ProcessState::Failed);
        context->Release();
        device->Release();
        return 22;
    }
    bfvr::shared::PublishState(
        &block->presenterState,
        bfvr::shared::ProcessState::Running);
    (void)channel.SignalPresenterUpdate();

    LONG consumedSequence = 0;
    LONG completedRenderRequest = 0;
    bool sampled = false;
    bool healthy = true;
    const DWORD startedAt = GetTickCount();
    while (GetTickCount() - startedAt < durationMs &&
        InterlockedCompareExchange(&block->shutdownRequested, 0, 0) == 0)
    {
        const bool runtimeTimed =
            (block->producerFlags &
             bfvr::shared::kProducerFlagRuntimeTimedRender) != 0;
        LONG availableSequence =
            InterlockedCompareExchange(&block->frameSequence, 0, 0);
        if (runtimeTimed)
        {
            const LONG readySequence =
                InterlockedCompareExchange(
                    &block->renderReadySequence,
                    0,
                    0);
            if (readySequence == completedRenderRequest)
            {
                if (channel.WaitForProducerUpdate(2) == WAIT_FAILED)
                {
                    Sleep(1);
                }
                continue;
            }
            block->renderRequest.predictedDisplayTime =
                static_cast<LONGLONG>(GetTickCount64()) * 1000000;
            block->renderRequest.shouldRender = 1;
            block->renderRequest.viewsValid = 1;
            for (std::size_t eye = 0; eye < 2; ++eye)
            {
                auto& view = block->renderRequest.views[eye];
                view.pose.orientationW = 1.0F;
                view.pose.positionX = eye == 0 ? -0.032F : 0.032F;
                view.fov.angleLeft = -0.8F;
                view.fov.angleRight = 0.8F;
                view.fov.angleUp = 0.8F;
                view.fov.angleDown = -0.8F;
            }
            MemoryBarrier();
            InterlockedExchange(
                &block->renderRequestSequence,
                readySequence);
            (void)channel.SignalPresenterUpdate();
            const DWORD frameWaitStarted = GetTickCount();
            while (GetTickCount() - frameWaitStarted < 1000)
            {
                availableSequence =
                    InterlockedCompareExchange(
                        &block->frameSequence,
                        0,
                        0);
                if (availableSequence == readySequence)
                {
                    break;
                }
                const DWORD elapsed = GetTickCount() - frameWaitStarted;
                const DWORD remaining = elapsed < 1000 ? 1000 - elapsed : 0;
                const DWORD waitSlice = (std::min)(remaining, 20UL);
                if (channel.WaitForProducerUpdate(waitSlice) == WAIT_FAILED)
                {
                    Sleep((std::min)(remaining, 1UL));
                }
            }
            if (availableSequence != readySequence)
            {
                fwprintf(
                    g_output,
                    L"[X64-CONSUMER] Runtime frame wait failed: ready=%ld available=%ld frame=%ld producerState=%ld presenterState=%ld shutdown=%ld.\n",
                    readySequence,
                    availableSequence,
                    InterlockedCompareExchange(
                        &block->frameSequence,
                        0,
                        0),
                    static_cast<long>(
                        bfvr::shared::ReadState(
                            &block->producerState)),
                    static_cast<long>(
                        bfvr::shared::ReadState(
                            &block->presenterState)),
                    InterlockedCompareExchange(
                        &block->shutdownRequested,
                        0,
                        0));
                fflush(g_output);
                healthy = false;
                break;
            }
        }
        if (availableSequence == consumedSequence)
        {
            if (channel.WaitForProducerUpdate(2) == WAIT_FAILED)
            {
                Sleep(1);
            }
            continue;
        }
        MemoryBarrier();
        const LONG frameOverlayFlags = InterlockedCompareExchange(
            &block->frameOverlayFlags,
            0,
            0);
        const bool frameDepthValid = InterlockedCompareExchange(
            &block->frameDepthValid,
            0,
            0) != 0;
        const bfvr::shared::SharedDepthFrameParameters frameDepth =
            frameDepthValid
            ? block->frameDepth
            : bfvr::shared::SharedDepthFrameParameters{};
        const bool frameWaterMaskValid = InterlockedCompareExchange(
            &block->frameWaterMaskValid,
            0,
            0) != 0;
        if (!consumer.ConsumeFrame(
                frameOverlayFlags,
                frameDepthValid,
                frameDepthValid ? &frameDepth : nullptr,
                frameWaterMaskValid))
        {
            healthy = false;
            break;
        }
        if (!sampled)
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
                    L"[X64-CONSUMER] First local-copy pixels: left=%08lX right=%08lX UI=%08lX.\n",
                    static_cast<unsigned long>(pixels[0]),
                    static_cast<unsigned long>(pixels[1]),
                    static_cast<unsigned long>(pixels[2]));
                fflush(g_output);
                sampled = true;
            }
        }
        consumedSequence = availableSequence;
        InterlockedExchange(&block->consumedFrameSequence, consumedSequence);
        InterlockedIncrement(&block->transportedFrameCount);
        (void)channel.SignalPresenterUpdate();
        if (runtimeTimed)
        {
            completedRenderRequest = availableSequence;
            InterlockedExchange(
                &block->renderedFrameSequence,
                completedRenderRequest);
            (void)channel.SignalPresenterUpdate();
        }
    }

    bfvr::shared::PublishState(
        &block->presenterState,
        bfvr::shared::ProcessState::Stopping);
    consumer.Shutdown();
    context->Release();
    device->Release();
    const LONG transported =
        InterlockedCompareExchange(&block->transportedFrameCount, 0, 0);
    const bool succeeded = healthy && consumedSequence > 0 && transported > 0 && sampled;
    if (!succeeded)
    {
        InterlockedExchange(&block->presenterError, 23);
    }
    bfvr::shared::PublishState(
        &block->presenterState,
        succeeded
            ? bfvr::shared::ProcessState::Stopped
            : bfvr::shared::ProcessState::Failed);
    (void)channel.SignalPresenterUpdate();
    fwprintf(
        g_output,
        L"[X64-CONSUMER] Exit summary: healthy=%d consumedSequence=%ld transported=%ld.\n",
        healthy ? 1 : 0,
        consumedSequence,
        transported);
    fflush(g_output);
    return succeeded ? 0 : 23;
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    const wchar_t* channelName = nullptr;
    DWORD durationMs = 60000;
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
        }
        else if (wcscmp(argv[index], L"--log") == 0 && index + 1 < argc)
        {
            if (_wfreopen_s(&g_output, argv[++index], L"w, ccs=UTF-8", stdout) != 0)
            {
                return 2;
            }
        }
        else
        {
            fwprintf(
                stderr,
                L"Usage: BFVRSharedTextureConsumerProbe --channel <name> [--duration-ms <1000-120000>] [--log <path>]\n");
            return 2;
        }
    }
    if (channelName == nullptr)
    {
        return 2;
    }
    return RunConsumer(channelName, durationMs);
}
