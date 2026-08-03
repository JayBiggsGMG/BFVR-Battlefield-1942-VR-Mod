#include "presenter/D3D11AmbientOcclusion.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
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

constexpr char kEvaluatePixelShader[] = R"(
Texture2D packedDepthTexture : register(t0);
SamplerState pointSampler : register(s0);

cbuffer Configuration : register(b0)
{
    float4 projection0; // m00, m11, m20, m21
    float4 projection1; // m22, m32, 1/width, 1/height
    float4 aoParameters0; // viewRadius, radiusLimitPixels, minResolvedPixels, bias
    float4 aoParameters1; // strength, power, blurSharpness, unused
};

float DecodeDepth(float4 packed)
{
    return dot(packed.rgb, float3(1.0, 1.0 / 255.0, 1.0 / 65025.0));
}

float3 ReconstructViewPosition(float2 uv, float depth)
{
    const float denominator = depth - projection1.x;
    const float viewZ = projection1.y / (
        abs(denominator) < 0.0000001
            ? (denominator < 0.0 ? -0.0000001 : 0.0000001)
            : denominator);
    const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    return float3(
        viewZ * (ndc.x - projection0.z) / projection0.x,
        viewZ * (ndc.y - projection0.w) / projection0.y,
        viewZ);
}

float3 ReadPosition(float2 uv)
{
    const float depth = DecodeDepth(
        packedDepthTexture.SampleLevel(pointSampler, saturate(uv), 0.0));
    return ReconstructViewPosition(uv, depth);
}

float InterleavedGradientNoise(float2 pixel)
{
    return frac(
        52.9829189 * frac(
            dot(pixel, float2(0.06711056, 0.00583715))));
}

float SmoothLimitRadius(float radiusPixels, float maximumPixels)
{
    // Preserve the exact projected radius through half the limit, then use a
    // quadratic transition whose slope reaches zero at 1.5 times the limit.
    // This avoids the derivative discontinuity of a hard pixel-radius clamp.
    const float transitionStart = maximumPixels * 0.5;
    const float transitionEnd = maximumPixels * 1.5;
    if (radiusPixels <= transitionStart)
        return radiusPixels;
    if (radiusPixels >= transitionEnd)
        return maximumPixels;
    const float normalized =
        (radiusPixels - transitionStart) /
        max(transitionEnd - transitionStart, 0.0001);
    return transitionStart +
        (transitionEnd - transitionStart) *
        (normalized - 0.5 * normalized * normalized);
}

float main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target
{
    const float centerDepth = DecodeDepth(
        packedDepthTexture.SampleLevel(pointSampler, texcoord, 0.0));
    if (centerDepth >= 0.9999)
        return 1.0;

    const float2 texel = projection1.zw;
    const float3 center = ReconstructViewPosition(texcoord, centerDepth);
    const float3 left = ReadPosition(texcoord - float2(texel.x, 0.0));
    const float3 right = ReadPosition(texcoord + float2(texel.x, 0.0));
    const float3 up = ReadPosition(texcoord - float2(0.0, texel.y));
    const float3 down = ReadPosition(texcoord + float2(0.0, texel.y));
    const float3 dx = abs(right.z - center.z) < abs(center.z - left.z)
        ? right - center
        : center - left;
    const float3 dy = abs(down.z - center.z) < abs(center.z - up.z)
        ? down - center
        : center - up;
    const float3 unnormalizedNormal = cross(dx, dy);
    const float normalLengthSquared = dot(
        unnormalizedNormal,
        unnormalizedNormal);
    float3 normal = normalLengthSquared > 0.00000001
        ? unnormalizedNormal * rsqrt(normalLengthSquared)
        : float3(0.0, 0.0, -1.0);
    if (normal.z > 0.0)
        normal = -normal;

    const float viewRadius = aoParameters0.x;
    const float projectedRadiusPixels =
        viewRadius * abs(projection0.y) * 0.5 /
        max(abs(center.z) * projection1.w, 0.0001);
    const float radiusPixels = max(
        SmoothLimitRadius(projectedRadiusPixels, aoParameters0.y),
        0.5);
    const float rotation =
        InterleavedGradientNoise(position.xy) * 6.28318530718;
    float obscurance = 0.0;
    [unroll]
    for (uint index = 0; index < 8; ++index)
    {
        const float sampleIndex = float(index) + 0.5;
        const float sampleAngle =
            rotation + sampleIndex * 2.39996322973;
        const float sampleRadius = sqrt(sampleIndex / 8.0);
        const float2 sampleDirection = float2(
            cos(sampleAngle),
            sin(sampleAngle));
        const float2 sampleUv = texcoord +
            sampleDirection * sampleRadius * radiusPixels * texel;
        const float sampleDepth = DecodeDepth(
            packedDepthTexture.SampleLevel(
                pointSampler,
                saturate(sampleUv),
                0.0));
        if (sampleDepth >= 0.9999)
            continue;
        const float3 samplePosition =
            ReconstructViewPosition(sampleUv, sampleDepth);
        const float3 offset = samplePosition - center;
        const float distanceToSample = length(offset);
        const float horizon = saturate(
            dot(normal, offset / max(distanceToSample, 0.0001)) -
            aoParameters0.w);
        const float rangeWeight = saturate(
            1.0 - distanceToSample / max(viewRadius, 0.0001));
        obscurance += horizon * rangeWeight;
    }
    const float ao = saturate(
        1.0 - obscurance * (aoParameters1.x / 8.0));
    const float resolutionFade = smoothstep(
        0.5,
        aoParameters0.z,
        projectedRadiusPixels);
    return lerp(1.0, pow(ao, aoParameters1.y), resolutionFade);
}
)";

constexpr char kDenoisePixelShader[] = R"(
Texture2D aoTexture : register(t0);
Texture2D packedDepthTexture : register(t1);
SamplerState pointSampler : register(s0);

cbuffer Configuration : register(b0)
{
    float4 projection0;
    float4 projection1;
    float4 aoParameters0;
    float4 aoParameters1;
};

float DecodeDepth(float4 packed)
{
    return dot(packed.rgb, float3(1.0, 1.0 / 255.0, 1.0 / 65025.0));
}

float ViewZ(float depth)
{
    const float denominator = depth - projection1.x;
    return projection1.y / (
        abs(denominator) < 0.0000001
            ? (denominator < 0.0 ? -0.0000001 : 0.0000001)
            : denominator);
}

float main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target
{
    uint aoWidth;
    uint aoHeight;
    aoTexture.GetDimensions(aoWidth, aoHeight);
    const float2 aoTexel = 1.0 / float2(aoWidth, aoHeight);
    const float centerDepth = DecodeDepth(
        packedDepthTexture.SampleLevel(pointSampler, texcoord, 0.0));
    if (centerDepth >= 0.9999)
        return 1.0;
    const float centerZ = ViewZ(centerDepth);

    float weightedAo = 0.0;
    float totalWeight = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const float2 sampleUv = saturate(
                texcoord + float2(x, y) * aoTexel);
            const float sampleAo = aoTexture.SampleLevel(
                pointSampler, sampleUv, 0.0).r;
            const float sampleDepth = DecodeDepth(
                packedDepthTexture.SampleLevel(
                    pointSampler, sampleUv, 0.0));
            const float sampleZ = ViewZ(sampleDepth);
            const float spatialWeight = (x == 0 && y == 0)
                ? 1.0
                : (x == 0 || y == 0) ? 0.65 : 0.35;
            const float depthScale = max(abs(centerZ) * 0.02, 0.05);
            const float depthWeight = exp2(
                -abs(sampleZ - centerZ) * aoParameters1.z / depthScale);
            const float weight = spatialWeight * depthWeight;
            weightedAo += sampleAo * weight;
            totalWeight += weight;
        }
    }
    return weightedAo / max(totalWeight, 0.0001);
}
)";

bool CompileShader(
    const char* source,
    const char* target,
    ID3DBlob** bytecode,
    bfvr::shared::SharedTextureLogCallback logCallback,
    void* logContext)
{
    ID3DBlob* errors = nullptr;
    const HRESULT result = D3DCompile(
        source,
        std::strlen(source),
        "BFVR-D3D11AmbientOcclusion",
        nullptr,
        nullptr,
        "main",
        target,
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0,
        bytecode,
        &errors);
    if (FAILED(result) && logCallback != nullptr)
    {
        wchar_t message[1200] = {};
        if (errors != nullptr && errors->GetBufferPointer() != nullptr)
        {
            swprintf_s(
                message,
                L"D3D11 AO could not compile %S: %S (HRESULT=0x%08lX).",
                target,
                static_cast<const char*>(errors->GetBufferPointer()),
                static_cast<unsigned long>(result));
        }
        else
        {
            swprintf_s(
                message,
                L"D3D11 AO could not compile %S (HRESULT=0x%08lX).",
                target,
                static_cast<unsigned long>(result));
        }
        logCallback(logContext, message);
    }
    if (errors != nullptr)
        errors->Release();
    return SUCCEEDED(result) && bytecode != nullptr && *bytecode != nullptr;
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
D3D11AmbientOcclusion::~D3D11AmbientOcclusion()
{
    Shutdown();
}

bool D3D11AmbientOcclusion::Initialize(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    SharedTextureLogCallback logCallback,
    void* logContext)
{
    Shutdown();
    logCallback_ = logCallback;
    logContext_ = logContext;
    if (device == nullptr || context == nullptr)
        return false;

    device_ = device;
    device_->AddRef();
    context_ = context;
    context_->AddRef();

    ID3DBlob* bytecode = nullptr;
    HRESULT result = E_FAIL;
    if (CompileShader(
            kFullscreenVertexShader,
            "vs_4_0",
            &bytecode,
            logCallback_,
            logContext_))
    {
        result = device_->CreateVertexShader(
            bytecode->GetBufferPointer(),
            bytecode->GetBufferSize(),
            nullptr,
            &vertexShader_);
    }
    ReleaseInterface(bytecode);
    if (SUCCEEDED(result) && CompileShader(
            kEvaluatePixelShader,
            "ps_4_0",
            &bytecode,
            logCallback_,
            logContext_))
    {
        result = device_->CreatePixelShader(
            bytecode->GetBufferPointer(),
            bytecode->GetBufferSize(),
            nullptr,
            &evaluateShader_);
    }
    ReleaseInterface(bytecode);
    if (SUCCEEDED(result) && CompileShader(
            kDenoisePixelShader,
            "ps_4_0",
            &bytecode,
            logCallback_,
            logContext_))
    {
        result = device_->CreatePixelShader(
            bytecode->GetBufferPointer(),
            bytecode->GetBufferSize(),
            nullptr,
            &denoiseShader_);
    }
    ReleaseInterface(bytecode);

    D3D11_BUFFER_DESC bufferDescription = {};
    bufferDescription.ByteWidth = sizeof(float) * 16;
    bufferDescription.Usage = D3D11_USAGE_DEFAULT;
    bufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    result = SUCCEEDED(result)
        ? device_->CreateBuffer(
            &bufferDescription,
            nullptr,
            &configurationBuffer_)
        : result;

    D3D11_SAMPLER_DESC samplerDescription = {};
    samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
    result = SUCCEEDED(result)
        ? device_->CreateSamplerState(
            &samplerDescription,
            &pointSampler_)
        : result;

    D3D11_QUERY_DESC disjointDescription = {};
    disjointDescription.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
    result = SUCCEEDED(result)
        ? device_->CreateQuery(&disjointDescription, &disjointQuery_)
        : result;
    D3D11_QUERY_DESC timestampDescription = {};
    timestampDescription.Query = D3D11_QUERY_TIMESTAMP;
    result = SUCCEEDED(result)
        ? device_->CreateQuery(
            &timestampDescription,
            &applicationTimestampStart_)
        : result;
    result = SUCCEEDED(result)
        ? device_->CreateQuery(
            &timestampDescription,
            &applicationTimestampEnd_)
        : result;
    for (EyeResources& eye : eyes_)
    {
        result = SUCCEEDED(result)
            ? device_->CreateQuery(
                &timestampDescription,
                &eye.timestampStart)
            : result;
        result = SUCCEEDED(result)
            ? device_->CreateQuery(
                &timestampDescription,
                &eye.timestampEnd)
            : result;
    }

    if (FAILED(result) || vertexShader_ == nullptr ||
        evaluateShader_ == nullptr || denoiseShader_ == nullptr ||
        configurationBuffer_ == nullptr || pointSampler_ == nullptr ||
        disjointQuery_ == nullptr || applicationTimestampStart_ == nullptr ||
        applicationTimestampEnd_ == nullptr)
    {
        WriteLog(
            L"D3D11 AO initialization failed (HRESULT=0x%08lX); AO remains disabled.",
            static_cast<unsigned long>(result));
        Shutdown();
        return false;
    }
    WriteLog(
        L"D3D11 AO initialized: native-resolution per-pixel-rotated 8-sample view-space disk plus 3x3 bilateral denoise; radius=0.60 m, C1-continuous 24..72-pixel transition into a 48-pixel cap, subpixel-footprint fade only, temporal history is disabled.");
    return true;
}

bool D3D11AmbientOcclusion::BeginFrame()
{
    if (context_ == nullptr || disjointQuery_ == nullptr || frameTimingActive_)
        return false;
    context_->Begin(disjointQuery_);
    frameEyesBuilt_ = {};
    frameApplicationTimingActive_ = false;
    frameApplicationTimed_ = false;
    frameTimingActive_ = true;
    return true;
}

bool D3D11AmbientOcclusion::BuildEye(
    std::size_t eyeIndex,
    ID3D11ShaderResourceView* packedDepth,
    UINT depthWidth,
    UINT depthHeight,
    const float projection[16])
{
    if (context_ == nullptr || eyeIndex >= eyes_.size() ||
        packedDepth == nullptr || projection == nullptr ||
        !frameTimingActive_ ||
        !EnsureEyeResources(eyes_[eyeIndex], depthWidth, depthHeight) ||
        !UpdateConfiguration(depthWidth, depthHeight, projection))
    {
        return false;
    }

    EyeResources& eye = eyes_[eyeIndex];
    context_->End(eye.timestampStart);
    const D3D11_VIEWPORT viewport = {
        0.0F,
        0.0F,
        static_cast<float>(eye.width),
        static_cast<float>(eye.height),
        0.0F,
        1.0F};
    const D3D11_RECT scissor = {
        0,
        0,
        static_cast<LONG>(eye.width),
        static_cast<LONG>(eye.height)};
    const float blendFactor[4] = {};
    context_->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFFU);
    context_->OMSetDepthStencilState(nullptr, 0);
    context_->RSSetState(nullptr);
    context_->RSSetScissorRects(1, &scissor);
    context_->RSSetViewports(1, &viewport);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertexShader_, nullptr, 0);
    context_->PSSetConstantBuffers(0, 1, &configurationBuffer_);
    context_->PSSetSamplers(0, 1, &pointSampler_);

    context_->OMSetRenderTargets(1, &eye.targets[0], nullptr);
    context_->PSSetShader(evaluateShader_, nullptr, 0);
    context_->PSSetShaderResources(0, 1, &packedDepth);
    context_->Draw(3, 0);

    ID3D11ShaderResourceView* nullViews[2] = {};
    context_->PSSetShaderResources(0, 2, nullViews);
    context_->OMSetRenderTargets(1, &eye.targets[1], nullptr);
    context_->PSSetShader(denoiseShader_, nullptr, 0);
    ID3D11ShaderResourceView* denoiseInputs[2] = {
        eye.views[0],
        packedDepth};
    context_->PSSetShaderResources(0, 2, denoiseInputs);
    context_->Draw(3, 0);
    context_->PSSetShaderResources(0, 2, nullViews);
    ID3D11RenderTargetView* nullTarget = nullptr;
    context_->OMSetRenderTargets(1, &nullTarget, nullptr);
    context_->End(eye.timestampEnd);
    frameEyesBuilt_[eyeIndex] = true;
    return true;
}

void D3D11AmbientOcclusion::BeginApplicationTiming()
{
    if (context_ != nullptr && frameTimingActive_ &&
        !frameApplicationTimingActive_ && !frameApplicationTimed_)
    {
        context_->End(applicationTimestampStart_);
        frameApplicationTimingActive_ = true;
    }
}

void D3D11AmbientOcclusion::EndApplicationTiming()
{
    if (context_ != nullptr && frameTimingActive_ &&
        frameApplicationTimingActive_)
    {
        context_->End(applicationTimestampEnd_);
        frameApplicationTimingActive_ = false;
        frameApplicationTimed_ = true;
    }
}

void D3D11AmbientOcclusion::EndFrame()
{
    if (context_ != nullptr && disjointQuery_ != nullptr && frameTimingActive_)
    {
        EndApplicationTiming();
        context_->End(disjointQuery_);
    }
}

void D3D11AmbientOcclusion::CollectFrameTimings()
{
    if (context_ == nullptr || !frameTimingActive_)
        return;
    frameTimingActive_ = false;

    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint = {};
    if (context_->GetData(
            disjointQuery_,
            &disjoint,
            sizeof(disjoint),
            0) != S_OK ||
        disjoint.Disjoint || disjoint.Frequency == 0)
    {
        return;
    }
    std::array<double, kDepthTextureCount> eyeMilliseconds = {};
    std::array<bool, kDepthTextureCount> eyeTimingsValid = {};
    for (std::size_t eyeIndex = 0; eyeIndex < eyes_.size(); ++eyeIndex)
    {
        if (!frameEyesBuilt_[eyeIndex])
            continue;
        UINT64 started = 0;
        UINT64 ended = 0;
        if (context_->GetData(
                eyes_[eyeIndex].timestampStart,
                &started,
                sizeof(started),
                0) == S_OK &&
            context_->GetData(
                eyes_[eyeIndex].timestampEnd,
                &ended,
                sizeof(ended),
                0) == S_OK &&
            ended >= started && gpuMilliseconds_[eyeIndex].size() < 8192)
        {
            eyeMilliseconds[eyeIndex] =
                static_cast<double>(ended - started) * 1000.0 /
                static_cast<double>(disjoint.Frequency);
            eyeTimingsValid[eyeIndex] = true;
            gpuMilliseconds_[eyeIndex].push_back(eyeMilliseconds[eyeIndex]);
        }
    }
    const bool stereoValid = eyeTimingsValid[0] && eyeTimingsValid[1];
    const double stereoMilliseconds =
        eyeMilliseconds[0] + eyeMilliseconds[1];
    if (stereoValid && stereoGpuMilliseconds_.size() < 8192)
        stereoGpuMilliseconds_.push_back(stereoMilliseconds);

    UINT64 applicationStarted = 0;
    UINT64 applicationEnded = 0;
    const bool applicationValid = frameApplicationTimed_ &&
        context_->GetData(
            applicationTimestampStart_,
            &applicationStarted,
            sizeof(applicationStarted),
            0) == S_OK &&
        context_->GetData(
            applicationTimestampEnd_,
            &applicationEnded,
            sizeof(applicationEnded),
            0) == S_OK &&
        applicationEnded >= applicationStarted;
    const double applicationMilliseconds = applicationValid
        ? static_cast<double>(applicationEnded - applicationStarted) * 1000.0 /
            static_cast<double>(disjoint.Frequency)
        : 0.0;
    if (applicationValid && applicationGpuMilliseconds_.size() < 8192)
        applicationGpuMilliseconds_.push_back(applicationMilliseconds);
    if (stereoValid && applicationValid &&
        completeGpuMilliseconds_.size() < 8192)
    {
        completeGpuMilliseconds_.push_back(
            stereoMilliseconds + applicationMilliseconds);
    }
}

ID3D11ShaderResourceView* D3D11AmbientOcclusion::GetEyeView(
    std::size_t eye) const noexcept
{
    return eye < eyes_.size() ? eyes_[eye].views[1] : nullptr;
}

bool D3D11AmbientOcclusion::EnsureEyeResources(
    EyeResources& resources,
    UINT depthWidth,
    UINT depthHeight)
{
    // Downsampled AO requires a depth-aware bilateral upsample. The former
    // implementation instead stretched the half-resolution result through the
    // color scaler's ordinary linear sampler, exposing sampling structure as
    // view-locked bands. Match the source depth exactly until a true bilateral
    // upsample path exists.
    const UINT requiredWidth = depthWidth;
    const UINT requiredHeight = depthHeight;
    if (resources.width == requiredWidth &&
        resources.height == requiredHeight &&
        resources.textures[0] != nullptr && resources.textures[1] != nullptr)
    {
        return true;
    }

    ID3D11Query* const startQuery = resources.timestampStart;
    ID3D11Query* const endQuery = resources.timestampEnd;
    resources.timestampStart = nullptr;
    resources.timestampEnd = nullptr;
    ReleaseEyeResources(resources);
    resources.timestampStart = startQuery;
    resources.timestampEnd = endQuery;

    D3D11_TEXTURE2D_DESC description = {};
    description.Width = requiredWidth;
    description.Height = requiredHeight;
    description.MipLevels = 1;
    description.ArraySize = 1;
    // Use a floating-point AO target for the first live contouring diagnostic.
    // The prior R8 target exposed only 256 obscurance levels before the final
    // world composite; all sampling/radius behavior remains unchanged so the
    // owner headset comparison isolates AO-result quantization.
    description.Format = DXGI_FORMAT_R16_FLOAT;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags =
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    HRESULT result = S_OK;
    for (std::size_t index = 0; index < 2 && SUCCEEDED(result); ++index)
    {
        result = device_->CreateTexture2D(
            &description,
            nullptr,
            &resources.textures[index]);
        result = SUCCEEDED(result)
            ? device_->CreateShaderResourceView(
                resources.textures[index],
                nullptr,
                &resources.views[index])
            : result;
        result = SUCCEEDED(result)
            ? device_->CreateRenderTargetView(
                resources.textures[index],
                nullptr,
                &resources.targets[index])
            : result;
    }
    if (FAILED(result))
    {
        WriteLog(
            L"D3D11 AO could not allocate %ux%u eye resources (HRESULT=0x%08lX).",
            requiredWidth,
            requiredHeight,
            static_cast<unsigned long>(result));
        resources.timestampStart = nullptr;
        resources.timestampEnd = nullptr;
        ReleaseEyeResources(resources);
        resources.timestampStart = startQuery;
        resources.timestampEnd = endQuery;
        return false;
    }
    resources.width = requiredWidth;
    resources.height = requiredHeight;
    WriteLog(
        L"D3D11 AO allocated two native-resolution R16_FLOAT targets for one eye at %ux%u.",
        requiredWidth,
        requiredHeight);
    return true;
}

bool D3D11AmbientOcclusion::UpdateConfiguration(
    UINT depthWidth,
    UINT depthHeight,
    const float projection[16])
{
    constexpr std::array<std::size_t, 6> requiredIndices = {0, 5, 8, 9, 10, 14};
    for (const std::size_t index : requiredIndices)
    {
        if (!std::isfinite(projection[index]))
            return false;
    }
    if (std::fabs(projection[0]) < 0.0001F ||
        std::fabs(projection[5]) < 0.0001F ||
        std::fabs(projection[14]) < 0.0000001F ||
        depthWidth == 0 || depthHeight == 0)
    {
        return false;
    }

    const float configuration[16] = {
        projection[0], projection[5], projection[8], projection[9],
        projection[10], projection[14],
        1.0F / static_cast<float>(depthWidth),
        1.0F / static_cast<float>(depthHeight),
        0.60F, 48.0F, 2.0F, 0.04F,
        1.65F, 1.15F, 32.0F, 0.0F};
    context_->UpdateSubresource(
        configurationBuffer_,
        0,
        nullptr,
        configuration,
        0,
        0);
    return true;
}

void D3D11AmbientOcclusion::ReleaseEyeResources(EyeResources& resources)
{
    for (std::size_t index = 0; index < 2; ++index)
    {
        ReleaseInterface(resources.targets[index]);
        ReleaseInterface(resources.views[index]);
        ReleaseInterface(resources.textures[index]);
    }
    ReleaseInterface(resources.timestampEnd);
    ReleaseInterface(resources.timestampStart);
    resources.width = 0;
    resources.height = 0;
}

void D3D11AmbientOcclusion::ReportTimings()
{
    if (timingsReported_)
        return;
    timingsReported_ = true;
    for (std::size_t eye = 0; eye < gpuMilliseconds_.size(); ++eye)
    {
        if (gpuMilliseconds_[eye].empty())
            continue;
        std::vector<double> sorted = gpuMilliseconds_[eye];
        std::sort(sorted.begin(), sorted.end());
        const std::size_t p95Index = static_cast<std::size_t>(
            std::ceil(static_cast<double>(sorted.size()) * 0.95)) - 1;
        WriteLog(
            L"D3D11 AO GPU summary eye=%zu samples=%zu median=%.4f ms p95=%.4f ms max=%.4f ms.",
            eye,
            sorted.size(),
            sorted[sorted.size() / 2],
            sorted[(std::min)(p95Index, sorted.size() - 1)],
            sorted.back());
    }
    auto reportSamples = [this](
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
            L"D3D11 AO GPU summary %s samples=%zu median=%.4f ms p95=%.4f ms max=%.4f ms.",
            label,
            sorted.size(),
            sorted[sorted.size() / 2],
            sorted[(std::min)(p95Index, sorted.size() - 1)],
            sorted.back());
    };
    reportSamples(L"stereo evaluation+denoise", stereoGpuMilliseconds_);
    reportSamples(L"AO-applied stereo world conversion", applicationGpuMilliseconds_);
    reportSamples(L"evaluation+denoise+world conversion", completeGpuMilliseconds_);
}

void D3D11AmbientOcclusion::Shutdown()
{
    ReportTimings();
    frameTimingActive_ = false;
    frameApplicationTimingActive_ = false;
    frameApplicationTimed_ = false;
    for (EyeResources& eye : eyes_)
        ReleaseEyeResources(eye);
    ReleaseInterface(disjointQuery_);
    ReleaseInterface(applicationTimestampEnd_);
    ReleaseInterface(applicationTimestampStart_);
    ReleaseInterface(pointSampler_);
    ReleaseInterface(configurationBuffer_);
    ReleaseInterface(denoiseShader_);
    ReleaseInterface(evaluateShader_);
    ReleaseInterface(vertexShader_);
    ReleaseInterface(context_);
    ReleaseInterface(device_);
    gpuMilliseconds_ = {};
    stereoGpuMilliseconds_.clear();
    applicationGpuMilliseconds_.clear();
    completeGpuMilliseconds_.clear();
    frameEyesBuilt_ = {};
    timingsReported_ = false;
    logCallback_ = nullptr;
    logContext_ = nullptr;
}

void D3D11AmbientOcclusion::WriteLog(const wchar_t* format, ...) const
{
    if (logCallback_ == nullptr)
        return;
    wchar_t message[1200] = {};
    va_list arguments;
    va_start(arguments, format);
    _vsnwprintf_s(message, std::size(message), _TRUNCATE, format, arguments);
    va_end(arguments);
    logCallback_(logContext_, message);
}
} // namespace bfvr::shared
