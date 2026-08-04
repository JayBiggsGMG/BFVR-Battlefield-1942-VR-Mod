#include "presenter/D3D11TextureScaler.h"

#include <windows.h>

#include <d3d11.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cwchar>
#include <vector>

namespace
{
void WriteLog(void*, const wchar_t* message)
{
    wprintf(L"[BLOOM] %s\n", message);
}

template <typename T>
void ReleaseInterface(T*& value)
{
    if (value != nullptr)
    {
        value->Release();
        value = nullptr;
    }
}

bool CreateDevice(
    ID3D11Device** device,
    ID3D11DeviceContext** context,
    D3D_FEATURE_LEVEL& featureLevel)
{
    constexpr std::array<D3D_FEATURE_LEVEL, 4> levels = {
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0};
    return SUCCEEDED(D3D11CreateDevice(
               nullptr,
               D3D_DRIVER_TYPE_HARDWARE,
               nullptr,
               D3D11_CREATE_DEVICE_BGRA_SUPPORT,
               levels.data(),
               static_cast<UINT>(levels.size()),
               D3D11_SDK_VERSION,
               device,
               &featureLevel,
               context)) &&
        *device != nullptr && *context != nullptr;
}

bool CreateTarget(
    ID3D11Device* device,
    UINT width,
    UINT height,
    ID3D11Texture2D** texture,
    ID3D11RenderTargetView** target,
    ID3D11Texture2D** staging)
{
    D3D11_TEXTURE2D_DESC description = {};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET;
    HRESULT result = device->CreateTexture2D(&description, nullptr, texture);
    result = SUCCEEDED(result)
        ? device->CreateRenderTargetView(*texture, nullptr, target)
        : result;
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    result = SUCCEEDED(result)
        ? device->CreateTexture2D(&description, nullptr, staging)
        : result;
    return SUCCEEDED(result) && *texture != nullptr && *target != nullptr &&
        *staging != nullptr;
}

bool WaitForGpuCopy(
    ID3D11DeviceContext* context,
    ID3D11Texture2D* source,
    ID3D11Texture2D* staging)
{
    context->CopyResource(staging, source);
    context->Flush();
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)))
        return false;
    context->Unmap(staging, 0);
    return true;
}

bool ReadPixel(
    ID3D11DeviceContext* context,
    ID3D11Texture2D* staging,
    UINT x,
    UINT y,
    DWORD& pixel)
{
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)))
        return false;
    const auto* row = static_cast<const unsigned char*>(mapped.pData) +
        static_cast<std::size_t>(y) * mapped.RowPitch;
    pixel = reinterpret_cast<const DWORD*>(row)[x];
    context->Unmap(staging, 0);
    return true;
}

unsigned Channel(DWORD pixel)
{
    return pixel & 0xFFU;
}

int Run(UINT width, UINT height, UINT iterations)
{
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_9_1;
    if (!CreateDevice(&device, &context, featureLevel))
    {
        fwprintf(stderr, L"[FAIL] Could not create a D3D11 hardware device.\n");
        return 3;
    }

    const UINT centerX = width / 2;
    const UINT centerY = height / 2;
    const UINT halfBright = (std::max)(4U, (std::min)(width, height) / 128U);
    std::vector<DWORD> sourcePixels(
        static_cast<std::size_t>(width) * height,
        0xFF101010U);
    for (UINT y = centerY - halfBright; y <= centerY + halfBright; ++y)
    {
        for (UINT x = centerX - halfBright; x <= centerX + halfBright; ++x)
            sourcePixels[static_cast<std::size_t>(y) * width + x] = 0xFFFFFFFFU;
    }

    D3D11_TEXTURE2D_DESC sourceDescription = {};
    sourceDescription.Width = width;
    sourceDescription.Height = height;
    sourceDescription.MipLevels = 1;
    sourceDescription.ArraySize = 1;
    sourceDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    sourceDescription.SampleDesc.Count = 1;
    sourceDescription.Usage = D3D11_USAGE_IMMUTABLE;
    sourceDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sourceData = {};
    sourceData.pSysMem = sourcePixels.data();
    sourceData.SysMemPitch = width * sizeof(DWORD);

    ID3D11Texture2D* source = nullptr;
    ID3D11ShaderResourceView* sourceView = nullptr;
    ID3D11Texture2D* baseline = nullptr;
    ID3D11RenderTargetView* baselineTarget = nullptr;
    ID3D11Texture2D* baselineStaging = nullptr;
    ID3D11Texture2D* bloom = nullptr;
    ID3D11RenderTargetView* bloomTarget = nullptr;
    ID3D11Texture2D* bloomStaging = nullptr;
    HRESULT result = device->CreateTexture2D(
        &sourceDescription,
        &sourceData,
        &source);
    result = SUCCEEDED(result)
        ? device->CreateShaderResourceView(source, nullptr, &sourceView)
        : result;
    bool ready = SUCCEEDED(result) && CreateTarget(
        device,
        width,
        height,
        &baseline,
        &baselineTarget,
        &baselineStaging) && CreateTarget(
        device,
        width,
        height,
        &bloom,
        &bloomTarget,
        &bloomStaging);

    bfvr::shared::D3D11TextureScaler scaler;
    ready = ready && scaler.Initialize(
        device,
        context,
        WriteLog,
        nullptr,
        true,
        false,
        false,
        false);
    const auto scale = [&](ID3D11RenderTargetView* target, bool applyBloom)
    {
        return scaler.ScaleAspectFit(
            sourceView,
            width,
            height,
            target,
            width,
            height,
            false,
            true,
            false,
            nullptr,
            0.0F,
            nullptr,
            0.0F,
            0.0F,
            nullptr,
            0.0F,
            applyBloom,
            0.55F,
            0.35F);
    };
    ready = ready && scale(baselineTarget, false) &&
        WaitForGpuCopy(context, baseline, baselineStaging);
    for (UINT iteration = 0; iteration < iterations && ready; ++iteration)
    {
        const bool timing = scaler.BeginBloomFrame();
        ready = scale(bloomTarget, true);
        if (timing)
            scaler.EndBloomFrame();
        ready = ready && WaitForGpuCopy(context, bloom, bloomStaging);
        if (timing)
            scaler.CollectBloomFrameTimings();
    }

    const UINT nearX = (std::min)(
        width - 1,
        centerX + halfBright + (std::max)(4U, width / 256U));
    DWORD baselineNear = 0;
    DWORD bloomNear = 0;
    DWORD bloomCenter = 0;
    ready = ready && ReadPixel(
        context, baselineStaging, nearX, centerY, baselineNear) &&
        ReadPixel(context, bloomStaging, nearX, centerY, bloomNear) &&
        ReadPixel(context, bloomStaging, centerX, centerY, bloomCenter);
    const unsigned baselineChannel = Channel(baselineNear);
    const unsigned bloomChannel = Channel(bloomNear);
    const bool visibleHalo = ready && bloomChannel >= baselineChannel + 4U &&
        Channel(bloomCenter) >= bloomChannel;

    scaler.Shutdown();
    ReleaseInterface(bloomStaging);
    ReleaseInterface(bloomTarget);
    ReleaseInterface(bloom);
    ReleaseInterface(baselineStaging);
    ReleaseInterface(baselineTarget);
    ReleaseInterface(baseline);
    ReleaseInterface(sourceView);
    ReleaseInterface(source);
    ReleaseInterface(context);
    ReleaseInterface(device);

    wprintf(
        visibleHalo
            ? L"[PASS] Quarter-resolution linear bloom produced a measurable halo outside the bright source at %ux%u: baseline=%u bloom=%u center=%u iterations=%u featureLevel=0x%X.\n"
            : L"[FAIL] Bloom halo validation failed at %ux%u: baseline=%u bloom=%u center=%u iterations=%u featureLevel=0x%X.\n",
        width,
        height,
        baselineChannel,
        bloomChannel,
        Channel(bloomCenter),
        iterations,
        static_cast<unsigned int>(featureLevel));
    return visibleHalo ? 0 : 4;
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    UINT width = 1872;
    UINT height = 2016;
    UINT iterations = 64;
    for (int index = 1; index < argc; ++index)
    {
        if (wcscmp(argv[index], L"--width") == 0 && index + 1 < argc)
            width = wcstoul(argv[++index], nullptr, 10);
        else if (wcscmp(argv[index], L"--height") == 0 && index + 1 < argc)
            height = wcstoul(argv[++index], nullptr, 10);
        else if (wcscmp(argv[index], L"--iterations") == 0 && index + 1 < argc)
            iterations = wcstoul(argv[++index], nullptr, 10);
        else
        {
            fwprintf(
                stderr,
                L"Usage: BFVRBloomGpuProbe [--width <64-4096>] [--height <64-4096>] [--iterations <1-1024>]\n");
            return 2;
        }
    }
    if (width < 64 || width > 4096 || height < 64 || height > 4096 ||
        iterations < 1 || iterations > 1024)
    {
        fwprintf(stderr, L"[FAIL] Probe dimensions or iteration count are out of range.\n");
        return 2;
    }
    return Run(width, height, iterations);
}
