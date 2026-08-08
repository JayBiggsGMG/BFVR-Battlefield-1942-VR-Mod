#include "presenter/D3D11AmbientOcclusion.h"
#include "presenter/D3D11TextureScaler.h"

#include <d3d11.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
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
        wprintf(L"[AO] %ls\n", message);
}

float HalfToFloat(std::uint16_t value)
{
    const float sign = (value & 0x8000U) != 0 ? -1.0F : 1.0F;
    const unsigned int exponent = (value >> 10U) & 0x1FU;
    const unsigned int mantissa = value & 0x03FFU;
    if (exponent == 0)
        return sign * std::ldexp(static_cast<float>(mantissa), -24);
    if (exponent == 31)
        return sign;
    return sign * std::ldexp(
        1.0F + static_cast<float>(mantissa) / 1024.0F,
        static_cast<int>(exponent) - 15);
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
    if (end == text || *end != L'\0' ||
        parsed < minimum || parsed > maximum)
    {
        return false;
    }
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

template <typename Work>
bool MeasureGpuMilliseconds(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    Work&& work,
    double& milliseconds)
{
    D3D11_QUERY_DESC description = {};
    ID3D11Query* disjointQuery = nullptr;
    ID3D11Query* startQuery = nullptr;
    ID3D11Query* endQuery = nullptr;
    description.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
    HRESULT result = device->CreateQuery(&description, &disjointQuery);
    description.Query = D3D11_QUERY_TIMESTAMP;
    result = SUCCEEDED(result)
        ? device->CreateQuery(&description, &startQuery)
        : result;
    result = SUCCEEDED(result)
        ? device->CreateQuery(&description, &endQuery)
        : result;
    bool completed = false;
    if (SUCCEEDED(result) && disjointQuery != nullptr &&
        startQuery != nullptr && endQuery != nullptr)
    {
        context->Begin(disjointQuery);
        context->End(startQuery);
        completed = work();
        context->End(endQuery);
        context->End(disjointQuery);
        completed = completed && WaitForGpu(device, context);
    }
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint = {};
    UINT64 started = 0;
    UINT64 ended = 0;
    completed = completed &&
        context->GetData(
            disjointQuery,
            &disjoint,
            sizeof(disjoint),
            0) == S_OK &&
        !disjoint.Disjoint && disjoint.Frequency != 0 &&
        context->GetData(
            startQuery,
            &started,
            sizeof(started),
            0) == S_OK &&
        context->GetData(
            endQuery,
            &ended,
            sizeof(ended),
            0) == S_OK &&
        ended >= started;
    if (completed)
    {
        milliseconds = static_cast<double>(ended - started) * 1000.0 /
            static_cast<double>(disjoint.Frequency);
    }
    ReleaseInterface(endQuery);
    ReleaseInterface(startQuery);
    ReleaseInterface(disjointQuery);
    return completed;
}

struct TimingSummary
{
    double median = 0.0;
    double p95 = 0.0;
    double maximum = 0.0;
};

TimingSummary Summarize(std::vector<double> samples)
{
    if (samples.empty())
        return {};
    std::sort(samples.begin(), samples.end());
    const std::size_t p95Index = static_cast<std::size_t>(
        std::ceil(static_cast<double>(samples.size()) * 0.95)) - 1;
    return {
        samples[samples.size() / 2],
        samples[(std::min)(p95Index, samples.size() - 1)],
        samples.back()};
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
        {
            if (!ParseUnsigned(argv[++index], 64, 8192, width))
                return 2;
        }
        else if (wcscmp(argv[index], L"--height") == 0 && index + 1 < argc)
        {
            if (!ParseUnsigned(argv[++index], 64, 8192, height))
                return 2;
        }
        else if (wcscmp(argv[index], L"--iterations") == 0 &&
            index + 1 < argc)
        {
            if (!ParseUnsigned(argv[++index], 1, 512, iterations))
                return 2;
        }
        else
        {
            fwprintf(
                stderr,
                L"Usage: BFVRAmbientOcclusionGpuProbe "
                L"[--width <64..8192>] [--height <64..8192>] "
                L"[--iterations <1..512>]\n");
            return 2;
        }
    }

    constexpr float nearPlane = 0.1F;
    constexpr float farPlane = 100.0F;
    constexpr float verticalFov = 1.55F;
    const float yScale = 1.0F / std::tan(verticalFov * 0.5F);
    const float xScale = yScale * static_cast<float>(height) /
        static_cast<float>(width);
    const float depthScale = farPlane / (farPlane - nearPlane);
    const float depthOffset = -nearPlane * farPlane /
        (farPlane - nearPlane);
    const float projection[16] = {
        xScale, 0.0F, 0.0F, 0.0F,
        0.0F, yScale, 0.0F, 0.0F,
        0.0F, 0.0F, depthScale, 1.0F,
        0.0F, 0.0F, depthOffset, 0.0F};

    std::vector<std::uint32_t> packedDepth(
        static_cast<std::size_t>(width) * height);
    for (UINT y = 0; y < height; ++y)
    {
        for (UINT x = 0; x < width; ++x)
        {
            const bool raisedPanel =
                x < width / 2 &&
                y > height / 5 && y < height * 4 / 5;
            const float viewZ = raisedPanel ? 3.88F : 4.0F;
            const float deviceDepth = depthScale + depthOffset / viewZ;
            packedDepth[static_cast<std::size_t>(y) * width + x] =
                PackDeviceDepth(deviceDepth);
        }
    }

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

    D3D11_TEXTURE2D_DESC depthDescription = {};
    depthDescription.Width = width;
    depthDescription.Height = height;
    depthDescription.MipLevels = 1;
    depthDescription.ArraySize = 1;
    depthDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    depthDescription.SampleDesc.Count = 1;
    depthDescription.Usage = D3D11_USAGE_IMMUTABLE;
    depthDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA depthData = {};
    depthData.pSysMem = packedDepth.data();
    depthData.SysMemPitch = width * sizeof(std::uint32_t);
    ID3D11Texture2D* depthTexture = nullptr;
    ID3D11ShaderResourceView* depthView = nullptr;
    result = device->CreateTexture2D(
        &depthDescription,
        &depthData,
        &depthTexture);
    result = SUCCEEDED(result)
        ? device->CreateShaderResourceView(depthTexture, nullptr, &depthView)
        : result;

    bfvr::shared::D3D11AmbientOcclusion ao;
    bool passed = SUCCEEDED(result) && depthView != nullptr &&
        ao.Initialize(device, context, WriteLog, nullptr);
    for (UINT iteration = 0; iteration < iterations && passed; ++iteration)
    {
        passed = ao.BeginFrame();
        for (std::size_t eye = 0; eye < 2 && passed; ++eye)
        {
            passed = ao.BuildEye(
                eye,
                depthView,
                width,
                height,
                projection);
        }
        ao.EndFrame();
        passed = passed && WaitForGpu(device, context);
        ao.CollectFrameTimings();
    }

    std::fill(packedDepth.begin(), packedDepth.end(), 0xFF8090A0U);
    D3D11_SUBRESOURCE_DATA colorData = {};
    colorData.pSysMem = packedDepth.data();
    colorData.SysMemPitch = width * sizeof(std::uint32_t);
    ID3D11Texture2D* colorTexture = nullptr;
    ID3D11ShaderResourceView* colorView = nullptr;
    result = passed
        ? device->CreateTexture2D(
            &depthDescription,
            &colorData,
            &colorTexture)
        : E_FAIL;
    result = SUCCEEDED(result)
        ? device->CreateShaderResourceView(colorTexture, nullptr, &colorView)
        : result;
    D3D11_TEXTURE2D_DESC destinationDescription = depthDescription;
    destinationDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    destinationDescription.Usage = D3D11_USAGE_DEFAULT;
    destinationDescription.BindFlags =
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D* destinationTexture = nullptr;
    ID3D11RenderTargetView* destinationTarget = nullptr;
    result = SUCCEEDED(result)
        ? device->CreateTexture2D(
            &destinationDescription,
            nullptr,
            &destinationTexture)
        : result;
    result = SUCCEEDED(result)
        ? device->CreateRenderTargetView(
            destinationTexture,
            nullptr,
            &destinationTarget)
        : result;

    bfvr::shared::D3D11TextureScaler scaler;
    passed = passed && SUCCEEDED(result) && colorView != nullptr &&
        destinationTarget != nullptr &&
        scaler.Initialize(
            device, context, WriteLog, nullptr, false, true, false, false);
    auto drawStereoComposite = [&](bool applyAo)
    {
        bool drawn = true;
        for (std::size_t eye = 0; eye < 2 && drawn; ++eye)
        {
            ID3D11ShaderResourceView* const eyeAo =
                applyAo ? ao.GetEyeView(eye) : nullptr;
            drawn = scaler.ScaleAspectFit(
                colorView,
                width,
                height,
                destinationTarget,
                width,
                height,
                false,
                false,
                true,
                0.0F,
                eyeAo,
                eyeAo != nullptr ? 1.0F : 0.0F,
                nullptr,
                0.0F,
                0.0F,
                nullptr,
                0.0F,
                false,
                0.0F,
                0.0F);
        }
        return drawn;
    };
    if (passed)
    {
        passed = drawStereoComposite(false) &&
            drawStereoComposite(true) &&
            WaitForGpu(device, context);
    }
    std::vector<double> baselineCompositeMilliseconds;
    std::vector<double> aoCompositeMilliseconds;
    baselineCompositeMilliseconds.reserve(iterations);
    aoCompositeMilliseconds.reserve(iterations);
    for (UINT iteration = 0; iteration < iterations && passed; ++iteration)
    {
        double baseline = 0.0;
        double withAo = 0.0;
        auto measureBaseline = [&]()
        {
            return MeasureGpuMilliseconds(
                device,
                context,
                [&]() { return drawStereoComposite(false); },
                baseline);
        };
        auto measureWithAo = [&]()
        {
            return MeasureGpuMilliseconds(
                device,
                context,
                [&]() { return drawStereoComposite(true); },
                withAo);
        };
        passed = iteration % 2 == 0
            ? measureBaseline() && measureWithAo()
            : measureWithAo() && measureBaseline();
        if (passed)
        {
            baselineCompositeMilliseconds.push_back(baseline);
            aoCompositeMilliseconds.push_back(withAo);
        }
    }
    const TimingSummary baselineComposite =
        Summarize(baselineCompositeMilliseconds);
    const TimingSummary aoComposite = Summarize(aoCompositeMilliseconds);

    std::uint8_t minimumAo = 255;
    std::uint8_t maximumAo = 0;
    ID3D11ShaderResourceView* aoView = ao.GetEyeView(0);
    ID3D11Resource* aoResource = nullptr;
    if (passed && aoView != nullptr)
        aoView->GetResource(&aoResource);
    ID3D11Texture2D* aoTexture = nullptr;
    if (aoResource != nullptr)
    {
        result = aoResource->QueryInterface(
            __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(&aoTexture));
    }
    D3D11_TEXTURE2D_DESC aoDescription = {};
    if (SUCCEEDED(result) && aoTexture != nullptr)
        aoTexture->GetDesc(&aoDescription);
    aoDescription.Usage = D3D11_USAGE_STAGING;
    aoDescription.BindFlags = 0;
    aoDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    aoDescription.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    result = passed && aoTexture != nullptr
        ? device->CreateTexture2D(&aoDescription, nullptr, &staging)
        : E_FAIL;
    if (SUCCEEDED(result) && staging != nullptr)
    {
        context->CopyResource(staging, aoTexture);
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        result = context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
        if (SUCCEEDED(result) && mapped.pData != nullptr)
        {
            for (UINT y = 0; y < aoDescription.Height; ++y)
            {
                const auto* row = static_cast<const std::uint8_t*>(
                    mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch;
                for (UINT x = 0; x < aoDescription.Width; ++x)
                {
                    std::uint8_t aoByte = 0;
                    if (aoDescription.Format == DXGI_FORMAT_R8_UNORM)
                    {
                        aoByte = row[x];
                    }
                    else if (aoDescription.Format == DXGI_FORMAT_R16_FLOAT)
                    {
                        std::uint16_t encoded = 0;
                        std::memcpy(&encoded, row + x * sizeof(encoded), sizeof(encoded));
                        aoByte = ToUnorm8(HalfToFloat(encoded));
                    }
                    else
                    {
                        result = E_FAIL;
                        break;
                    }
                    minimumAo = (std::min)(minimumAo, aoByte);
                    maximumAo = (std::max)(maximumAo, aoByte);
                }
            }
            context->Unmap(staging, 0);
        }
    }
    passed = passed && SUCCEEDED(result) &&
        aoDescription.Width == width && aoDescription.Height == height &&
        minimumAo < 250 && maximumAo == 255;

    scaler.Shutdown();
    ao.Shutdown();
    ReleaseInterface(staging);
    ReleaseInterface(aoTexture);
    ReleaseInterface(aoResource);
    ReleaseInterface(depthView);
    ReleaseInterface(depthTexture);
    ReleaseInterface(destinationTarget);
    ReleaseInterface(destinationTexture);
    ReleaseInterface(colorView);
    ReleaseInterface(colorTexture);
    ReleaseInterface(context);
    ReleaseInterface(device);

    wprintf(
        L"[COMPOSITE] stereo baseline median=%.4f p95=%.4f max=%.4f ms; "
        L"with-AO median=%.4f p95=%.4f max=%.4f ms; p95 delta=%.4f ms.\n",
        baselineComposite.median,
        baselineComposite.p95,
        baselineComposite.maximum,
        aoComposite.median,
        aoComposite.p95,
        aoComposite.maximum,
        aoComposite.p95 - baselineComposite.p95);
    wprintf(
        passed
            ? L"[PASS] Spatial AO evaluated and denoised both eyes at %ux%u for %u iterations; output range=%u..%u, featureLevel=0x%X.\n"
            : L"[FAIL] Spatial AO GPU probe failed at %ux%u; output range=%u..%u, featureLevel=0x%X.\n",
        width,
        height,
        iterations,
        static_cast<unsigned int>(minimumAo),
        static_cast<unsigned int>(maximumAo),
        static_cast<unsigned int>(featureLevel));
    return passed ? 0 : 1;
}
