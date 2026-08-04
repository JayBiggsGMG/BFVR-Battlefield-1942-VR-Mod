#include "presenter/D3D11WaterReflection.h"

#include <d3d11.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
void WriteLog(void*, const wchar_t* message)
{
    std::fwprintf(stdout, L"%ls\n", message);
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

bool CreateTextureAndView(
    ID3D11Device* device,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    const void* pixels,
    UINT rowPitch,
    ID3D11Texture2D** texture,
    ID3D11ShaderResourceView** view)
{
    D3D11_TEXTURE2D_DESC description = {};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = format;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA data = {pixels, rowPitch, 0};
    HRESULT result = device->CreateTexture2D(
        &description,
        &data,
        texture);
    if (SUCCEEDED(result))
        result = device->CreateShaderResourceView(*texture, nullptr, view);
    return SUCCEEDED(result) && *texture != nullptr && *view != nullptr;
}

bool OutputIsClear(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    ID3D11ShaderResourceView* outputView,
    UINT width,
    UINT height,
    UINT* nonZeroAlpha = nullptr,
    UINT* rowsWithAlpha = nullptr,
    UINT* internalEmptyRows = nullptr)
{
    ID3D11Resource* outputResource = nullptr;
    outputView->GetResource(&outputResource);
    ID3D11Texture2D* outputTexture = nullptr;
    HRESULT result = outputResource == nullptr
        ? E_FAIL
        : outputResource->QueryInterface(
            __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(&outputTexture));
    ReleaseInterface(outputResource);
    D3D11_TEXTURE2D_DESC description = {};
    if (SUCCEEDED(result))
        outputTexture->GetDesc(&description);
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    description.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    if (SUCCEEDED(result))
        result = device->CreateTexture2D(&description, nullptr, &staging);
    if (SUCCEEDED(result))
    {
        context->CopyResource(staging, outputTexture);
        context->Flush();
    }
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(result))
        result = context->Map(
            staging,
            0,
            D3D11_MAP_READ,
            0,
            &mapped);
    const bool mappedSuccessfully = SUCCEEDED(result);
    bool clear = mappedSuccessfully;
    UINT alphaCount = 0;
    UINT populatedRows = 0;
    UINT firstPopulatedRow = height;
    UINT lastPopulatedRow = 0;
    std::vector<bool> rowPopulated(height, false);
    if (mappedSuccessfully)
    {
        for (UINT y = 0; y < height; ++y)
        {
            const auto* row = static_cast<const unsigned char*>(mapped.pData) +
                static_cast<std::size_t>(y) * mapped.RowPitch;
            for (UINT x = 0; x < width * 4; ++x)
            {
                if (row[x] != 0)
                    clear = false;
            }
            for (UINT x = 0; x < width; ++x)
            {
                if (row[x * 4 + 3] != 0)
                {
                    ++alphaCount;
                    rowPopulated[y] = true;
                }
            }
            if (rowPopulated[y])
            {
                ++populatedRows;
                firstPopulatedRow = (std::min)(firstPopulatedRow, y);
                lastPopulatedRow = (std::max)(lastPopulatedRow, y);
            }
        }
        context->Unmap(staging, 0);
    }
    ReleaseInterface(staging);
    ReleaseInterface(outputTexture);
    if (nonZeroAlpha != nullptr)
        *nonZeroAlpha = alphaCount;
    if (rowsWithAlpha != nullptr)
        *rowsWithAlpha = populatedRows;
    if (internalEmptyRows != nullptr)
    {
        UINT emptyRows = 0;
        if (populatedRows != 0)
        {
            for (UINT y = firstPopulatedRow; y <= lastPopulatedRow; ++y)
            {
                if (!rowPopulated[y])
                    ++emptyRows;
            }
        }
        *internalEmptyRows = emptyRows;
    }
    return clear;
}

DWORD PackDepthMask(float depth, unsigned char mask)
{
    depth = std::min(depth, 0.99999994F);
    const auto fractional = [](float value)
    {
        return value - std::floor(value);
    };
    float red = fractional(depth);
    float green = fractional(depth * 255.0F);
    const float blue = fractional(depth * 65025.0F);
    red -= green / 255.0F;
    green -= blue / 255.0F;
    const auto channel = [](float value)
    {
        return static_cast<unsigned char>(std::clamp(
            std::lround(value * 255.0F),
            0L,
            255L));
    };
    // B8G8R8A8 memory order in one little-endian DWORD.
    return static_cast<DWORD>(channel(blue)) |
        (static_cast<DWORD>(channel(green)) << 8) |
        (static_cast<DWORD>(channel(red)) << 16) |
        (static_cast<DWORD>(mask) << 24);
}
} // namespace

int wmain()
{
    constexpr UINT width = 64;
    constexpr UINT height = 64;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_9_1;
    HRESULT result = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &device,
        &featureLevel,
        &context);
    if (FAILED(result))
    {
        result = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            0,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            &device,
            &featureLevel,
            &context);
    }

    std::vector<DWORD> color(width * height, 0xFF806040);
    // DXGI_FORMAT_B8G8R8A8 memory layout: this is packed depth 0.5 in red
    // with a deliberately empty alpha water mask.
    std::vector<DWORD> depthMask(width * height, 0x00800000);
    ID3D11Texture2D* colorTexture = nullptr;
    ID3D11ShaderResourceView* colorView = nullptr;
    ID3D11Texture2D* depthTexture = nullptr;
    ID3D11ShaderResourceView* depthView = nullptr;
    bool passed = SUCCEEDED(result) &&
        CreateTextureAndView(
            device,
            width,
            height,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            color.data(),
            width * sizeof(DWORD),
            &colorTexture,
            &colorView) &&
        CreateTextureAndView(
            device,
            width,
            height,
            DXGI_FORMAT_B8G8R8A8_UNORM,
            depthMask.data(),
            width * sizeof(DWORD),
            &depthTexture,
            &depthView);

    const float projection[16] = {
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.001001F, 1.0F,
        0.0F, 0.0F, -0.1001001F, 0.0F};
    bfvr::shared::D3D11WaterReflection reflection;
    passed = passed && reflection.Initialize(
        device,
        context,
        WriteLog,
        nullptr);
    for (std::size_t eye = 0; eye < 2 && passed; ++eye)
    {
        passed = reflection.BuildEye(
            eye,
            colorView,
            depthView,
            width,
            height,
            projection);
        if (passed)
        {
            passed = OutputIsClear(
                device,
                context,
                reflection.GetEyeView(eye),
                width,
                height);
        }
    }

    // Positive control: bottom-half pixels describe a horizontal view-space
    // water plane (y=-1); the dry top half is a wall at z=5. Reflected rays
    // from the water should intersect the wall and produce nonzero confidence.
    if (passed)
    {
        for (UINT y = 0; y < height; ++y)
        {
            const float uvY = (static_cast<float>(y) + 0.5F) /
                static_cast<float>(height);
            const float ndcY = 1.0F - uvY * 2.0F;
            const bool water = y > height / 2 && ndcY < -0.02F;
            const float viewZ = water ? -1.0F / ndcY : 5.0F;
            const float deviceDepth =
                (viewZ * projection[10] + projection[14]) /
                (viewZ * projection[11] + projection[15]);
            const DWORD packed = PackDepthMask(
                deviceDepth,
                water ? 255 : 0);
            std::fill_n(
                depthMask.begin() + static_cast<std::size_t>(y) * width,
                width,
                packed);
        }
        context->UpdateSubresource(
            depthTexture,
            0,
            nullptr,
            depthMask.data(),
            width * sizeof(DWORD),
            0);
        passed = reflection.BuildEye(
            0,
            colorView,
            depthView,
            width,
            height,
            projection);
        UINT reflectedPixels = 0;
        UINT reflectedRows = 0;
        UINT internalEmptyRows = 0;
        const bool unexpectedlyClear = passed && OutputIsClear(
            device,
            context,
            reflection.GetEyeView(0),
            width,
            height,
            &reflectedPixels,
            &reflectedRows,
            &internalEmptyRows);
        const UINT candidatePixels = reflectedRows * width;
        const bool denseCoverage =
            reflectedRows >= 8 &&
            internalEmptyRows == 0 &&
            candidatePixels != 0 &&
            reflectedPixels * 4 >= candidatePixels * 3;
        passed = passed && !unexpectedlyClear && denseCoverage;
        std::fwprintf(
            stdout,
            L"BFVR water SSR positive control reflectedPixels=%u rows=%u internalEmptyRows=%u coverage=%.1f%%.\n",
            reflectedPixels,
            reflectedRows,
            internalEmptyRows,
            candidatePixels != 0
                ? static_cast<double>(reflectedPixels) * 100.0 /
                    static_cast<double>(candidatePixels)
                : 0.0);
    }

    reflection.Shutdown();
    ReleaseInterface(depthView);
    ReleaseInterface(depthTexture);
    ReleaseInterface(colorView);
    ReleaseInterface(colorTexture);
    ReleaseInterface(context);
    ReleaseInterface(device);
    std::fwprintf(
        stdout,
        passed
            ? L"BFVR water SSR GPU probe passed: shader compiled, an empty per-eye mask produced zero reflections, and refined depth crossings produced dense, stripe-free masked-plane coverage against the dry scene wall.\n"
            : L"BFVR water SSR GPU probe failed.\n");
    return passed ? 0 : 1;
}
