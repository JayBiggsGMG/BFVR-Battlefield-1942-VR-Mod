#include "presenter/D3D11ScreenSpaceGlobalIllumination.h"

#include <d3d11.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <iterator>
#include <vector>

namespace
{
template <typename T>
void ReleaseInterface(T*& interfacePointer)
{
    if (interfacePointer != nullptr)
    {
        interfacePointer->Release();
        interfacePointer = nullptr;
    }
}

void WriteLog(void*, const wchar_t* message)
{
    if (message != nullptr)
        wprintf(L"[SSGI] %ls\n", message);
}

bool ParseUnsigned(
    const wchar_t* text,
    UINT minimum,
    UINT maximum,
    UINT& value)
{
    if (text == nullptr || *text == L'\0')
        return false;
    wchar_t* end = nullptr;
    const unsigned long parsed = wcstoul(text, &end, 10);
    if (end == text || *end != L'\0' || parsed < minimum || parsed > maximum)
        return false;
    value = static_cast<UINT>(parsed);
    return true;
}

std::uint8_t ToUnorm8(float value)
{
    return static_cast<std::uint8_t>(std::clamp(
        std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F),
        0L,
        255L));
}

std::uint32_t PackBgra(float red, float green, float blue)
{
    return static_cast<std::uint32_t>(ToUnorm8(blue)) |
        (static_cast<std::uint32_t>(ToUnorm8(green)) << 8) |
        (static_cast<std::uint32_t>(ToUnorm8(red)) << 16) |
        0xFF000000U;
}

std::uint32_t PackDeviceDepth(float depth)
{
    depth = (std::min)(depth, 0.99999994F);
    const float scaled1 = depth * 255.0F;
    const float scaled2 = depth * 65025.0F;
    float red = depth - std::floor(depth);
    float green = scaled1 - std::floor(scaled1);
    const float blue = scaled2 - std::floor(scaled2);
    red -= green / 255.0F;
    green -= blue / 255.0F;
    return static_cast<std::uint32_t>(ToUnorm8(blue)) |
        (static_cast<std::uint32_t>(ToUnorm8(green)) << 8) |
        (static_cast<std::uint32_t>(ToUnorm8(red)) << 16) |
        0xFF000000U;
}

float HalfToFloat(std::uint16_t value)
{
    const float sign = (value & 0x8000U) != 0 ? -1.0F : 1.0F;
    const unsigned int exponent = (value >> 10U) & 0x1FU;
    const unsigned int mantissa = value & 0x03FFU;
    if (exponent == 0)
        return sign * std::ldexp(static_cast<float>(mantissa), -24);
    if (exponent == 31)
        return sign * INFINITY;
    return sign * std::ldexp(
        1.0F + static_cast<float>(mantissa) / 1024.0F,
        static_cast<int>(exponent) - 15);
}

bool WaitForGpu(ID3D11Device* device, ID3D11DeviceContext* context)
{
    D3D11_QUERY_DESC description = {};
    description.Query = D3D11_QUERY_EVENT;
    ID3D11Query* query = nullptr;
    HRESULT result = device->CreateQuery(&description, &query);
    if (FAILED(result) || query == nullptr)
        return false;
    context->End(query);
    context->Flush();
    const DWORD startedAt = GetTickCount();
    while ((result = context->GetData(query, nullptr, 0, 0)) == S_FALSE &&
        GetTickCount() - startedAt < 5000)
    {
        SwitchToThread();
    }
    query->Release();
    return result == S_OK;
}

struct SceneBuffers
{
    std::vector<std::uint32_t> depth;
    std::vector<std::uint32_t> redSourceColor;
    std::vector<std::uint32_t> neutralSourceColor;
};

SceneBuffers BuildCornerScene(
    UINT width,
    UINT height,
    float xScale,
    float yScale,
    float depthScale,
    float depthOffset)
{
    SceneBuffers buffers;
    const std::size_t pixelCount = static_cast<std::size_t>(width) * height;
    buffers.depth.resize(pixelCount);
    buffers.redSourceColor.resize(pixelCount);
    buffers.neutralSourceColor.resize(pixelCount);
    const std::uint32_t receiverColor = PackBgra(0.16F, 0.16F, 0.16F);
    const std::uint32_t redSource = PackBgra(0.90F, 0.045F, 0.035F);
    const std::uint32_t neutralSource = PackBgra(0.055F, 0.055F, 0.055F);
    // Keep the back-wall junction at the same screen coordinate for landscape
    // probe sizes and BFVR's portrait-oriented native eye targets.
    const float sourceWallX = -1.25F / xScale;

    for (UINT y = 0; y < height; ++y)
    {
        for (UINT x = 0; x < width; ++x)
        {
            const float ndcX =
                (static_cast<float>(x) + 0.5F) * 2.0F /
                    static_cast<float>(width) -
                1.0F;
            const float ndcY = 1.0F -
                (static_cast<float>(y) + 0.5F) * 2.0F /
                    static_cast<float>(height);
            const float rayX = ndcX / xScale;
            const float rayY = ndcY / yScale;
            float nearestZ = 5.0F;
            bool sourceWall = false;

            if (rayX < -0.0001F)
            {
                const float wallZ = sourceWallX / rayX;
                const float wallY = rayY * wallZ;
                if (wallZ >= 2.0F && wallZ < nearestZ &&
                    wallY >= -1.8F && wallY <= 1.8F)
                {
                    nearestZ = wallZ;
                    sourceWall = true;
                }
            }

            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            buffers.depth[index] = PackDeviceDepth(
                depthScale + depthOffset / nearestZ);
            buffers.redSourceColor[index] =
                sourceWall ? redSource : receiverColor;
            buffers.neutralSourceColor[index] =
                sourceWall ? neutralSource : receiverColor;
        }
    }
    return buffers;
}

bool CreateImmutableBgraTexture(
    ID3D11Device* device,
    UINT width,
    UINT height,
    const std::vector<std::uint32_t>& pixels,
    ID3D11Texture2D** texture,
    ID3D11ShaderResourceView** view)
{
    D3D11_TEXTURE2D_DESC description = {};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data = {};
    data.pSysMem = pixels.data();
    data.SysMemPitch = width * sizeof(std::uint32_t);
    HRESULT result = device->CreateTexture2D(&description, &data, texture);
    result = SUCCEEDED(result)
        ? device->CreateShaderResourceView(*texture, nullptr, view)
        : result;
    return SUCCEEDED(result) && *texture != nullptr && *view != nullptr;
}

bool ReadbackRadiance(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    ID3D11ShaderResourceView* view,
    UINT width,
    UINT height,
    std::vector<float>& rgb)
{
    ID3D11Resource* resource = nullptr;
    view->GetResource(&resource);
    ID3D11Texture2D* texture = nullptr;
    HRESULT result = resource == nullptr
        ? E_FAIL
        : resource->QueryInterface(
            __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture));
    ReleaseInterface(resource);
    D3D11_TEXTURE2D_DESC description = {};
    if (SUCCEEDED(result))
        texture->GetDesc(&description);
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    description.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    result = SUCCEEDED(result)
        ? device->CreateTexture2D(&description, nullptr, &staging)
        : result;
    if (SUCCEEDED(result))
    {
        context->CopyResource(staging, texture);
        result = WaitForGpu(device, context) ? S_OK : E_FAIL;
    }
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    result = SUCCEEDED(result)
        ? context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)
        : result;
    if (SUCCEEDED(result))
    {
        rgb.resize(static_cast<std::size_t>(width) * height * 3);
        for (UINT y = 0; y < height; ++y)
        {
            const auto* row = reinterpret_cast<const std::uint16_t*>(
                static_cast<const std::uint8_t*>(mapped.pData) +
                static_cast<std::size_t>(y) * mapped.RowPitch);
            for (UINT x = 0; x < width; ++x)
            {
                const std::size_t destination =
                    (static_cast<std::size_t>(y) * width + x) * 3;
                rgb[destination + 0] = HalfToFloat(row[x * 4 + 0]);
                rgb[destination + 1] = HalfToFloat(row[x * 4 + 1]);
                rgb[destination + 2] = HalfToFloat(row[x * 4 + 2]);
            }
        }
        context->Unmap(staging, 0);
    }
    ReleaseInterface(staging);
    ReleaseInterface(texture);
    return SUCCEEDED(result);
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    UINT width = 640;
    UINT height = 480;
    UINT iterations = 32;
    for (int index = 1; index < argc; ++index)
    {
        if (wcscmp(argv[index], L"--width") == 0 && index + 1 < argc)
        {
            if (!ParseUnsigned(argv[++index], 64, 4096, width))
                return 2;
        }
        else if (wcscmp(argv[index], L"--height") == 0 && index + 1 < argc)
        {
            if (!ParseUnsigned(argv[++index], 64, 4096, height))
                return 2;
        }
        else if (wcscmp(argv[index], L"--iterations") == 0 &&
            index + 1 < argc)
        {
            if (!ParseUnsigned(argv[++index], 1, 256, iterations))
                return 2;
        }
        else
        {
            fwprintf(
                stderr,
                L"Usage: BFVRScreenSpaceGlobalIlluminationGpuProbe "
                L"[--width <64..4096>] [--height <64..4096>] "
                L"[--iterations <1..256>]\n");
            return 2;
        }
    }

    constexpr float nearPlane = 0.1F;
    constexpr float farPlane = 100.0F;
    constexpr float verticalFov = 1.20F;
    const float yScale = 1.0F / std::tan(verticalFov * 0.5F);
    const float xScale = yScale * static_cast<float>(height) /
        static_cast<float>(width);
    const float depthScale = farPlane / (farPlane - nearPlane);
    const float depthOffset = -nearPlane * farPlane / (farPlane - nearPlane);
    const float projection[16] = {
        xScale, 0.0F, 0.0F, 0.0F,
        0.0F, yScale, 0.0F, 0.0F,
        0.0F, 0.0F, depthScale, 1.0F,
        0.0F, 0.0F, depthOffset, 0.0F};
    const SceneBuffers scene = BuildCornerScene(
        width, height, xScale, yScale, depthScale, depthOffset);

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_9_1;
    const std::array<D3D_FEATURE_LEVEL, 2> levels = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0};
    HRESULT result = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        levels.data(),
        static_cast<UINT>(levels.size()),
        D3D11_SDK_VERSION,
        &device,
        &featureLevel,
        &context);
    if (FAILED(result) || device == nullptr || context == nullptr)
    {
        fwprintf(
            stderr,
            L"[FAIL] D3D11 device creation failed (HRESULT=0x%08lX).\n",
            static_cast<unsigned long>(result));
        ReleaseInterface(context);
        ReleaseInterface(device);
        return 1;
    }

    ID3D11Texture2D* depthTexture = nullptr;
    ID3D11ShaderResourceView* depthView = nullptr;
    ID3D11Texture2D* redTexture = nullptr;
    ID3D11ShaderResourceView* redView = nullptr;
    ID3D11Texture2D* neutralTexture = nullptr;
    ID3D11ShaderResourceView* neutralView = nullptr;
    bool passed =
        CreateImmutableBgraTexture(
            device,
            width,
            height,
            scene.depth,
            &depthTexture,
            &depthView) &&
        CreateImmutableBgraTexture(
            device,
            width,
            height,
            scene.redSourceColor,
            &redTexture,
            &redView) &&
        CreateImmutableBgraTexture(
            device,
            width,
            height,
            scene.neutralSourceColor,
            &neutralTexture,
            &neutralView);

    bfvr::shared::D3D11ScreenSpaceGlobalIllumination ssgi;
    passed = passed && ssgi.Initialize(device, context, WriteLog, nullptr);
    for (UINT iteration = 0; iteration < iterations && passed; ++iteration)
    {
        passed = ssgi.BeginFrame() &&
            ssgi.BuildEye(
                0, redView, false, depthView, width, height, projection) &&
            ssgi.BuildEye(
                1, neutralView, false, depthView, width, height, projection);
        ssgi.EndFrame();
        passed = passed && WaitForGpu(device, context);
        ssgi.CollectFrameTimings();
    }

    std::vector<float> redResult;
    std::vector<float> neutralResult;
    passed = passed && ReadbackRadiance(
        device,
        context,
        ssgi.GetEyeView(0),
        width,
        height,
        redResult);
    passed = passed && ReadbackRadiance(
        device,
        context,
        ssgi.GetEyeView(1),
        width,
        height,
        neutralResult);

    float maximumRedLift = 0.0F;
    float greenLiftAtMaximum = 0.0F;
    float blueLiftAtMaximum = 0.0F;
    double receiverRedLiftSum = 0.0;
    std::size_t receiverPixelCount = 0;
    std::size_t litReceiverPixelCount = 0;
    UINT maximumX = 0;
    UINT maximumY = 0;
    if (passed)
    {
        for (UINT y = 0; y < height; ++y)
        {
            for (UINT x = 0; x < width; ++x)
            {
                const std::size_t pixelIndex =
                    static_cast<std::size_t>(y) * width + x;
                if (scene.redSourceColor[pixelIndex] !=
                    scene.neutralSourceColor[pixelIndex])
                {
                    continue;
                }
                const std::size_t index =
                    pixelIndex * 3;
                const float redLift = redResult[index] - neutralResult[index];
                receiverRedLiftSum += redLift;
                ++receiverPixelCount;
                if (redLift >= 0.002F)
                    ++litReceiverPixelCount;
                if (redLift > maximumRedLift)
                {
                    maximumRedLift = redLift;
                    greenLiftAtMaximum =
                        redResult[index + 1] - neutralResult[index + 1];
                    blueLiftAtMaximum =
                        redResult[index + 2] - neutralResult[index + 2];
                    maximumX = x;
                    maximumY = y;
                }
            }
        }
    }
    const double receiverCoverage = receiverPixelCount > 0
        ? static_cast<double>(litReceiverPixelCount) /
            static_cast<double>(receiverPixelCount)
        : 0.0;
    const double meanReceiverRedLift = receiverPixelCount > 0
        ? receiverRedLiftSum / static_cast<double>(receiverPixelCount)
        : 0.0;
    std::vector<float> highFrequencyResiduals;
    if (passed && width > 2 && height > 2)
    {
        highFrequencyResiduals.reserve(receiverPixelCount / 4);
        for (UINT y = 1; y + 1 < height; ++y)
        {
            for (UINT x = 1; x + 1 < width; ++x)
            {
                const std::size_t pixel =
                    static_cast<std::size_t>(y) * width + x;
                const std::array<std::size_t, 5> neighbourhood = {
                    pixel,
                    pixel - 1,
                    pixel + 1,
                    pixel - width,
                    pixel + width};
                bool receiverNeighbourhood = true;
                std::array<float, 5> redLift = {};
                bool activeNeighbourhood = false;
                for (std::size_t index = 0; index < neighbourhood.size(); ++index)
                {
                    const std::size_t neighbour = neighbourhood[index];
                    if (scene.redSourceColor[neighbour] !=
                        scene.neutralSourceColor[neighbour])
                    {
                        receiverNeighbourhood = false;
                        break;
                    }
                    redLift[index] =
                        redResult[neighbour * 3] - neutralResult[neighbour * 3];
                    activeNeighbourhood = activeNeighbourhood ||
                        redLift[index] >= 0.001F;
                }
                if (!receiverNeighbourhood || !activeNeighbourhood)
                    continue;
                const float neighbourMean =
                    (redLift[1] + redLift[2] + redLift[3] + redLift[4]) *
                    0.25F;
                highFrequencyResiduals.push_back(
                    std::fabs(redLift[0] - neighbourMean));
            }
        }
    }
    std::sort(highFrequencyResiduals.begin(), highFrequencyResiduals.end());
    const std::size_t noiseP95Index = highFrequencyResiduals.empty()
        ? 0
        : (std::min)(
            highFrequencyResiduals.size() - 1,
            static_cast<std::size_t>(
                std::ceil(highFrequencyResiduals.size() * 0.95)) - 1);
    const float highFrequencyResidualP95 = highFrequencyResiduals.empty()
        ? 1.0F
        : highFrequencyResiduals[noiseP95Index];
    const bool visibleColorBleed = passed && maximumRedLift >= 0.002F &&
        maximumRedLift >= greenLiftAtMaximum * 3.0F &&
        maximumRedLift >= blueLiftAtMaximum * 3.0F &&
        receiverCoverage >= 0.05 &&
        highFrequencyResidualP95 <= 0.01F;

    ssgi.Shutdown();
    ReleaseInterface(neutralView);
    ReleaseInterface(neutralTexture);
    ReleaseInterface(redView);
    ReleaseInterface(redTexture);
    ReleaseInterface(depthView);
    ReleaseInterface(depthTexture);
    ReleaseInterface(context);
    ReleaseInterface(device);

    wprintf(
        visibleColorBleed
            ? L"[PASS] Spatial SSGI transferred red radiance broadly and smoothly across the synthetic receiver: maximum=%ux%u redLift=%.6f greenLift=%.6f blueLift=%.6f coverage=%.2f%% meanRedLift=%.6f highFrequencyP95=%.6f iterations=%u featureLevel=0x%X.\n"
            : L"[FAIL] Spatial SSGI broad/smooth color-bleed validation failed: maximum=%ux%u redLift=%.6f greenLift=%.6f blueLift=%.6f coverage=%.2f%% meanRedLift=%.6f highFrequencyP95=%.6f iterations=%u featureLevel=0x%X.\n",
        maximumX,
        maximumY,
        maximumRedLift,
        greenLiftAtMaximum,
        blueLiftAtMaximum,
        receiverCoverage * 100.0,
        meanReceiverRedLift,
        highFrequencyResidualP95,
        iterations,
        static_cast<unsigned int>(featureLevel));
    return visibleColorBleed ? 0 : 1;
}
