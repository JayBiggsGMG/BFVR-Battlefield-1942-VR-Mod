#include "presenter/D3D11TextureScaler.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <cstdio>
#include <iterator>

namespace
{
constexpr char kFullscreenVertexShader[] = R"(
struct VertexOutput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

VertexOutput main(uint vertexId : SV_VertexID)
{
    VertexOutput output;
    const float2 position = vertexId == 0
        ? float2(-1.0, -1.0)
        : vertexId == 1
        ? float2(-1.0, 3.0)
        : float2(3.0, -1.0);
    output.position = float4(position, 0.0, 1.0);
    output.texcoord = float2(
        (position.x + 1.0) * 0.5,
        (1.0 - position.y) * 0.5);
    return output;
}
)";

constexpr char kTexturePixelShader[] = R"(
Texture2D sourceTexture : register(t0);
#if BFVR_AMBIENT_OCCLUSION
Texture2D ambientOcclusionTexture : register(t1);
#endif
#if BFVR_BLOOM
Texture2D bloomTexture : register(t2);
#endif
#if BFVR_WATER_REFLECTIONS
Texture2D waterReflectionTexture : register(t3);
#endif
#if BFVR_SSGI
Texture2D screenSpaceGlobalIlluminationTexture : register(t4);
#endif
SamplerState sourceSampler : register(s0);
cbuffer Configuration : register(b0)
{
    float sourceAlreadyLinear;
    float bloomThreshold;
    float bloomIntensity;
    float ambientOcclusionIntensity;
    float2 bloomTexelSize;
    float waterReflectionIntensity;
    float screenSpaceGlobalIlluminationIntensity;
    float screenSpaceGlobalIlluminationDebugMode;
    float3 configurationPadding2;
};

float3 SrgbToLinear(float3 color)
{
    const float3 low = color / 12.92;
    const float3 high = pow((color + 0.055) / 1.055, 2.4);
    return lerp(high, low, step(color, 0.04045));
}

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target
{
    const float4 encoded = sourceTexture.Sample(sourceSampler, texcoord);
    float3 linearColor = sourceAlreadyLinear > 0.5
        ? encoded.rgb
        : SrgbToLinear(encoded.rgb);
#if BFVR_AMBIENT_OCCLUSION
    const float ao = ambientOcclusionTexture.SampleLevel(
        sourceSampler, texcoord, 0.0).r;
    linearColor *= lerp(1.0, ao, ambientOcclusionIntensity);
#endif
#if BFVR_SSGI
    const float4 ssgi = screenSpaceGlobalIlluminationTexture.SampleLevel(
        sourceSampler, texcoord, 0.0);
    if (screenSpaceGlobalIlluminationDebugMode > 1.5)
        return float4(ssgi.aaa, encoded.a);
    if (screenSpaceGlobalIlluminationDebugMode > 0.5)
    {
        const float3 exposedRadiance =
            1.0 - exp2(-ssgi.rgb * 32.0);
        const float3 invalidGuide = float3(0.25, 0.0, 0.25);
        return float4(
            lerp(invalidGuide, exposedRadiance, saturate(ssgi.a)),
            encoded.a);
    }
    linearColor += ssgi.rgb * screenSpaceGlobalIlluminationIntensity;
#endif
#if BFVR_WATER_REFLECTIONS
    const float4 reflection = waterReflectionTexture.SampleLevel(
        sourceSampler, texcoord, 0.0);
    linearColor = lerp(
        linearColor,
        reflection.rgb,
        saturate(reflection.a * waterReflectionIntensity));
#endif
#if BFVR_BLOOM
    linearColor += bloomTexture.SampleLevel(
        sourceSampler, texcoord, 0.0).rgb * bloomIntensity;
#endif
    return float4(linearColor, encoded.a);
}
)";

constexpr char kFxaaPixelShader[] = R"(
Texture2D sourceTexture : register(t0);
#if BFVR_AMBIENT_OCCLUSION
Texture2D ambientOcclusionTexture : register(t1);
#endif
#if BFVR_BLOOM
Texture2D bloomTexture : register(t2);
#endif
#if BFVR_WATER_REFLECTIONS
Texture2D waterReflectionTexture : register(t3);
#endif
#if BFVR_SSGI
Texture2D screenSpaceGlobalIlluminationTexture : register(t4);
#endif
SamplerState sourceSampler : register(s0);
cbuffer Configuration : register(b0)
{
    float sourceAlreadyLinear;
    float bloomThreshold;
    float bloomIntensity;
    float ambientOcclusionIntensity;
    float2 bloomTexelSize;
    float waterReflectionIntensity;
    float screenSpaceGlobalIlluminationIntensity;
    float screenSpaceGlobalIlluminationDebugMode;
    float3 configurationPadding2;
};

// NVIDIA FXAA quality controls, retained as compile-time constants because
// BFVR has one owner-approved world-AA profile rather than a runtime slider.
const float kFxaaQualitySubpixel = 0.75;
const float kFxaaQualityEdgeThreshold = 0.166;
const float kFxaaQualityEdgeThresholdMin = 0.0833;

float Luma(float3 color)
{
    return dot(color, float3(0.299, 0.587, 0.114));
}

float3 SrgbToLinear(float3 color)
{
    const float3 low = color / 12.92;
    const float3 high = pow((color + 0.055) / 1.055, 2.4);
    return lerp(high, low, step(color, 0.04045));
}

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target
{
    uint width;
    uint height;
    sourceTexture.GetDimensions(width, height);
    const float2 texel = 1.0 / float2(width, height);

    const float4 center = sourceTexture.SampleLevel(
        sourceSampler, texcoord, 0.0);
    const float3 northwest = sourceTexture.SampleLevel(
        sourceSampler, texcoord + float2(-1.0, -1.0) * texel, 0.0).rgb;
    const float3 northeast = sourceTexture.SampleLevel(
        sourceSampler, texcoord + float2(1.0, -1.0) * texel, 0.0).rgb;
    const float3 southwest = sourceTexture.SampleLevel(
        sourceSampler, texcoord + float2(-1.0, 1.0) * texel, 0.0).rgb;
    const float3 southeast = sourceTexture.SampleLevel(
        sourceSampler, texcoord + float2(1.0, 1.0) * texel, 0.0).rgb;

    const float lumaCenter = Luma(center.rgb);
    const float lumaNorthwest = Luma(northwest);
    const float lumaNortheast = Luma(northeast);
    const float lumaSouthwest = Luma(southwest);
    const float lumaSoutheast = Luma(southeast);
    const float lumaMinimum = min(
        lumaCenter,
        min(
            min(lumaNorthwest, lumaNortheast),
            min(lumaSouthwest, lumaSoutheast)));
    const float lumaMaximum = max(
        lumaCenter,
        max(
            max(lumaNorthwest, lumaNortheast),
            max(lumaSouthwest, lumaSoutheast)));
    const float lumaRange = lumaMaximum - lumaMinimum;

    float3 filtered = center.rgb;
    if (lumaRange >= max(
            kFxaaQualityEdgeThresholdMin,
            lumaMaximum * kFxaaQualityEdgeThreshold))
    {
        float2 direction = float2(
            -((lumaNorthwest + lumaNortheast) -
              (lumaSouthwest + lumaSoutheast)),
            (lumaNorthwest + lumaSouthwest) -
              (lumaNortheast + lumaSoutheast));
        const float directionReduce = max(
            (lumaNorthwest + lumaNortheast +
             lumaSouthwest + lumaSoutheast) * (0.25 * 0.03125),
            1.0 / 128.0);
        const float reciprocalMinimum =
            1.0 / (min(abs(direction.x), abs(direction.y)) +
                   directionReduce);
        direction = clamp(
            direction * reciprocalMinimum,
            float2(-8.0, -8.0),
            float2(8.0, 8.0)) * texel;

        const float3 rgbA = 0.5 * (
            sourceTexture.SampleLevel(
                sourceSampler,
                texcoord + direction * (1.0 / 3.0 - 0.5),
                0.0).rgb +
            sourceTexture.SampleLevel(
                sourceSampler,
                texcoord + direction * (2.0 / 3.0 - 0.5),
                0.0).rgb);
        const float3 rgbB = rgbA * 0.5 + 0.25 * (
            sourceTexture.SampleLevel(
                sourceSampler,
                texcoord + direction * -0.5,
                0.0).rgb +
            sourceTexture.SampleLevel(
                sourceSampler,
                texcoord + direction * 0.5,
                0.0).rgb);
        const float lumaB = Luma(rgbB);
        filtered = lumaB < lumaMinimum || lumaB > lumaMaximum
            ? rgbA
            : rgbB;
    }

    // The compact BFVR variant already has a diagonal 2x2 luma neighbourhood.
    // Use it for FXAA's subpixel blend without adding another texture fetch.
    const float neighbourhoodLuma =
        (lumaNorthwest + lumaNortheast + lumaSouthwest + lumaSoutheast) *
        0.25;
    const float subpixelContrast = saturate(
        abs(lumaCenter - neighbourhoodLuma) / max(lumaRange, 0.0001));
    const float subpixelBlend =
        subpixelContrast * subpixelContrast * kFxaaQualitySubpixel;
    const float3 neighbourhoodColor =
        (center.rgb + northwest + northeast + southwest + southeast) * 0.2;
    filtered = lerp(filtered, neighbourhoodColor, subpixelBlend);

    float3 linearColor = sourceAlreadyLinear > 0.5
        ? filtered
        : SrgbToLinear(filtered);
#if BFVR_AMBIENT_OCCLUSION
    const float ao = ambientOcclusionTexture.SampleLevel(
        sourceSampler, texcoord, 0.0).r;
    linearColor *= lerp(1.0, ao, ambientOcclusionIntensity);
#endif
#if BFVR_SSGI
    const float4 ssgi = screenSpaceGlobalIlluminationTexture.SampleLevel(
        sourceSampler, texcoord, 0.0);
    if (screenSpaceGlobalIlluminationDebugMode > 1.5)
        return float4(ssgi.aaa, center.a);
    if (screenSpaceGlobalIlluminationDebugMode > 0.5)
    {
        const float3 exposedRadiance =
            1.0 - exp2(-ssgi.rgb * 32.0);
        const float3 invalidGuide = float3(0.25, 0.0, 0.25);
        return float4(
            lerp(invalidGuide, exposedRadiance, saturate(ssgi.a)),
            center.a);
    }
    linearColor += ssgi.rgb * screenSpaceGlobalIlluminationIntensity;
#endif
#if BFVR_WATER_REFLECTIONS
    const float4 reflection = waterReflectionTexture.SampleLevel(
        sourceSampler, texcoord, 0.0);
    linearColor = lerp(
        linearColor,
        reflection.rgb,
        saturate(reflection.a * waterReflectionIntensity));
#endif
#if BFVR_BLOOM
    linearColor += bloomTexture.SampleLevel(
        sourceSampler, texcoord, 0.0).rgb * bloomIntensity;
#endif
    return float4(linearColor, center.a);
}
)";

constexpr char kBloomDownsamplePixelShader[] = R"(
Texture2D sourceTexture : register(t0);
SamplerState sourceSampler : register(s0);
cbuffer Configuration : register(b0)
{
    float sourceAlreadyLinear;
    float bloomThreshold;
    float bloomIntensity;
    float ambientOcclusionIntensity;
    float2 bloomTexelSize;
    float2 configurationPadding1;
};

float3 SrgbToLinear(float3 color)
{
    const float3 low = color / 12.92;
    const float3 high = pow((color + 0.055) / 1.055, 2.4);
    return lerp(high, low, step(color, 0.04045));
}

float3 ExtractBloom(float3 linearColor)
{
    const float luminance = dot(linearColor, float3(0.2126, 0.7152, 0.0722));
    // LDR BF1942 has no emissive/HDR values above white. A fixed soft knee
    // avoids an abrupt halo boundary while retaining the brightness control.
    const float brightWeight = smoothstep(
        max(bloomThreshold - 0.10, 0.0),
        min(bloomThreshold + 0.10, 1.0),
        luminance);
    return linearColor * brightWeight;
}

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target
{
    uint width;
    uint height;
    sourceTexture.GetDimensions(width, height);
    const float2 texel = 1.0 / float2(width, height);
    // Four bilinear taps cover the source footprint of one quarter-resolution
    // output pixel. Extract each tap before averaging so a small bright object
    // is not erased by the downsample itself.
    const float2 offsets[4] = {
        float2(-1.0, -1.0),
        float2(1.0, -1.0),
        float2(-1.0, 1.0),
        float2(1.0, 1.0)};
    float3 bloom = float3(0.0, 0.0, 0.0);
    [unroll]
    for (uint index = 0; index < 4; ++index)
    {
        const float3 encoded = sourceTexture.SampleLevel(
            sourceSampler,
            texcoord + offsets[index] * texel,
            0.0).rgb;
        const float3 linearColor = sourceAlreadyLinear > 0.5
            ? encoded
            : SrgbToLinear(encoded);
        bloom += ExtractBloom(linearColor);
    }
    return float4(bloom * 0.25, 1.0);
}
)";

constexpr char kBloomBlurHorizontalPixelShader[] = R"(
Texture2D bloomTexture : register(t0);
SamplerState sourceSampler : register(s0);
cbuffer Configuration : register(b0)
{
    float sourceAlreadyLinear;
    float bloomThreshold;
    float bloomIntensity;
    float ambientOcclusionIntensity;
    float2 bloomTexelSize;
    float2 configurationPadding1;
};

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target
{
    const float2 step = float2(bloomTexelSize.x, 0.0);
    float3 color = bloomTexture.SampleLevel(sourceSampler, texcoord, 0.0).rgb * 0.227027;
    color += bloomTexture.SampleLevel(sourceSampler, texcoord + step, 0.0).rgb * 0.1945946;
    color += bloomTexture.SampleLevel(sourceSampler, texcoord - step, 0.0).rgb * 0.1945946;
    color += bloomTexture.SampleLevel(sourceSampler, texcoord + step * 2.0, 0.0).rgb * 0.1216216;
    color += bloomTexture.SampleLevel(sourceSampler, texcoord - step * 2.0, 0.0).rgb * 0.1216216;
    color += bloomTexture.SampleLevel(sourceSampler, texcoord + step * 3.0, 0.0).rgb * 0.054054;
    color += bloomTexture.SampleLevel(sourceSampler, texcoord - step * 3.0, 0.0).rgb * 0.054054;
    color += bloomTexture.SampleLevel(sourceSampler, texcoord + step * 4.0, 0.0).rgb * 0.016216;
    color += bloomTexture.SampleLevel(sourceSampler, texcoord - step * 4.0, 0.0).rgb * 0.016216;
    return float4(color, 1.0);
}
)";

constexpr char kBloomBlurVerticalPixelShader[] = R"(
Texture2D bloomTexture : register(t0);
SamplerState sourceSampler : register(s0);
cbuffer Configuration : register(b0)
{
    float sourceAlreadyLinear;
    float bloomThreshold;
    float bloomIntensity;
    float ambientOcclusionIntensity;
    float2 bloomTexelSize;
    float2 configurationPadding1;
};

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target
{
    const float2 step = float2(0.0, bloomTexelSize.y);
    float3 color = bloomTexture.SampleLevel(sourceSampler, texcoord, 0.0).rgb * 0.227027;
    color += bloomTexture.SampleLevel(sourceSampler, texcoord + step, 0.0).rgb * 0.1945946;
    color += bloomTexture.SampleLevel(sourceSampler, texcoord - step, 0.0).rgb * 0.1945946;
    color += bloomTexture.SampleLevel(sourceSampler, texcoord + step * 2.0, 0.0).rgb * 0.1216216;
    color += bloomTexture.SampleLevel(sourceSampler, texcoord - step * 2.0, 0.0).rgb * 0.1216216;
    color += bloomTexture.SampleLevel(sourceSampler, texcoord + step * 3.0, 0.0).rgb * 0.054054;
    color += bloomTexture.SampleLevel(sourceSampler, texcoord - step * 3.0, 0.0).rgb * 0.054054;
    color += bloomTexture.SampleLevel(sourceSampler, texcoord + step * 4.0, 0.0).rgb * 0.016216;
    color += bloomTexture.SampleLevel(sourceSampler, texcoord - step * 4.0, 0.0).rgb * 0.016216;
    return float4(color, 1.0);
}
)";

bool CompileShader(
    const char* source,
    const char* target,
    ID3DBlob** bytecode,
    bfvr::shared::SharedTextureLogCallback logCallback,
    void* logContext,
    bool ambientOcclusionVariant = false,
    bool screenSpaceGlobalIlluminationVariant = false,
    bool bloomVariant = false,
    bool waterReflectionVariant = false)
{
    const D3D_SHADER_MACRO defines[] = {
        {"BFVR_AMBIENT_OCCLUSION", ambientOcclusionVariant ? "1" : "0"},
        {"BFVR_SSGI", screenSpaceGlobalIlluminationVariant ? "1" : "0"},
        {"BFVR_BLOOM", bloomVariant ? "1" : "0"},
        {"BFVR_WATER_REFLECTIONS", waterReflectionVariant ? "1" : "0"},
        {nullptr, nullptr}};
    ID3DBlob* errors = nullptr;
    const HRESULT result = D3DCompile(
        source,
        strlen(source),
        "BFVR-D3D11TextureScaler",
        defines,
        nullptr,
        "main",
        target,
        D3DCOMPILE_ENABLE_STRICTNESS,
        0,
        bytecode,
        &errors);
    if (FAILED(result) && logCallback != nullptr)
    {
        wchar_t message[1024] = {};
        if (errors != nullptr && errors->GetBufferPointer() != nullptr)
        {
            swprintf_s(
                message,
                L"D3D11 texture scaler could not compile %S: %S (HRESULT=0x%08lX).",
                target,
                static_cast<const char*>(errors->GetBufferPointer()),
                static_cast<unsigned long>(result));
        }
        else
        {
            swprintf_s(
                message,
                L"D3D11 texture scaler could not compile %S (HRESULT=0x%08lX).",
                target,
                static_cast<unsigned long>(result));
        }
        logCallback(logContext, message);
    }
    if (errors != nullptr)
    {
        errors->Release();
    }
    return SUCCEEDED(result) && *bytecode != nullptr;
}
} // namespace

namespace bfvr::shared
{
D3D11TextureScaler::~D3D11TextureScaler()
{
    Shutdown();
}

bool D3D11TextureScaler::Initialize(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    SharedTextureLogCallback logCallback,
    void* logContext,
    bool enableBloom,
    bool enableAmbientOcclusion,
    bool enableScreenSpaceGlobalIllumination,
    bool enableWaterReflections)
{
    Shutdown();
    logCallback_ = logCallback;
    logContext_ = logContext;
    if (device == nullptr || context == nullptr)
    {
        WriteLog(L"D3D11 texture scaler received a null device or context.");
        return false;
    }
    device_ = device;
    device_->AddRef();

    ID3DBlob* vertexBytecode = nullptr;
    ID3DBlob* pixelBytecode = nullptr;
    HRESULT result = E_FAIL;
    if (CompileShader(
            kFullscreenVertexShader,
            "vs_4_0",
            &vertexBytecode,
            logCallback_,
            logContext_))
    {
        result = device->CreateVertexShader(
            vertexBytecode->GetBufferPointer(),
            vertexBytecode->GetBufferSize(),
            nullptr,
            &vertexShader_);
    }
    if (SUCCEEDED(result) &&
        CompileShader(
            kTexturePixelShader,
            "ps_4_0",
            &pixelBytecode,
            logCallback_,
            logContext_))
    {
        result = device->CreatePixelShader(
            pixelBytecode->GetBufferPointer(),
            pixelBytecode->GetBufferSize(),
            nullptr,
            &colorPixelShader_);
    }
    if (pixelBytecode != nullptr)
    {
        pixelBytecode->Release();
        pixelBytecode = nullptr;
    }
    if (vertexBytecode != nullptr)
    {
        vertexBytecode->Release();
    }

    if (SUCCEEDED(result) &&
        CompileShader(
            kFxaaPixelShader,
            "ps_4_0",
            &pixelBytecode,
            logCallback_,
            logContext_))
    {
        result = device->CreatePixelShader(
            pixelBytecode->GetBufferPointer(),
            pixelBytecode->GetBufferSize(),
            nullptr,
            &fxaaPixelShader_);
    }
    if (pixelBytecode != nullptr)
    {
        pixelBytecode->Release();
        pixelBytecode = nullptr;
    }

    if (enableAmbientOcclusion || enableScreenSpaceGlobalIllumination ||
        enableBloom || enableWaterReflections)
    {
        if (SUCCEEDED(result) &&
            CompileShader(
                kTexturePixelShader,
                "ps_4_0",
                &pixelBytecode,
                logCallback_,
                logContext_,
                enableAmbientOcclusion,
                enableScreenSpaceGlobalIllumination,
                enableBloom,
                enableWaterReflections))
        {
            result = device->CreatePixelShader(
                pixelBytecode->GetBufferPointer(),
                pixelBytecode->GetBufferSize(),
                nullptr,
                &compositeColorPixelShader_);
        }
        if (pixelBytecode != nullptr)
        {
            pixelBytecode->Release();
            pixelBytecode = nullptr;
        }
        if (SUCCEEDED(result) &&
            CompileShader(
                kFxaaPixelShader,
                "ps_4_0",
                &pixelBytecode,
                logCallback_,
                logContext_,
                enableAmbientOcclusion,
                enableScreenSpaceGlobalIllumination,
                enableBloom,
                enableWaterReflections))
        {
            result = device->CreatePixelShader(
                pixelBytecode->GetBufferPointer(),
                pixelBytecode->GetBufferSize(),
                nullptr,
                &compositeFxaaPixelShader_);
        }
        if (pixelBytecode != nullptr)
        {
            pixelBytecode->Release();
            pixelBytecode = nullptr;
        }
    }

    if (enableBloom)
    {
        if (SUCCEEDED(result) &&
            CompileShader(
                kBloomDownsamplePixelShader,
                "ps_4_0",
                &pixelBytecode,
                logCallback_,
                logContext_))
        {
            result = device->CreatePixelShader(
                pixelBytecode->GetBufferPointer(),
                pixelBytecode->GetBufferSize(),
                nullptr,
                &bloomDownsamplePixelShader_);
        }
        if (pixelBytecode != nullptr)
        {
            pixelBytecode->Release();
            pixelBytecode = nullptr;
        }
        if (SUCCEEDED(result) &&
            CompileShader(
                kBloomBlurHorizontalPixelShader,
                "ps_4_0",
                &pixelBytecode,
                logCallback_,
                logContext_))
        {
            result = device->CreatePixelShader(
                pixelBytecode->GetBufferPointer(),
                pixelBytecode->GetBufferSize(),
                nullptr,
                &bloomBlurHorizontalPixelShader_);
        }
        if (pixelBytecode != nullptr)
        {
            pixelBytecode->Release();
            pixelBytecode = nullptr;
        }
        if (SUCCEEDED(result) &&
            CompileShader(
                kBloomBlurVerticalPixelShader,
                "ps_4_0",
                &pixelBytecode,
                logCallback_,
                logContext_))
        {
            result = device->CreatePixelShader(
                pixelBytecode->GetBufferPointer(),
                pixelBytecode->GetBufferSize(),
                nullptr,
                &bloomBlurVerticalPixelShader_);
        }
        if (pixelBytecode != nullptr)
        {
            pixelBytecode->Release();
            pixelBytecode = nullptr;
        }
    }

    D3D11_BUFFER_DESC configurationDescription = {};
    configurationDescription.ByteWidth = sizeof(float) * 12;
    configurationDescription.Usage = D3D11_USAGE_DEFAULT;
    configurationDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (SUCCEEDED(result))
    {
        result = device->CreateBuffer(
            &configurationDescription,
            nullptr,
            &configurationBuffer_);
    }

    D3D11_SAMPLER_DESC samplerDescription = {};
    samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
    if (SUCCEEDED(result))
    {
        result = device->CreateSamplerState(&samplerDescription, &sampler_);
    }
    if (enableBloom)
    {
        D3D11_QUERY_DESC queryDescription = {};
        queryDescription.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        result = SUCCEEDED(result)
            ? device->CreateQuery(&queryDescription, &bloomDisjointQuery_)
            : result;
        queryDescription.Query = D3D11_QUERY_TIMESTAMP;
        for (std::size_t eye = 0;
             eye < bloomTimestampStarts_.size() && SUCCEEDED(result);
             ++eye)
        {
            result = device->CreateQuery(
                &queryDescription,
                &bloomTimestampStarts_[eye]);
            result = SUCCEEDED(result)
                ? device->CreateQuery(
                    &queryDescription,
                    &bloomTimestampEnds_[eye])
                : result;
        }
    }
    if (FAILED(result) ||
        vertexShader_ == nullptr ||
        colorPixelShader_ == nullptr ||
        fxaaPixelShader_ == nullptr ||
        ((enableAmbientOcclusion || enableScreenSpaceGlobalIllumination ||
             enableBloom || enableWaterReflections) &&
            (compositeColorPixelShader_ == nullptr ||
             compositeFxaaPixelShader_ == nullptr)) ||
        (enableBloom &&
            (bloomDownsamplePixelShader_ == nullptr ||
             bloomBlurHorizontalPixelShader_ == nullptr ||
             bloomBlurVerticalPixelShader_ == nullptr ||
             bloomDisjointQuery_ == nullptr ||
             bloomTimestampStarts_[0] == nullptr ||
             bloomTimestampStarts_[1] == nullptr ||
             bloomTimestampEnds_[0] == nullptr ||
             bloomTimestampEnds_[1] == nullptr)) ||
        configurationBuffer_ == nullptr ||
        sampler_ == nullptr)
    {
        WriteLog(
            L"D3D11 texture scaler initialization failed (HRESULT=0x%08lX).",
            static_cast<unsigned long>(result));
        Shutdown();
        return false;
    }

    context_ = context;
    context_->AddRef();
    if (enableScreenSpaceGlobalIllumination)
    {
        WriteLog(
            L"D3D11 texture scaler initialized with an additive linear SSGI input; the input is sampled only for world eyes and Ref2 UI remains isolated.");
    }
    else if (enableWaterReflections)
    {
        WriteLog(
            L"D3D11 texture scaler initialized with a water-reflection composite input; the input is sampled only for world eyes and Ref2 UI remains isolated.");
    }
    else if (enableBloom)
    {
        WriteLog(
            enableAmbientOcclusion
                ? L"D3D11 texture scaler initialized with independent AO and quarter-resolution soft-knee bloom composite inputs, legacy-sRGB transfer correction, world-only FXAA, and isolated bloom GPU timestamps."
                : L"D3D11 texture scaler initialized with quarter-resolution soft-knee bloom, legacy-sRGB transfer correction, world-only FXAA (subpixel=0.75 edge=0.166 min=0.0833), and isolated bloom GPU timestamps.");
    }
    else
    {
        WriteLog(
            enableAmbientOcclusion
                ? L"D3D11 texture scaler initialized with separate default and AO-composite shaders, aspect-fit sampling, legacy-sRGB transfer correction, and world-only FXAA; bloom shaders are not compiled."
                : L"D3D11 texture scaler initialized with aspect-fit sampling, legacy-sRGB transfer correction, and world-only FXAA (subpixel=0.75 edge=0.166 min=0.0833); AO and bloom shaders are not compiled.");
    }
    return true;
}

bool D3D11TextureScaler::ScaleAspectFit(
    ID3D11ShaderResourceView* sourceView,
    UINT sourceWidth,
    UINT sourceHeight,
    ID3D11RenderTargetView* destinationView,
    UINT destinationWidth,
    UINT destinationHeight,
    bool transparentPadding,
    bool sourceAlreadyLinear,
    bool applyAntialiasing,
    ID3D11ShaderResourceView* ambientOcclusionView,
    float ambientOcclusionIntensity,
    ID3D11ShaderResourceView* screenSpaceGlobalIlluminationView,
    float screenSpaceGlobalIlluminationIntensity,
    float screenSpaceGlobalIlluminationDebugMode,
    ID3D11ShaderResourceView* waterReflectionView,
    float waterReflectionIntensity,
    bool applyBloom,
    float bloomThreshold,
    float bloomIntensity)
{
    if (context_ == nullptr ||
        device_ == nullptr ||
        sourceView == nullptr ||
        destinationView == nullptr ||
        sourceWidth == 0 ||
        sourceHeight == 0 ||
        destinationWidth == 0 ||
        destinationHeight == 0)
    {
        return false;
    }

    bloomThreshold = std::clamp(bloomThreshold, 0.0F, 1.0F);
    bloomIntensity = std::clamp(bloomIntensity, 0.0F, 2.0F);
    ambientOcclusionIntensity = std::clamp(
        ambientOcclusionIntensity,
        0.0F,
        1.0F);
    if (ambientOcclusionView == nullptr)
    {
        ambientOcclusionIntensity = 0.0F;
    }
    const bool applyAmbientOcclusion = ambientOcclusionIntensity > 0.0F;
    screenSpaceGlobalIlluminationIntensity = std::clamp(
        screenSpaceGlobalIlluminationIntensity,
        0.0F,
        2.0F);
    screenSpaceGlobalIlluminationDebugMode = std::clamp(
        screenSpaceGlobalIlluminationDebugMode,
        0.0F,
        2.0F);
    if (screenSpaceGlobalIlluminationView == nullptr)
    {
        screenSpaceGlobalIlluminationIntensity = 0.0F;
        screenSpaceGlobalIlluminationDebugMode = 0.0F;
    }
    const bool applyScreenSpaceGlobalIllumination =
        screenSpaceGlobalIlluminationIntensity > 0.0F ||
        screenSpaceGlobalIlluminationDebugMode > 0.0F;
    waterReflectionIntensity = std::clamp(
        waterReflectionIntensity,
        0.0F,
        2.0F);
    if (waterReflectionView == nullptr)
        waterReflectionIntensity = 0.0F;
    const bool applyWaterReflections = waterReflectionIntensity > 0.0F;
    if ((applyAmbientOcclusion || applyScreenSpaceGlobalIllumination ||
            applyWaterReflections) &&
        (compositeColorPixelShader_ == nullptr ||
         compositeFxaaPixelShader_ == nullptr))
    {
        return false;
    }
    applyBloom = applyBloom && bloomIntensity > 0.0F;
    if (applyBloom &&
        (!EnsureBloomResources(sourceWidth, sourceHeight) ||
         !BuildBloom(sourceView, sourceAlreadyLinear, bloomThreshold)))
    {
        return false;
    }

    const float horizontalScale =
        static_cast<float>(destinationWidth) / static_cast<float>(sourceWidth);
    const float verticalScale =
        static_cast<float>(destinationHeight) / static_cast<float>(sourceHeight);
    const float scale = std::min(horizontalScale, verticalScale);
    const float fittedWidth = static_cast<float>(sourceWidth) * scale;
    const float fittedHeight = static_cast<float>(sourceHeight) * scale;
    const D3D11_VIEWPORT viewport = {
        (static_cast<float>(destinationWidth) - fittedWidth) * 0.5F,
        (static_cast<float>(destinationHeight) - fittedHeight) * 0.5F,
        fittedWidth,
        fittedHeight,
        0.0F,
        1.0F};
    const float clearColor[4] = {
        0.0F,
        0.0F,
        0.0F,
        transparentPadding ? 0.0F : 1.0F};

    context_->ClearRenderTargetView(destinationView, clearColor);
    context_->OMSetRenderTargets(1, &destinationView, nullptr);
    context_->RSSetViewports(1, &viewport);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertexShader_, nullptr, 0);
    UpdateConfiguration(
        sourceAlreadyLinear,
        ambientOcclusionIntensity,
        bloomThreshold,
        applyBloom ? bloomIntensity : 0.0F,
        applyBloom ? 1.0F / static_cast<float>(bloomWidth_) : 0.0F,
        applyBloom ? 1.0F / static_cast<float>(bloomHeight_) : 0.0F,
        screenSpaceGlobalIlluminationIntensity,
        screenSpaceGlobalIlluminationDebugMode,
        waterReflectionIntensity);
    context_->PSSetShader(
        applyAmbientOcclusion || applyScreenSpaceGlobalIllumination ||
                applyBloom || applyWaterReflections
            ? applyAntialiasing
                ? compositeFxaaPixelShader_
                : compositeColorPixelShader_
            : applyAntialiasing ? fxaaPixelShader_ : colorPixelShader_,
        nullptr,
        0);
    context_->PSSetConstantBuffers(0, 1, &configurationBuffer_);
    context_->PSSetSamplers(0, 1, &sampler_);
    ID3D11ShaderResourceView* sourceViews[5] = {
        sourceView,
        ambientOcclusionView,
        applyBloom ? bloomViews_[0] : nullptr,
        waterReflectionView,
        screenSpaceGlobalIlluminationView};
    context_->PSSetShaderResources(0, 5, sourceViews);
    context_->Draw(3, 0);

    ID3D11ShaderResourceView* nullViews[5] = {};
    context_->PSSetShaderResources(0, 5, nullViews);
    ID3D11RenderTargetView* nullTarget = nullptr;
    context_->OMSetRenderTargets(1, &nullTarget, nullptr);
    return true;
}

bool D3D11TextureScaler::EnsureBloomResources(UINT sourceWidth, UINT sourceHeight)
{
    const UINT requiredWidth = std::max<UINT>(1, (sourceWidth + 3) / 4);
    const UINT requiredHeight = std::max<UINT>(1, (sourceHeight + 3) / 4);
    if (bloomWidth_ == requiredWidth &&
        bloomHeight_ == requiredHeight &&
        bloomTextures_[0] != nullptr && bloomTextures_[1] != nullptr &&
        bloomViews_[0] != nullptr && bloomViews_[1] != nullptr &&
        bloomTargets_[0] != nullptr && bloomTargets_[1] != nullptr)
    {
        return true;
    }

    ReleaseBloomResources();
    D3D11_TEXTURE2D_DESC description = {};
    description.Width = requiredWidth;
    description.Height = requiredHeight;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    HRESULT result = S_OK;
    for (std::size_t index = 0; index < std::size(bloomTextures_); ++index)
    {
        result = device_->CreateTexture2D(
            &description,
            nullptr,
            &bloomTextures_[index]);
        if (SUCCEEDED(result))
        {
            result = device_->CreateShaderResourceView(
                bloomTextures_[index],
                nullptr,
                &bloomViews_[index]);
        }
        if (SUCCEEDED(result))
        {
            result = device_->CreateRenderTargetView(
                bloomTextures_[index],
                nullptr,
                &bloomTargets_[index]);
        }
        if (FAILED(result))
        {
            WriteLog(
                L"D3D11 texture scaler could not allocate quarter-resolution bloom targets %ux%u (HRESULT=0x%08lX).",
                requiredWidth,
                requiredHeight,
                static_cast<unsigned long>(result));
            ReleaseBloomResources();
            return false;
        }
    }
    bloomWidth_ = requiredWidth;
    bloomHeight_ = requiredHeight;
    WriteLog(
        L"D3D11 texture scaler allocated two quarter-resolution HDR bloom targets (%ux%u).",
        bloomWidth_,
        bloomHeight_);
    return true;
}

bool D3D11TextureScaler::BuildBloom(
    ID3D11ShaderResourceView* sourceView,
    bool sourceAlreadyLinear,
    float bloomThreshold)
{
    if (sourceView == nullptr || bloomWidth_ == 0 || bloomHeight_ == 0)
    {
        return false;
    }
    const std::size_t timingEye = bloomFrameEyeCount_;
    const bool timeThisEye =
        bloomFrameTimingActive_ && timingEye < bloomTimestampStarts_.size();
    if (timeThisEye)
        context_->End(bloomTimestampStarts_[timingEye]);

    const D3D11_VIEWPORT bloomViewport = {
        0.0F,
        0.0F,
        static_cast<float>(bloomWidth_),
        static_cast<float>(bloomHeight_),
        0.0F,
        1.0F};
    auto drawBloomPass = [this, &bloomViewport](
                             ID3D11ShaderResourceView* input,
                             ID3D11RenderTargetView* output,
                             ID3D11PixelShader* shader,
                             bool inputAlreadyLinear,
                             float threshold) -> bool
    {
        if (input == nullptr || output == nullptr || shader == nullptr)
        {
            return false;
        }
        UpdateConfiguration(
            inputAlreadyLinear,
            0.0F,
            threshold,
            0.0F,
            1.0F / static_cast<float>(bloomWidth_),
            1.0F / static_cast<float>(bloomHeight_),
            0.0F,
            0.0F,
            0.0F);
        context_->OMSetRenderTargets(1, &output, nullptr);
        context_->RSSetViewports(1, &bloomViewport);
        context_->IASetInputLayout(nullptr);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(vertexShader_, nullptr, 0);
        context_->PSSetShader(shader, nullptr, 0);
        context_->PSSetConstantBuffers(0, 1, &configurationBuffer_);
        context_->PSSetSamplers(0, 1, &sampler_);
        context_->PSSetShaderResources(0, 1, &input);
        context_->Draw(3, 0);
        ID3D11ShaderResourceView* nullView = nullptr;
        context_->PSSetShaderResources(0, 1, &nullView);
        ID3D11RenderTargetView* nullTarget = nullptr;
        context_->OMSetRenderTargets(1, &nullTarget, nullptr);
        return true;
    };

    const bool built = drawBloomPass(
            sourceView,
            bloomTargets_[0],
            bloomDownsamplePixelShader_,
            sourceAlreadyLinear,
            bloomThreshold) &&
        drawBloomPass(
            bloomViews_[0],
            bloomTargets_[1],
            bloomBlurHorizontalPixelShader_,
            true,
            bloomThreshold) &&
        drawBloomPass(
            bloomViews_[1],
            bloomTargets_[0],
            bloomBlurVerticalPixelShader_,
            true,
            bloomThreshold);
    if (timeThisEye)
    {
        context_->End(bloomTimestampEnds_[timingEye]);
        bloomFrameEyesBuilt_[timingEye] = built;
        ++bloomFrameEyeCount_;
    }
    return built;
}

bool D3D11TextureScaler::BeginBloomFrame()
{
    if (context_ == nullptr || bloomDisjointQuery_ == nullptr ||
        bloomFrameTimingActive_)
    {
        return false;
    }
    context_->Begin(bloomDisjointQuery_);
    bloomFrameEyesBuilt_ = {};
    bloomFrameEyeCount_ = 0;
    bloomFrameTimingActive_ = true;
    return true;
}

void D3D11TextureScaler::EndBloomFrame()
{
    if (context_ != nullptr && bloomDisjointQuery_ != nullptr &&
        bloomFrameTimingActive_)
    {
        context_->End(bloomDisjointQuery_);
    }
}

void D3D11TextureScaler::CollectBloomFrameTimings()
{
    if (context_ == nullptr || !bloomFrameTimingActive_)
        return;
    bloomFrameTimingActive_ = false;

    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint = {};
    if (context_->GetData(
            bloomDisjointQuery_,
            &disjoint,
            sizeof(disjoint),
            D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
        disjoint.Disjoint || disjoint.Frequency == 0)
    {
        return;
    }
    std::array<double, 2> eyeMilliseconds = {};
    std::array<bool, 2> eyeValid = {};
    for (std::size_t eye = 0; eye < bloomTimestampStarts_.size(); ++eye)
    {
        if (!bloomFrameEyesBuilt_[eye])
            continue;
        UINT64 started = 0;
        UINT64 ended = 0;
        if (context_->GetData(
                bloomTimestampStarts_[eye],
                &started,
                sizeof(started),
                D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            context_->GetData(
                bloomTimestampEnds_[eye],
                &ended,
                sizeof(ended),
                D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            ended >= started)
        {
            eyeMilliseconds[eye] =
                static_cast<double>(ended - started) * 1000.0 /
                static_cast<double>(disjoint.Frequency);
            eyeValid[eye] = true;
            if (bloomGpuMilliseconds_[eye].size() < 8192)
                bloomGpuMilliseconds_[eye].push_back(eyeMilliseconds[eye]);
        }
    }
    if (eyeValid[0] && eyeValid[1] &&
        bloomStereoGpuMilliseconds_.size() < 8192)
    {
        bloomStereoGpuMilliseconds_.push_back(
            eyeMilliseconds[0] + eyeMilliseconds[1]);
    }
}

void D3D11TextureScaler::ReleaseBloomResources()
{
    for (std::size_t index = 0; index < std::size(bloomTextures_); ++index)
    {
        if (bloomTargets_[index] != nullptr)
        {
            bloomTargets_[index]->Release();
            bloomTargets_[index] = nullptr;
        }
        if (bloomViews_[index] != nullptr)
        {
            bloomViews_[index]->Release();
            bloomViews_[index] = nullptr;
        }
        if (bloomTextures_[index] != nullptr)
        {
            bloomTextures_[index]->Release();
            bloomTextures_[index] = nullptr;
        }
    }
    bloomWidth_ = 0;
    bloomHeight_ = 0;
}

void D3D11TextureScaler::UpdateConfiguration(
    bool sourceAlreadyLinear,
    float ambientOcclusionIntensity,
    float bloomThreshold,
    float bloomIntensity,
    float bloomTexelWidth,
    float bloomTexelHeight,
    float screenSpaceGlobalIlluminationIntensity,
    float screenSpaceGlobalIlluminationDebugMode,
    float waterReflectionIntensity)
{
    const float configuration[12] = {
        sourceAlreadyLinear ? 1.0F : 0.0F,
        bloomThreshold,
        bloomIntensity,
        ambientOcclusionIntensity,
        bloomTexelWidth,
        bloomTexelHeight,
        waterReflectionIntensity,
        screenSpaceGlobalIlluminationIntensity,
        screenSpaceGlobalIlluminationDebugMode,
        0.0F,
        0.0F,
        0.0F};
    context_->UpdateSubresource(
        configurationBuffer_,
        0,
        nullptr,
        configuration,
        0,
        0);
}

void D3D11TextureScaler::ReportBloomTimings()
{
    if (bloomTimingsReported_)
        return;
    bloomTimingsReported_ = true;
    auto report = [this](
                      const wchar_t* label,
                      const std::vector<double>& samples)
    {
        if (samples.empty())
            return;
        std::vector<double> sorted = samples;
        std::sort(sorted.begin(), sorted.end());
        const std::size_t p95Index = static_cast<std::size_t>(
            std::ceil(static_cast<double>(sorted.size()) * 0.95)) - 1;
        WriteLog(
            L"D3D11 bloom GPU summary %s samples=%zu median=%.4f ms p95=%.4f ms max=%.4f ms.",
            label,
            sorted.size(),
            sorted[sorted.size() / 2],
            sorted[(std::min)(p95Index, sorted.size() - 1)],
            sorted.back());
    };
    report(L"left extract+blur", bloomGpuMilliseconds_[0]);
    report(L"right extract+blur", bloomGpuMilliseconds_[1]);
    report(L"stereo extract+blur", bloomStereoGpuMilliseconds_);
}

void D3D11TextureScaler::Shutdown()
{
    ReportBloomTimings();
    bloomFrameTimingActive_ = false;
    ReleaseBloomResources();
    for (ID3D11Query*& query : bloomTimestampEnds_)
    {
        if (query != nullptr)
        {
            query->Release();
            query = nullptr;
        }
    }
    for (ID3D11Query*& query : bloomTimestampStarts_)
    {
        if (query != nullptr)
        {
            query->Release();
            query = nullptr;
        }
    }
    if (bloomDisjointQuery_ != nullptr)
    {
        bloomDisjointQuery_->Release();
        bloomDisjointQuery_ = nullptr;
    }
    if (sampler_ != nullptr)
    {
        sampler_->Release();
        sampler_ = nullptr;
    }
    if (configurationBuffer_ != nullptr)
    {
        configurationBuffer_->Release();
        configurationBuffer_ = nullptr;
    }
    if (fxaaPixelShader_ != nullptr)
    {
        fxaaPixelShader_->Release();
        fxaaPixelShader_ = nullptr;
    }
    if (compositeFxaaPixelShader_ != nullptr)
    {
        compositeFxaaPixelShader_->Release();
        compositeFxaaPixelShader_ = nullptr;
    }
    if (compositeColorPixelShader_ != nullptr)
    {
        compositeColorPixelShader_->Release();
        compositeColorPixelShader_ = nullptr;
    }
    if (bloomBlurVerticalPixelShader_ != nullptr)
    {
        bloomBlurVerticalPixelShader_->Release();
        bloomBlurVerticalPixelShader_ = nullptr;
    }
    if (bloomBlurHorizontalPixelShader_ != nullptr)
    {
        bloomBlurHorizontalPixelShader_->Release();
        bloomBlurHorizontalPixelShader_ = nullptr;
    }
    if (bloomDownsamplePixelShader_ != nullptr)
    {
        bloomDownsamplePixelShader_->Release();
        bloomDownsamplePixelShader_ = nullptr;
    }
    if (colorPixelShader_ != nullptr)
    {
        colorPixelShader_->Release();
        colorPixelShader_ = nullptr;
    }
    if (vertexShader_ != nullptr)
    {
        vertexShader_->Release();
        vertexShader_ = nullptr;
    }
    if (context_ != nullptr)
    {
        context_->Release();
        context_ = nullptr;
    }
    if (device_ != nullptr)
    {
        device_->Release();
        device_ = nullptr;
    }
    bloomGpuMilliseconds_ = {};
    bloomStereoGpuMilliseconds_.clear();
    bloomFrameEyesBuilt_ = {};
    bloomFrameEyeCount_ = 0;
    bloomTimingsReported_ = false;
    logCallback_ = nullptr;
    logContext_ = nullptr;
}

void D3D11TextureScaler::WriteLog(const wchar_t* format, ...) const
{
    if (logCallback_ == nullptr)
    {
        return;
    }
    wchar_t message[1024] = {};
    va_list arguments;
    va_start(arguments, format);
    _vsnwprintf_s(message, std::size(message), _TRUNCATE, format, arguments);
    va_end(arguments);
    logCallback_(logContext_, message);
}
} // namespace bfvr::shared
