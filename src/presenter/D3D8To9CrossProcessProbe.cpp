#include "presenter/D3D8To9CrossProcessProbe.h"

#include "presenter/SharedControlChannel.h"
#include "presenter/SharedPresentationProtocol.h"

#include <dxgiformat.h>

#include <array>
#include <algorithm>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

namespace
{
constexpr UINT kTextureWidth = 1404;
constexpr UINT kTextureHeight = 1512;

std::wstring QuoteArgument(const wchar_t* argument)
{
    return std::wstring(L"\"") + argument + L"\"";
}

bool IsProcessRunning(HANDLE process)
{
    return process != nullptr &&
        WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
}

bool LaunchConsumer(
    const wchar_t* consumerPath,
    const wchar_t* channelName,
    const wchar_t* logPath,
    PROCESS_INFORMATION& process)
{
    std::wstring command =
        QuoteArgument(consumerPath) +
        L" --channel " + QuoteArgument(channelName) +
        L" --duration-ms 10000";
    if (logPath != nullptr && *logPath != L'\0')
    {
        command += L" --log " + QuoteArgument(logPath);
    }
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    process = {};
    return CreateProcessW(
        consumerPath,
        mutableCommand.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startup,
        &process) != FALSE;
}

bool WaitForState(
    const bfvr::shared::ControlBlock& block,
    HANDLE process,
    bfvr::shared::ProcessState expected,
    DWORD timeoutMs)
{
    const DWORD startedAt = GetTickCount();
    while (GetTickCount() - startedAt < timeoutMs)
    {
        const bfvr::shared::ProcessState state =
            bfvr::shared::ReadState(&block.presenterState);
        if (state == expected)
        {
            return true;
        }
        if (state == bfvr::shared::ProcessState::Failed ||
            !IsProcessRunning(process))
        {
            return false;
        }
        Sleep(2);
    }
    return false;
}

template <typename T>
void ReleaseInterface(T*& interfacePointer)
{
    if (interfacePointer != nullptr)
    {
        interfacePointer->Release();
        interfacePointer = nullptr;
    }
}
} // namespace

namespace bfvr::shared
{
bool RunD3D8To9CrossProcessProbe(
    IDirect3DDevice8* device,
    BFVRD3D8To9CreateSharedRenderTargetFn createSharedTarget,
    BFVRD3D8To9CreateTextureBackedDepthStencilFn createDepthTarget,
    BFVRD3D8To9ResolveDepthToSharedTargetFn resolveDepthTarget,
    BFVRD3D8To9WaitForGpuFn waitForGpu,
    const wchar_t* consumerPath,
    const wchar_t* consumerLogPath,
    bool enableAmbientOcclusion,
    bool enableWaterReflections)
{
    const bool enableDepthEffects =
        enableAmbientOcclusion || enableWaterReflections;
    if (device == nullptr ||
        createSharedTarget == nullptr ||
        waitForGpu == nullptr ||
        (enableDepthEffects &&
            (createDepthTarget == nullptr || resolveDepthTarget == nullptr)) ||
        consumerPath == nullptr ||
        *consumerPath == L'\0')
    {
        return false;
    }

    constexpr std::array<D3DFORMAT, kTextureCount> kFormats = {
        D3DFMT_A2B10G10R10,
        D3DFMT_A2B10G10R10,
        D3DFMT_A16B16G16R16F};
    constexpr std::array<DXGI_FORMAT, kTextureCount> kDxgiFormats = {
        DXGI_FORMAT_R10G10B10A2_UNORM,
        DXGI_FORMAT_R10G10B10A2_UNORM,
        DXGI_FORMAT_R16G16B16A16_FLOAT};
    constexpr std::array<D3DCOLOR, kTextureCount> kClearColors = {
        0xFF123456u,
        0xFF654321u,
        0x9E1452EBu};

    std::array<IDirect3DSurface8*, kTextureCount> surfaces = {};
    std::array<HANDLE, kTextureCount> handles = {};
    std::array<IDirect3DSurface8*, kDepthTextureCount> depthSurfaces = {};
    std::array<IDirect3DSurface8*, kDepthTextureCount> depthExports = {};
    std::array<HANDLE, kDepthTextureCount> depthHandles = {};
    IDirect3DSurface8* priorColor = nullptr;
    IDirect3DSurface8* priorDepth = nullptr;
    D3DVIEWPORT8 priorViewport = {};
    SharedControlChannel channel;
    PROCESS_INFORMATION consumer = {};
    ControlBlock* block = nullptr;
    wchar_t channelName[96] = {};
    HRESULT priorColorResult = E_FAIL;
    HRESULT priorDepthResult = E_FAIL;
    HRESULT priorViewportResult = E_FAIL;
    bool succeeded = false;

    for (std::size_t index = 0; index < surfaces.size(); ++index)
    {
        const HRESULT result = createSharedTarget(
            device,
            kTextureWidth,
            kTextureHeight,
            kFormats[index],
            &handles[index],
            reinterpret_cast<void**>(&surfaces[index]));
        if (FAILED(result) ||
            handles[index] == nullptr ||
            surfaces[index] == nullptr)
        {
            fwprintf(
                stderr,
                L"[FAIL] Cross-process shared target %zu creation returned 0x%08lX.\n",
                index,
                static_cast<unsigned long>(result));
            goto cleanup;
        }
    }
    if (enableDepthEffects)
    {
        for (std::size_t eye = 0; eye < depthSurfaces.size(); ++eye)
        {
            HRESULT result = createSharedTarget(
                device,
                kTextureWidth,
                kTextureHeight,
                D3DFMT_A8R8G8B8,
                &depthHandles[eye],
                reinterpret_cast<void**>(&depthExports[eye]));
            result = SUCCEEDED(result)
                ? createDepthTarget(
                    device,
                    kTextureWidth,
                    kTextureHeight,
                    D3DFMT_A2B10G10R10,
                    reinterpret_cast<void**>(&depthSurfaces[eye]))
                : result;
            if (FAILED(result) || depthHandles[eye] == nullptr ||
                depthExports[eye] == nullptr || depthSurfaces[eye] == nullptr)
            {
                fwprintf(
                    stderr,
                    L"[FAIL] Cross-process AO depth resources for eye %zu returned 0x%08lX.\n",
                    eye,
                    static_cast<unsigned long>(result));
                goto cleanup;
            }
        }
    }

    priorColorResult = device->GetRenderTarget(&priorColor);
    priorDepthResult = device->GetDepthStencilSurface(&priorDepth);
    priorViewportResult = device->GetViewport(&priorViewport);
    if (FAILED(priorColorResult) ||
        (FAILED(priorDepthResult) && priorDepth != nullptr) ||
        FAILED(priorViewportResult))
    {
        fwprintf(stderr, L"[FAIL] Cross-process probe could not snapshot D3D8 target state.\n");
        goto cleanup;
    }
    for (std::size_t index = 0; index < surfaces.size(); ++index)
    {
        IDirect3DSurface8* const depth =
            enableDepthEffects && index < depthSurfaces.size()
            ? depthSurfaces[index]
            : nullptr;
        HRESULT result = device->SetRenderTarget(surfaces[index], depth);
        if (SUCCEEDED(result))
        {
            result = device->Clear(
                0,
                nullptr,
                D3DCLEAR_TARGET |
                    (depth != nullptr ? D3DCLEAR_ZBUFFER : 0),
                kClearColors[index],
                0.5F,
                0);
        }
        if (FAILED(result))
        {
            fwprintf(
                stderr,
                L"[FAIL] Cross-process shared target %zu clear returned 0x%08lX.\n",
                index,
                static_cast<unsigned long>(result));
            goto cleanup;
        }
    }
    if (FAILED(device->SetRenderTarget(priorColor, priorDepth)) ||
        FAILED(device->SetViewport(&priorViewport)))
    {
        fwprintf(stderr, L"[FAIL] Cross-process probe target restoration failed.\n");
        goto cleanup;
    }
    if (enableDepthEffects)
    {
        for (std::size_t eye = 0; eye < depthSurfaces.size(); ++eye)
        {
            HRESULT result = device->SetRenderTarget(depthExports[eye], nullptr);
            if (SUCCEEDED(result))
            {
                result = device->Clear(
                    0,
                    nullptr,
                    D3DCLEAR_TARGET,
                    0x00000000u,
                    1.0F,
                    0);
            }
            BFVRD3D8To9DepthExportTiming timing = {};
            timing.size = sizeof(timing);
            if (SUCCEEDED(result))
            {
                result = resolveDepthTarget(
                    device,
                    depthSurfaces[eye],
                    depthExports[eye],
                    static_cast<DWORD>(
                        BFVRD3D8To9DepthExportEncoding::PackedRgba8),
                    &timing);
            }
            if (FAILED(result))
            {
                fwprintf(
                    stderr,
                    L"[FAIL] Cross-process packed-depth/mask preparation for eye %zu returned 0x%08lX.\n",
                    eye,
                    static_cast<unsigned long>(result));
                goto cleanup;
            }
            wprintf(
                L"[PACKED-DEPTH] eye=%zu gpu=%.4f ms valid=%d disjoint=%d.\n",
                eye,
                timing.elapsedMilliseconds,
                timing.gpuTimestampsValid ? 1 : 0,
                timing.gpuTimestampDisjoint ? 1 : 0);
        }
        if (FAILED(device->SetRenderTarget(priorColor, priorDepth)) ||
            FAILED(device->SetViewport(&priorViewport)))
        {
            fwprintf(stderr, L"[FAIL] Cross-process probe target restoration after depth export failed.\n");
            goto cleanup;
        }
    }
    if (FAILED(waitForGpu(device, 5000)))
    {
        fwprintf(stderr, L"[FAIL] Cross-process probe GPU completion failed.\n");
        goto cleanup;
    }

    swprintf_s(
        channelName,
        L"Local\\BFVR-D3D9Ex-%08lX-%08lX",
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long>(GetTickCount()));
    if (!channel.Create(channelName, GetCurrentProcessId()))
    {
        fwprintf(
            stderr,
            L"[FAIL] Cross-process channel creation failed (%lu).\n",
            channel.LastErrorCode());
        goto cleanup;
    }
    block = channel.Get();
    if (!LaunchConsumer(
            consumerPath,
            channelName,
            consumerLogPath,
            consumer))
    {
        fwprintf(
            stderr,
            L"[FAIL] Cross-process x64 consumer launch failed (%lu).\n",
            GetLastError());
        goto cleanup;
    }
    CloseHandle(consumer.hThread);
    consumer.hThread = nullptr;
    if (!WaitForState(
            *block,
            consumer.hProcess,
            ProcessState::RequirementsReady,
            10000))
    {
        fwprintf(
            stderr,
            L"[FAIL] Cross-process x64 consumer did not publish requirements (state=%ld error=%ld).\n",
            static_cast<long>(ReadState(&block->presenterState)),
            InterlockedCompareExchange(&block->presenterError, 0, 0));
        goto cleanup;
    }

    for (std::size_t index = 0; index < kTextureCount; ++index)
    {
        SharedTextureDescription& description = block->textures[index];
        description.width = kTextureWidth;
        description.height = kTextureHeight;
        description.format = static_cast<DWORD>(kDxgiFormats[index]);
        description.transport =
            static_cast<DWORD>(SharedTextureTransport::D3D9LegacyHandle);
        StoreLegacySharedHandle(description, handles[index]);
    }
    if (enableDepthEffects)
    {
        constexpr float nearPlane = 0.1F;
        constexpr float farPlane = 100.0F;
        const float depthScale = farPlane / (farPlane - nearPlane);
        const float depthOffset = -nearPlane * farPlane /
            (farPlane - nearPlane);
        const float projection[16] = {
            1.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, depthScale, 1.0F,
            0.0F, 0.0F, depthOffset, 0.0F};
        for (std::size_t eye = 0; eye < depthSurfaces.size(); ++eye)
        {
            SharedTextureDescription& description = block->depthTextures[eye];
            description.width = kTextureWidth;
            description.height = kTextureHeight;
            description.format = DXGI_FORMAT_B8G8R8A8_UNORM;
            description.transport = static_cast<DWORD>(
                SharedTextureTransport::D3D9LegacyHandle);
            StoreLegacySharedHandle(description, depthHandles[eye]);
            std::copy(
                std::begin(projection),
                std::end(projection),
                block->frameDepth.projections[eye]);
        }
        InterlockedExchange(
            &block->depthEncoding,
            static_cast<LONG>(DepthEncoding::PackedDeviceDepthBgra8));
        InterlockedExchange(
            &block->depthTextureCount,
            static_cast<LONG>(kDepthTextureCount));
        InterlockedExchange(&block->frameDepthValid, 1);
        if (enableWaterReflections)
            InterlockedExchange(&block->frameWaterMaskValid, 1);
    }
    block->producerFlags =
        (enableAmbientOcclusion
            ? kProducerFlagAmbientOcclusionRequested
            : 0u) |
        (enableWaterReflections
            ? kProducerFlagWaterReflectionsRequested
            : 0u);
    PublishState(&block->producerState, ProcessState::TexturesReady);
    (void)channel.SignalProducerUpdate();
    InterlockedExchange(&block->producedFrameCount, 1);
    MemoryBarrier();
    InterlockedExchange(&block->frameSequence, 1);
    (void)channel.SignalProducerUpdate();

    {
        const DWORD startedAt = GetTickCount();
        while (GetTickCount() - startedAt < 5000 &&
            InterlockedCompareExchange(
                &block->consumedFrameSequence,
                0,
                0) != 1 &&
            IsProcessRunning(consumer.hProcess))
        {
            if (channel.WaitForPresenterUpdate(20) == WAIT_FAILED)
            {
                Sleep(2);
            }
        }
    }
    {
        const std::array<DWORD, kTextureCount> pixels = {
            block->firstConsumedPixels[0],
            block->firstConsumedPixels[1],
            block->firstConsumedPixels[2]};
        succeeded =
            InterlockedCompareExchange(
                &block->consumedFrameSequence,
                0,
                0) == 1 &&
            InterlockedCompareExchange(
                &block->transportedFrameCount,
                0,
                0) == 1 &&
            pixels == kClearColors;
        wprintf(
            L"[CROSS-PROCESS] consumed=%ld transported=%ld pixels=[%08lX,%08lX,%08lX] exact=%d.\n",
            InterlockedCompareExchange(
                &block->consumedFrameSequence,
                0,
                0),
            InterlockedCompareExchange(
                &block->transportedFrameCount,
                0,
                0),
            static_cast<unsigned long>(pixels[0]),
            static_cast<unsigned long>(pixels[1]),
            static_cast<unsigned long>(pixels[2]),
            succeeded ? 1 : 0);
    }

cleanup:
    if (block != nullptr)
    {
        PublishState(
            &block->producerState,
            succeeded ? ProcessState::Stopping : ProcessState::Failed);
        InterlockedExchange(&block->shutdownRequested, 1);
        (void)channel.SignalProducerUpdate();
    }
    if (consumer.hProcess != nullptr)
    {
        if (WaitForSingleObject(consumer.hProcess, 5000) == WAIT_TIMEOUT)
        {
            TerminateProcess(consumer.hProcess, 9);
            WaitForSingleObject(consumer.hProcess, 2000);
            succeeded = false;
        }
        DWORD exitCode = STILL_ACTIVE;
        GetExitCodeProcess(consumer.hProcess, &exitCode);
        CloseHandle(consumer.hProcess);
        consumer.hProcess = nullptr;
        succeeded = succeeded && exitCode == 0;
    }
    channel.Close();
    ReleaseInterface(priorDepth);
    ReleaseInterface(priorColor);
    for (IDirect3DSurface8*& surface : surfaces)
    {
        ReleaseInterface(surface);
    }
    for (IDirect3DSurface8*& surface : depthExports)
        ReleaseInterface(surface);
    for (IDirect3DSurface8*& surface : depthSurfaces)
        ReleaseInterface(surface);
    wprintf(
        succeeded
            ? L"[PASS] Cross-process D3D8->D3D9Ex shared targets opened and converted by the x64 D3D11 consumer without CPU pixel transport.\n"
            : L"[FAIL] Cross-process D3D9Ex shared-target transport did not satisfy its exact contract.\n");
    return succeeded;
}
} // namespace bfvr::shared
