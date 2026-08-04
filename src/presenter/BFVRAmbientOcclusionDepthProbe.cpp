#include "presenter/D3DSystemRuntime.h"

#include "bfvr_shared_bridge.hpp"
#include "d3d8.hpp"

#include <d3d11.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>
#include <vector>

namespace
{
using Direct3DCreate8Fn = IDirect3D8*(WINAPI*)(UINT);

constexpr UINT kDefaultWidth = 1872;
constexpr UINT kDefaultHeight = 2016;
constexpr UINT kDefaultIterations = 32;
constexpr D3DFORMAT kSceneFormat = D3DFMT_A2B10G10R10;
constexpr D3DFORMAT kPackedFormat = D3DFMT_A8R8G8B8;
constexpr D3DFORMAT kFloatFormat = D3DFMT_A16B16G16R16F;

struct ColoredVertex
{
    float x;
    float y;
    float z;
    float rhw;
    DWORD color;
};

struct Float4
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
};

struct SamplePoint
{
    UINT x;
    UINT y;
};

template <typename T>
void ReleaseInterface(T*& interfacePointer)
{
    if (interfacePointer != nullptr)
    {
        interfacePointer->Release();
        interfacePointer = nullptr;
    }
}

int Fail(const wchar_t* operation, HRESULT result)
{
    fwprintf(
        stderr,
        L"[FAIL] %ls returned 0x%08lX.\n",
        operation,
        static_cast<unsigned long>(result));
    return 1;
}

std::wstring ModuleDirectory()
{
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(
        nullptr,
        path,
        static_cast<DWORD>(std::size(path)));
    if (length == 0 || length >= std::size(path))
        return {};

    std::wstring directory(path, length);
    const std::size_t separator = directory.find_last_of(L"\\/");
    return separator == std::wstring::npos
        ? std::wstring{}
        : directory.substr(0, separator);
}

std::wstring Combine(
    const std::wstring& directory,
    const wchar_t* fileName)
{
    return directory.empty()
        ? std::wstring(fileName)
        : directory + L"\\" + fileName;
}

LRESULT CALLBACK ProbeWindowProcedure(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    return DefWindowProcW(window, message, wParam, lParam);
}

HWND CreateProbeWindow(HINSTANCE instance)
{
    constexpr wchar_t kClassName[] =
        L"BFVRAmbientOcclusionDepthProbeWindow";
    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = ProbeWindowProcedure;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kClassName;
    if (RegisterClassW(&windowClass) == 0 &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        return nullptr;
    }

    return CreateWindowExW(
        0,
        kClassName,
        L"BFVR AO depth export probe",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        320,
        240,
        nullptr,
        nullptr,
        instance,
        nullptr);
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

HRESULT DrawQuad(
    IDirect3DDevice8* device,
    float left,
    float top,
    float right,
    float bottom,
    float depth,
    D3DCOLOR color)
{
    const ColoredVertex vertices[] = {
        {left - 0.5f, top - 0.5f, depth, 1.0f, color},
        {right - 0.5f, top - 0.5f, depth, 1.0f, color},
        {left - 0.5f, bottom - 0.5f, depth, 1.0f, color},
        {right - 0.5f, bottom - 0.5f, depth, 1.0f, color},
    };
    return device->DrawPrimitiveUP(
        D3DPT_TRIANGLESTRIP,
        2,
        vertices,
        sizeof(ColoredVertex));
}

HRESULT SetSceneState(IDirect3DDevice8* device)
{
    HRESULT result = device->SetVertexShader(
        D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    result = SUCCEEDED(result)
        ? device->SetPixelShader(0)
        : result;
    result = SUCCEEDED(result)
        ? device->SetTexture(0, nullptr)
        : result;

    const struct
    {
        D3DRENDERSTATETYPE state;
        DWORD value;
    } states[] = {
        {D3DRS_LIGHTING, FALSE},
        {D3DRS_CULLMODE, D3DCULL_NONE},
        {D3DRS_ALPHATESTENABLE, FALSE},
        {D3DRS_ALPHABLENDENABLE, FALSE},
        {D3DRS_ZENABLE, D3DZB_TRUE},
        {D3DRS_ZWRITEENABLE, TRUE},
        {D3DRS_ZFUNC, D3DCMP_LESSEQUAL},
        {D3DRS_STENCILENABLE, TRUE},
        {D3DRS_STENCILFUNC, D3DCMP_EQUAL},
        {D3DRS_STENCILREF, 0x5A},
        {D3DRS_STENCILMASK, 0xFF},
        {D3DRS_STENCILWRITEMASK, 0xFF},
        {D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP},
        {D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP},
        {D3DRS_STENCILPASS, D3DSTENCILOP_KEEP},
    };
    for (const auto& state : states)
    {
        if (SUCCEEDED(result))
            result = device->SetRenderState(state.state, state.value);
    }
    return result;
}

HRESULT RenderDepthPattern(
    IDirect3DDevice8* device,
    IDirect3DSurface8* color,
    IDirect3DSurface8* depth,
    UINT width,
    UINT height)
{
    D3DVIEWPORT8 viewport = {};
    viewport.Width = width;
    viewport.Height = height;
    viewport.MaxZ = 1.0f;
    HRESULT result = device->SetRenderTarget(color, depth);
    result = SUCCEEDED(result)
        ? device->SetViewport(&viewport)
        : result;
    result = SUCCEEDED(result)
        ? device->Clear(
            0,
            nullptr,
            D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,
            0xFF000000u,
            1.0f,
            0x5A)
        : result;
    result = SUCCEEDED(result)
        ? SetSceneState(device)
        : result;
    result = SUCCEEDED(result)
        ? device->BeginScene()
        : result;
    const bool sceneBegan = SUCCEEDED(result);
    const float coveredBottom = static_cast<float>(height) * 0.75f;
    result = SUCCEEDED(result)
        ? DrawQuad(
            device,
            0.0f,
            0.0f,
            static_cast<float>(width),
            coveredBottom,
            0.60f,
            0xFF00FF00u)
        : result;
    result = SUCCEEDED(result)
        ? DrawQuad(
            device,
            0.0f,
            0.0f,
            static_cast<float>(width) * 0.5f,
            coveredBottom,
            0.25f,
            0xFFFF0000u)
        : result;
    result = SUCCEEDED(result)
        ? DrawQuad(
            device,
            0.0f,
            0.0f,
            static_cast<float>(width),
            coveredBottom,
            0.50f,
            0xFF0000FFu)
        : result;

    // This final draw would overwrite the whole target if the INTZ stencil
    // component were ignored. The deliberately wrong reference must reject it.
    result = SUCCEEDED(result)
        ? device->SetRenderState(D3DRS_ZENABLE, FALSE)
        : result;
    result = SUCCEEDED(result)
        ? device->SetRenderState(D3DRS_STENCILREF, 0x5B)
        : result;
    result = SUCCEEDED(result)
        ? DrawQuad(
            device,
            0.0f,
            0.0f,
            static_cast<float>(width),
            static_cast<float>(height),
            0.0f,
            0xFFFFFF00u)
        : result;
    if (sceneBegan)
    {
        const HRESULT endResult = device->EndScene();
        if (SUCCEEDED(result) && FAILED(endResult))
            result = endResult;
    }
    return result;
}

bool VerifyLogicalDepthBias(IDirect3DDevice8* device8)
{
    IDirect3DDevice9* device9 = nullptr;
    HRESULT result = device8->QueryInterface(
        __uuidof(IDirect3DDevice9),
        reinterpret_cast<void**>(&device9));
    result = SUCCEEDED(result)
        ? device8->SetRenderState(
            static_cast<D3DRENDERSTATETYPE>(D3DRS_ZBIAS),
            1)
        : result;
    DWORD observedBits = 0;
    result = SUCCEEDED(result)
        ? device9->GetRenderState(D3DRS_DEPTHBIAS, &observedBits)
        : result;
    float observed = 0.0f;
    std::memcpy(&observed, &observedBits, sizeof(observed));
    const float expected = -1.0f / static_cast<float>((1 << 20) - 1);
    const bool matches = SUCCEEDED(result) &&
        std::fabs(observed - expected) <= 1.0e-12f;
    const HRESULT resetResult =
        device8->SetRenderState(
            static_cast<D3DRENDERSTATETYPE>(D3DRS_ZBIAS),
            0);
    ReleaseInterface(device9);
    if (!matches || FAILED(resetResult))
    {
        fwprintf(
            stderr,
            L"[FAIL] INTZ logical depth-bias mismatch: "
            L"observed=%.10g expected=%.10g hr=0x%08lX.\n",
            observed,
            expected,
            static_cast<unsigned long>(
                FAILED(result) ? result : resetResult));
        return false;
    }
    return true;
}

struct ResolverStateSnapshot
{
    IDirect3DSurface8* color = nullptr;
    IDirect3DSurface8* depth = nullptr;
    D3DVIEWPORT8 viewport = {};
    std::array<DWORD, 5> renderStates = {};
    std::array<DWORD, 2> textureStates = {};

    void Release()
    {
        ReleaseInterface(depth);
        ReleaseInterface(color);
    }
};

constexpr std::array<D3DRENDERSTATETYPE, 5> kSentinelRenderStates = {
    D3DRS_ALPHABLENDENABLE,
    D3DRS_SRCBLEND,
    D3DRS_DESTBLEND,
    D3DRS_CULLMODE,
    D3DRS_ZWRITEENABLE,
};

constexpr std::array<D3DTEXTURESTAGESTATETYPE, 2> kSentinelTextureStates = {
    static_cast<D3DTEXTURESTAGESTATETYPE>(D3DTSS_ADDRESSU),
    static_cast<D3DTEXTURESTAGESTATETYPE>(D3DTSS_MAGFILTER),
};

bool SetAndCaptureResolverSentinel(
    IDirect3DDevice8* device,
    ResolverStateSnapshot& snapshot)
{
    const D3DVIEWPORT8 sentinelViewport = {
        3,
        5,
        101,
        79,
        0.2f,
        0.9f};
    const std::array<DWORD, 5> sentinelRenderValues = {
        TRUE,
        D3DBLEND_DESTALPHA,
        D3DBLEND_INVDESTALPHA,
        D3DCULL_CW,
        FALSE,
    };
    const std::array<DWORD, 2> sentinelTextureValues = {
        D3DTADDRESS_MIRROR,
        D3DTEXF_LINEAR,
    };

    HRESULT result = device->SetViewport(&sentinelViewport);
    for (std::size_t index = 0;
        index < kSentinelRenderStates.size() && SUCCEEDED(result);
        ++index)
    {
        result = device->SetRenderState(
            kSentinelRenderStates[index],
            sentinelRenderValues[index]);
    }
    for (std::size_t index = 0;
        index < kSentinelTextureStates.size() && SUCCEEDED(result);
        ++index)
    {
        result = device->SetTextureStageState(
            0,
            kSentinelTextureStates[index],
            sentinelTextureValues[index]);
    }
    result = SUCCEEDED(result)
        ? device->GetRenderTarget(&snapshot.color)
        : result;
    if (SUCCEEDED(result))
    {
        const HRESULT depthResult =
            device->GetDepthStencilSurface(&snapshot.depth);
        if (FAILED(depthResult) && snapshot.depth != nullptr)
            result = depthResult;
    }
    result = SUCCEEDED(result)
        ? device->GetViewport(&snapshot.viewport)
        : result;
    for (std::size_t index = 0;
        index < kSentinelRenderStates.size() && SUCCEEDED(result);
        ++index)
    {
        result = device->GetRenderState(
            kSentinelRenderStates[index],
            &snapshot.renderStates[index]);
    }
    for (std::size_t index = 0;
        index < kSentinelTextureStates.size() && SUCCEEDED(result);
        ++index)
    {
        result = device->GetTextureStageState(
            0,
            kSentinelTextureStates[index],
            &snapshot.textureStates[index]);
    }
    if (FAILED(result))
    {
        snapshot.Release();
        Fail(L"capture resolver sentinel state", result);
        return false;
    }
    return true;
}

bool ResolverStateMatches(
    IDirect3DDevice8* device,
    const ResolverStateSnapshot& expected)
{
    IDirect3DSurface8* color = nullptr;
    IDirect3DSurface8* depth = nullptr;
    D3DVIEWPORT8 viewport = {};
    std::array<DWORD, 5> renderStates = {};
    std::array<DWORD, 2> textureStates = {};
    HRESULT result = device->GetRenderTarget(&color);
    if (SUCCEEDED(result))
    {
        const HRESULT depthResult = device->GetDepthStencilSurface(&depth);
        if (FAILED(depthResult) && depth != nullptr)
            result = depthResult;
    }
    result = SUCCEEDED(result)
        ? device->GetViewport(&viewport)
        : result;
    for (std::size_t index = 0;
        index < kSentinelRenderStates.size() && SUCCEEDED(result);
        ++index)
    {
        result = device->GetRenderState(
            kSentinelRenderStates[index],
            &renderStates[index]);
    }
    for (std::size_t index = 0;
        index < kSentinelTextureStates.size() && SUCCEEDED(result);
        ++index)
    {
        result = device->GetTextureStageState(
            0,
            kSentinelTextureStates[index],
            &textureStates[index]);
    }

    const bool matches = SUCCEEDED(result) &&
        color == expected.color &&
        depth == expected.depth &&
        std::memcmp(&viewport, &expected.viewport, sizeof(viewport)) == 0 &&
        renderStates == expected.renderStates &&
        textureStates == expected.textureStates;
    ReleaseInterface(depth);
    ReleaseInterface(color);
    if (!matches)
    {
        fwprintf(
            stderr,
            L"[FAIL] Depth resolve did not restore the exact D3D8-visible state "
            L"(hr=0x%08lX).\n",
            static_cast<unsigned long>(result));
    }
    return matches;
}

float HalfToFloat(WORD half)
{
    const float sign = (half & 0x8000u) != 0 ? -1.0f : 1.0f;
    const unsigned int exponent = (half >> 10) & 0x1Fu;
    const unsigned int mantissa = half & 0x03FFu;
    if (exponent == 0)
        return sign * std::ldexp(static_cast<float>(mantissa), -24);
    if (exponent == 31)
        return mantissa == 0
            ? sign * INFINITY
            : NAN;
    return sign * std::ldexp(
        static_cast<float>(1024u + mantissa),
        static_cast<int>(exponent) - 25);
}

bool ReadSharedSamples(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    HANDLE sharedHandle,
    DXGI_FORMAT expectedFormat,
    UINT expectedWidth,
    UINT expectedHeight,
    const std::array<SamplePoint, 3>& points,
    std::array<Float4, 3>& samples)
{
    ID3D11Texture2D* sharedTexture = nullptr;
    ID3D11Texture2D* stagingTexture = nullptr;
    HRESULT result = device->OpenSharedResource(
        sharedHandle,
        __uuidof(ID3D11Texture2D),
        reinterpret_cast<void**>(&sharedTexture));
    D3D11_TEXTURE2D_DESC description = {};
    if (SUCCEEDED(result) && sharedTexture != nullptr)
        sharedTexture->GetDesc(&description);
    if (FAILED(result) || sharedTexture == nullptr ||
        description.Width != expectedWidth ||
        description.Height != expectedHeight ||
        description.Format != expectedFormat ||
        description.SampleDesc.Count != 1)
    {
        fwprintf(
            stderr,
            L"[FAIL] Unexpected shared texture: hr=0x%08lX size=%ux%u "
            L"format=%u samples=%u.\n",
            static_cast<unsigned long>(result),
            description.Width,
            description.Height,
            static_cast<unsigned int>(description.Format),
            description.SampleDesc.Count);
        ReleaseInterface(sharedTexture);
        return false;
    }

    D3D11_TEXTURE2D_DESC stagingDescription = description;
    stagingDescription.Usage = D3D11_USAGE_STAGING;
    stagingDescription.BindFlags = 0;
    stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDescription.MiscFlags = 0;
    result = device->CreateTexture2D(
        &stagingDescription,
        nullptr,
        &stagingTexture);
    if (SUCCEEDED(result))
    {
        context->CopyResource(stagingTexture, sharedTexture);
        context->Flush();
    }
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    result = SUCCEEDED(result)
        ? context->Map(
            stagingTexture,
            0,
            D3D11_MAP_READ,
            0,
            &mapped)
        : result;
    if (FAILED(result))
    {
        Fail(L"D3D11 shared texture readback", result);
        ReleaseInterface(stagingTexture);
        ReleaseInterface(sharedTexture);
        return false;
    }

    for (std::size_t index = 0; index < points.size(); ++index)
    {
        const BYTE* const row =
            static_cast<const BYTE*>(mapped.pData) +
            static_cast<std::size_t>(points[index].y) * mapped.RowPitch;
        Float4 sample = {};
        if (expectedFormat == DXGI_FORMAT_R10G10B10A2_UNORM)
        {
            DWORD packed = 0;
            std::memcpy(
                &packed,
                row + static_cast<std::size_t>(points[index].x) * 4,
                sizeof(packed));
            sample.r = static_cast<float>(packed & 0x3FFu) / 1023.0f;
            sample.g = static_cast<float>((packed >> 10) & 0x3FFu) / 1023.0f;
            sample.b = static_cast<float>((packed >> 20) & 0x3FFu) / 1023.0f;
            sample.a = static_cast<float>((packed >> 30) & 0x3u) / 3.0f;
        }
        else if (expectedFormat == DXGI_FORMAT_B8G8R8A8_UNORM)
        {
            const BYTE* const pixel =
                row + static_cast<std::size_t>(points[index].x) * 4;
            sample.r = static_cast<float>(pixel[2]) / 255.0f;
            sample.g = static_cast<float>(pixel[1]) / 255.0f;
            sample.b = static_cast<float>(pixel[0]) / 255.0f;
            sample.a = static_cast<float>(pixel[3]) / 255.0f;
        }
        else if (expectedFormat == DXGI_FORMAT_R16G16B16A16_FLOAT)
        {
            const BYTE* const pixel =
                row + static_cast<std::size_t>(points[index].x) * 8;
            WORD channels[4] = {};
            std::memcpy(channels, pixel, sizeof(channels));
            sample = {
                HalfToFloat(channels[0]),
                HalfToFloat(channels[1]),
                HalfToFloat(channels[2]),
                HalfToFloat(channels[3])};
        }
        else
        {
            context->Unmap(stagingTexture, 0);
            ReleaseInterface(stagingTexture);
            ReleaseInterface(sharedTexture);
            return false;
        }
        samples[index] = sample;
    }

    context->Unmap(stagingTexture, 0);
    ReleaseInterface(stagingTexture);
    ReleaseInterface(sharedTexture);
    return true;
}

bool Approximately(float value, float expected, float tolerance)
{
    return std::isfinite(value) &&
        std::fabs(value - expected) <= tolerance;
}

bool VerifySceneSamples(const std::array<Float4, 3>& samples)
{
    constexpr float tolerance = 2.0f / 255.0f;
    const std::array<Float4, 3> expected = {
        Float4{1.0f, 0.0f, 0.0f, 1.0f},
        Float4{0.0f, 0.0f, 1.0f, 1.0f},
        Float4{0.0f, 0.0f, 0.0f, 1.0f},
    };
    for (std::size_t index = 0; index < samples.size(); ++index)
    {
        if (!Approximately(samples[index].r, expected[index].r, tolerance) ||
            !Approximately(samples[index].g, expected[index].g, tolerance) ||
            !Approximately(samples[index].b, expected[index].b, tolerance) ||
            !Approximately(samples[index].a, expected[index].a, tolerance))
        {
            fwprintf(
                stderr,
                L"[FAIL] INTZ depth/stencil scene sample %zu was "
                L"(%.5f,%.5f,%.5f,%.5f).\n",
                index,
                samples[index].r,
                samples[index].g,
                samples[index].b,
                samples[index].a);
            return false;
        }
    }
    return true;
}

float DecodePackedDepth(const Float4& sample)
{
    return sample.r +
        sample.g / 255.0f +
        sample.b / 65025.0f;
}

bool VerifyDepthSamples(
    const wchar_t* label,
    const std::array<Float4, 3>& samples,
    bool packed)
{
    constexpr std::array<float, 3> expected = {0.25f, 0.50f, 1.0f};
    constexpr float tolerance = 0.0025f;
    for (std::size_t index = 0; index < samples.size(); ++index)
    {
        const float observed = packed
            ? DecodePackedDepth(samples[index])
            : samples[index].r;
        if (!Approximately(observed, expected[index], tolerance))
        {
            fwprintf(
                stderr,
                L"[FAIL] %ls depth sample %zu was %.8f; expected %.8f.\n",
                label,
                index,
                observed,
                expected[index]);
            return false;
        }
    }
    return true;
}

bool VerifyPackedAlphaPreserved(const std::array<Float4, 3>& samples)
{
    constexpr float expected = 90.0f / 255.0f;
    constexpr float tolerance = 1.0f / 255.0f;
    for (std::size_t index = 0; index < samples.size(); ++index)
    {
        if (!Approximately(samples[index].a, expected, tolerance))
        {
            fwprintf(
                stderr,
                L"[FAIL] Packed-depth water-mask alpha sample %zu was %.5f; expected %.5f.\n",
                index,
                samples[index].a,
                expected);
            return false;
        }
    }
    return true;
}

bool ResolveRepeatedly(
    IDirect3DDevice8* device,
    BFVRD3D8To9ResolveDepthToSharedTargetFn resolveDepth,
    IDirect3DSurface8* depth,
    IDirect3DSurface8* target,
    BFVRD3D8To9DepthExportEncoding encoding,
    UINT iterations,
    const ResolverStateSnapshot& expectedState,
    std::vector<double>& gpuMilliseconds)
{
    for (UINT iteration = 0; iteration < iterations; ++iteration)
    {
        BFVRD3D8To9DepthExportTiming timing = {};
        timing.size = sizeof(timing);
        const HRESULT result = resolveDepth(
            device,
            depth,
            target,
            static_cast<DWORD>(encoding),
            &timing);
        if (FAILED(result))
        {
            Fail(L"BFVRD3D8To9ResolveDepthToSharedTarget", result);
            return false;
        }
        if (timing.version != BFVR_D3D8TO9_DEPTH_EXPORT_TIMING_VERSION ||
            !timing.gpuTimestampsValid ||
            timing.gpuTimestampDisjoint ||
            !std::isfinite(timing.elapsedMilliseconds))
        {
            fwprintf(
                stderr,
                L"[FAIL] Depth-export GPU timestamp %u is invalid.\n",
                iteration);
            return false;
        }
        if (!ResolverStateMatches(device, expectedState))
            return false;
        gpuMilliseconds.push_back(timing.elapsedMilliseconds);
    }
    return true;
}

struct TimingSummary
{
    double minimum = 0.0;
    double median = 0.0;
    double p95 = 0.0;
    double maximum = 0.0;
};

TimingSummary Summarize(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    const std::size_t p95Index = static_cast<std::size_t>(
        std::ceil(static_cast<double>(values.size()) * 0.95)) - 1;
    TimingSummary summary = {};
    summary.minimum = values.front();
    summary.median = values[values.size() / 2];
    summary.p95 = values[std::min(p95Index, values.size() - 1)];
    summary.maximum = values.back();
    return summary;
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    UINT width = kDefaultWidth;
    UINT height = kDefaultHeight;
    UINT iterations = kDefaultIterations;
    for (int index = 1; index < argc; ++index)
    {
        if (wcscmp(argv[index], L"--width") == 0 && index + 1 < argc)
        {
            if (!ParseUnsigned(argv[++index], 64, 8192, width))
                return 2;
        }
        else if (wcscmp(argv[index], L"--height") == 0 &&
            index + 1 < argc)
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
                L"Usage: BFVRAmbientOcclusionDepthProbe "
                L"[--width <64..8192>] [--height <64..8192>] "
                L"[--iterations <1..512>]\n");
            return 2;
        }
    }

    const std::wstring translatorPath =
        Combine(ModuleDirectory(), L"BFVRD3D8To9.dll");
    HMODULE translator = LoadLibraryExW(
        translatorPath.c_str(),
        nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
            LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (translator == nullptr)
    {
        fwprintf(
            stderr,
            L"[FAIL] Unable to load %ls (error=%lu).\n",
            translatorPath.c_str(),
            GetLastError());
        return 1;
    }

    const auto createDirect3D = reinterpret_cast<Direct3DCreate8Fn>(
        GetProcAddress(translator, "Direct3DCreate8"));
    const auto getBridgeVersion =
        reinterpret_cast<BFVRD3D8To9GetSharedBridgeVersionFn>(
            GetProcAddress(
                translator,
                "BFVRD3D8To9GetSharedBridgeVersion"));
    const auto createSharedTarget =
        reinterpret_cast<BFVRD3D8To9CreateSharedRenderTargetFn>(
            GetProcAddress(
                translator,
                "BFVRD3D8To9CreateSharedRenderTarget"));
    const auto createDepth =
        reinterpret_cast<BFVRD3D8To9CreateTextureBackedDepthStencilFn>(
            GetProcAddress(
                translator,
                "BFVRD3D8To9CreateTextureBackedDepthStencil"));
    const auto resolveDepth =
        reinterpret_cast<BFVRD3D8To9ResolveDepthToSharedTargetFn>(
            GetProcAddress(
                translator,
                "BFVRD3D8To9ResolveDepthToSharedTarget"));
    const auto waitForGpu =
        reinterpret_cast<BFVRD3D8To9WaitForGpuFn>(
            GetProcAddress(translator, "BFVRD3D8To9WaitForGpu"));
    if (createDirect3D == nullptr || getBridgeVersion == nullptr ||
        createSharedTarget == nullptr || createDepth == nullptr ||
        resolveDepth == nullptr || waitForGpu == nullptr ||
        getBridgeVersion() != BFVR_D3D8TO9_SHARED_BRIDGE_VERSION)
    {
        fwprintf(stderr, L"[FAIL] Translator depth-export ABI is unavailable.\n");
        FreeLibrary(translator);
        return 1;
    }

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    HWND window = CreateProbeWindow(instance);
    IDirect3D8* direct3D = nullptr;
    IDirect3DDevice8* device8 = nullptr;
    IDirect3DSurface8* sceneSurface = nullptr;
    IDirect3DSurface8* packedSurface = nullptr;
    IDirect3DSurface8* floatSurface = nullptr;
    IDirect3DSurface8* depthSurface = nullptr;
    IDirect3DSurface8* priorColor = nullptr;
    IDirect3DSurface8* priorDepth = nullptr;
    ID3D11Device* device11 = nullptr;
    ID3D11DeviceContext* context11 = nullptr;
    HANDLE sceneHandle = nullptr;
    HANDLE packedHandle = nullptr;
    HANDLE floatHandle = nullptr;
    D3DVIEWPORT8 priorViewport = {};
    ResolverStateSnapshot resolverState = {};
    D3DADAPTER_IDENTIFIER8 adapter = {};
    D3DPRESENT_PARAMETERS8 presentation = {};
    D3DSURFACE_DESC8 depthDescription = {};
    HRESULT result = E_FAIL;
    HRESULT viewportRestoreResult = E_FAIL;
    std::vector<double> packedTimes;
    std::vector<double> floatTimes;
    const bfvr::D3DSystemRuntime* runtime = nullptr;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_9_1;
    std::array<SamplePoint, 3> points = {
        SamplePoint{width / 4, height / 2},
        SamplePoint{(width * 3) / 4, height / 2},
        SamplePoint{width / 2, (height * 7) / 8},
    };
    std::array<Float4, 3> sceneSamples = {};
    std::array<Float4, 3> packedSamples = {};
    std::array<Float4, 3> floatSamples = {};
    TimingSummary packedSummary = {};
    TimingSummary floatSummary = {};
    double packedStorageMiB = 0.0;
    double floatStorageMiB = 0.0;
    int exitCode = 1;

    if (window == nullptr)
    {
        fwprintf(stderr, L"[FAIL] Probe window creation failed.\n");
        goto cleanup;
    }
    direct3D = createDirect3D(D3D_SDK_VERSION);
    if (direct3D == nullptr)
    {
        fwprintf(stderr, L"[FAIL] Direct3DCreate8 returned null.\n");
        goto cleanup;
    }

    direct3D->GetAdapterIdentifier(
        D3DADAPTER_DEFAULT,
        D3DENUM_NO_WHQL_LEVEL,
        &adapter);
    presentation.BackBufferWidth = 320;
    presentation.BackBufferHeight = 240;
    presentation.BackBufferFormat = D3DFMT_UNKNOWN;
    presentation.BackBufferCount = 1;
    presentation.SwapEffect = D3DSWAPEFFECT_DISCARD;
    presentation.hDeviceWindow = window;
    presentation.Windowed = TRUE;
    result = direct3D->CreateDevice(
        D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        window,
        D3DCREATE_HARDWARE_VERTEXPROCESSING |
            D3DCREATE_FPU_PRESERVE,
        &presentation,
        &device8);
    if (FAILED(result) || device8 == nullptr)
    {
        Fail(L"IDirect3D8::CreateDevice", result);
        goto cleanup;
    }

    result = device8->GetRenderTarget(&priorColor);
    if (SUCCEEDED(result))
    {
        const HRESULT depthResult =
            device8->GetDepthStencilSurface(&priorDepth);
        if (FAILED(depthResult) && priorDepth != nullptr)
            result = depthResult;
    }
    result = SUCCEEDED(result)
        ? device8->GetViewport(&priorViewport)
        : result;
    if (FAILED(result) || priorColor == nullptr)
    {
        Fail(L"capture original D3D8 targets", result);
        goto cleanup;
    }

    result = createSharedTarget(
        device8,
        width,
        height,
        kSceneFormat,
        &sceneHandle,
        reinterpret_cast<void**>(&sceneSurface));
    if (FAILED(result) || sceneSurface == nullptr || sceneHandle == nullptr)
    {
        Fail(L"create shared A2B10G10R10 scene target", result);
        goto cleanup;
    }
    result = createSharedTarget(
        device8,
        width,
        height,
        kPackedFormat,
        &packedHandle,
        reinterpret_cast<void**>(&packedSurface));
    if (FAILED(result) || packedSurface == nullptr || packedHandle == nullptr)
    {
        Fail(L"create shared A8R8G8B8 packed target", result);
        goto cleanup;
    }
    result = createSharedTarget(
        device8,
        width,
        height,
        kFloatFormat,
        &floatHandle,
        reinterpret_cast<void**>(&floatSurface));
    if (FAILED(result) || floatSurface == nullptr || floatHandle == nullptr)
    {
        Fail(L"create shared A16B16G16R16F target", result);
        goto cleanup;
    }
    result = createDepth(
        device8,
        width,
        height,
        kSceneFormat,
        reinterpret_cast<void**>(&depthSurface));
    if (FAILED(result) || depthSurface == nullptr)
    {
        Fail(L"create texture-backed INTZ depth/stencil", result);
        goto cleanup;
    }

    result = depthSurface->GetDesc(&depthDescription);
    if (FAILED(result) ||
        depthDescription.Width != width ||
        depthDescription.Height != height ||
        static_cast<DWORD>(depthDescription.Format) != BFVR_D3DFMT_INTZ)
    {
        Fail(L"validate INTZ depth description", result);
        goto cleanup;
    }

    // Live water masking binds this A8R8G8B8 export beside the same INTZ
    // surface used by the R10 world eye. Prove the adapter accepts that exact
    // target/depth pairing before relying on depth-tested alpha-only replay.
    result = device8->SetRenderTarget(packedSurface, depthSurface);
    result = SUCCEEDED(result)
        ? device8->SetRenderTarget(priorColor, priorDepth)
        : result;
    viewportRestoreResult = SUCCEEDED(result)
        ? device8->SetViewport(&priorViewport)
        : result;
    if (FAILED(result) || FAILED(viewportRestoreResult))
    {
        Fail(
            L"bind packed water-mask target with INTZ depth",
            FAILED(result) ? result : viewportRestoreResult);
        goto cleanup;
    }

    result = RenderDepthPattern(
        device8,
        sceneSurface,
        depthSurface,
        width,
        height);
    if (FAILED(result) || !VerifyLogicalDepthBias(device8))
    {
        if (FAILED(result))
            Fail(L"render INTZ depth/stencil pattern", result);
        goto cleanup;
    }
    result = device8->SetRenderTarget(priorColor, priorDepth);
    viewportRestoreResult = device8->SetViewport(&priorViewport);
    if (FAILED(result) || FAILED(viewportRestoreResult))
    {
        Fail(
            L"restore original target after INTZ rendering",
            FAILED(result) ? result : viewportRestoreResult);
        goto cleanup;
    }
    // The packed export owns RGB depth only. Seed alpha with a visible mask
    // sentinel and prove every repeated resolve preserves it byte-exactly.
    result = device8->SetRenderTarget(packedSurface, nullptr);
    result = SUCCEEDED(result)
        ? device8->Clear(0, nullptr, D3DCLEAR_TARGET, 0x5A000000, 1.0F, 0)
        : result;
    result = SUCCEEDED(result)
        ? device8->SetRenderTarget(priorColor, priorDepth)
        : result;
    viewportRestoreResult = SUCCEEDED(result)
        ? device8->SetViewport(&priorViewport)
        : result;
    if (FAILED(result) || FAILED(viewportRestoreResult))
    {
        Fail(
            L"seed and restore packed-depth alpha sentinel",
            FAILED(result) ? result : viewportRestoreResult);
        goto cleanup;
    }
    if (!SetAndCaptureResolverSentinel(device8, resolverState))
        goto cleanup;

    packedTimes.reserve(iterations);
    floatTimes.reserve(iterations);
    if (!ResolveRepeatedly(
            device8,
            resolveDepth,
            depthSurface,
            packedSurface,
            BFVRD3D8To9DepthExportEncoding::PackedRgba8,
            iterations,
            resolverState,
            packedTimes) ||
        !ResolveRepeatedly(
            device8,
            resolveDepth,
            depthSurface,
            floatSurface,
            BFVRD3D8To9DepthExportEncoding::FloatRgba16,
            iterations,
            resolverState,
            floatTimes))
    {
        goto cleanup;
    }
    result = waitForGpu(device8, 5000);
    if (FAILED(result))
    {
        Fail(L"BFVRD3D8To9WaitForGpu", result);
        goto cleanup;
    }

    runtime = &bfvr::GetD3DSystemRuntime();
    if (!runtime->IsAvailable())
    {
        fwprintf(
            stderr,
            L"[FAIL] System D3D11 runtime resolution failed (%lu).\n",
            runtime->error);
        goto cleanup;
    }
    result = runtime->createD3D11Device(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &device11,
        &featureLevel,
        &context11);
    if (FAILED(result))
    {
        Fail(L"D3D11CreateDevice", result);
        goto cleanup;
    }

    if (!ReadSharedSamples(
            device11,
            context11,
            sceneHandle,
            DXGI_FORMAT_R10G10B10A2_UNORM,
            width,
            height,
            points,
            sceneSamples) ||
        !ReadSharedSamples(
            device11,
            context11,
            packedHandle,
            DXGI_FORMAT_B8G8R8A8_UNORM,
            width,
            height,
            points,
            packedSamples) ||
        !ReadSharedSamples(
            device11,
            context11,
            floatHandle,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            width,
            height,
            points,
            floatSamples) ||
        !VerifySceneSamples(sceneSamples) ||
        !VerifyDepthSamples(L"RGBA8 packed", packedSamples, true) ||
        !VerifyPackedAlphaPreserved(packedSamples) ||
        !VerifyDepthSamples(L"RGBA16F", floatSamples, false))
    {
        goto cleanup;
    }

    packedSummary = Summarize(packedTimes);
    floatSummary = Summarize(floatTimes);
    packedStorageMiB =
        static_cast<double>(width) * height * 4.0 * 2.0 /
        (1024.0 * 1024.0);
    floatStorageMiB = packedStorageMiB * 2.0;
    wprintf(
        L"[PASS] INTZ depth/stencil, logical 24-bit bias, sampling, "
        L"water-mask target compatibility/alpha preservation, D3D8 state "
        L"restoration, and D3D11 reconstruction verified.\n"
        L"       adapter=%hs vendor=0x%04X device=0x%04X "
        L"featureLevel=0x%04X size=%ux%u iterations=%u\n"
        L"       RGBA8 packed: min=%.4f median=%.4f p95=%.4f max=%.4f ms/eye; "
        L"stereo-p95=%.4f ms; stereo-storage=%.2f MiB\n"
        L"       RGBA16F:      min=%.4f median=%.4f p95=%.4f max=%.4f ms/eye; "
        L"stereo-p95=%.4f ms; stereo-storage=%.2f MiB\n",
        adapter.Description,
        adapter.VendorId,
        adapter.DeviceId,
        static_cast<unsigned int>(featureLevel),
        width,
        height,
        iterations,
        packedSummary.minimum,
        packedSummary.median,
        packedSummary.p95,
        packedSummary.maximum,
        packedSummary.p95 * 2.0,
        packedStorageMiB,
        floatSummary.minimum,
        floatSummary.median,
        floatSummary.p95,
        floatSummary.maximum,
        floatSummary.p95 * 2.0,
        floatStorageMiB);
    exitCode = 0;

cleanup:
    resolverState.Release();
    ReleaseInterface(context11);
    ReleaseInterface(device11);
    ReleaseInterface(priorDepth);
    ReleaseInterface(priorColor);
    ReleaseInterface(depthSurface);
    ReleaseInterface(floatSurface);
    ReleaseInterface(packedSurface);
    ReleaseInterface(sceneSurface);
    ReleaseInterface(device8);
    ReleaseInterface(direct3D);
    if (window != nullptr)
        DestroyWindow(window);
    FreeLibrary(translator);
    return exitCode;
}
