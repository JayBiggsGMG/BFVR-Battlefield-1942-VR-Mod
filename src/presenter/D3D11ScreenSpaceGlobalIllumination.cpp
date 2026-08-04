#include "presenter/D3D11ScreenSpaceGlobalIllumination.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>

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

constexpr char kShaderCommon[] = R"(
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

float3 ReconstructViewPositionFromZ(float2 uv, float viewZ)
{
    const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    return float3(
        viewZ * (ndc.x - projection0.z) / projection0.x,
        viewZ * (ndc.y - projection0.w) / projection0.y,
        viewZ);
}

float3 ReconstructViewPosition(float2 uv, float depth)
{
    return ReconstructViewPositionFromZ(uv, ViewZ(depth));
}
)";

constexpr char kPreparePixelShaderPrefix[] = R"(
Texture2D packedDepthTexture : register(t0);
SamplerState pointSampler : register(s0);

cbuffer Configuration : register(b0)
{
    float4 projection0; // m00, m11, m20, m21
    float4 projection1; // m22, m32, 1/fullWidth, 1/fullHeight
    float4 ssgiParameters0; // radius, bounceScale, edgeFade, sourceIsLinear
    float4 ssgiParameters1; // 1/evaluationWidth, 1/evaluationHeight, depthSharpness, normalPower
};
)";

constexpr char kPreparePixelShaderBody[] = R"(
float3 ReadPosition(float2 uv)
{
    const float depth = DecodeDepth(
        packedDepthTexture.SampleLevel(pointSampler, saturate(uv), 0.0));
    return ReconstructViewPosition(uv, depth);
}

float4 main(float4 screenPosition : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    const float centerDepth = DecodeDepth(
        packedDepthTexture.SampleLevel(pointSampler, uv, 0.0));
    if (centerDepth >= 0.9999)
        return 0.0;

    const float2 texel = projection1.zw;
    const float3 center = ReconstructViewPosition(uv, centerDepth);
    const float3 left = ReadPosition(uv - float2(texel.x, 0.0));
    const float3 right = ReadPosition(uv + float2(texel.x, 0.0));
    const float3 up = ReadPosition(uv - float2(0.0, texel.y));
    const float3 down = ReadPosition(uv + float2(0.0, texel.y));
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
    // Native eye targets can make the cross product of two one-pixel view
    // derivatives very small even for a perfectly valid plane. Test close to
    // float precision instead of rejecting resolution-dependent normals.
    if (normalLengthSquared <= 0.0000000000000001 || center.z <= 0.0)
        return 0.0;
    float3 normal = unnormalizedNormal * rsqrt(normalLengthSquared);
    const float3 surfaceToCamera = -normalize(center);
    if (dot(normal, surfaceToCamera) < 0.0)
        normal = -normal;
    return float4(normal, center.z);
}
)";

constexpr char kEvaluatePixelShaderPrefix[] = R"(
Texture2D guideTexture : register(t0);
Texture2D worldColorTexture : register(t1);
SamplerState pointSampler : register(s0);
SamplerState linearSampler : register(s1);

cbuffer Configuration : register(b0)
{
    float4 projection0;
    float4 projection1;
    float4 ssgiParameters0;
    float4 ssgiParameters1;
};
)";

constexpr char kEvaluatePixelShaderBody[] = R"(
float3 SrgbToLinear(float3 color)
{
    const float3 low = color / 12.92;
    const float3 high = pow((color + 0.055) / 1.055, 2.4);
    return lerp(high, low, step(color, 0.04045));
}

float InterleavedGradientNoise(float2 pixel)
{
    return frac(52.9829189 * frac(
        dot(pixel, float2(0.06711056, 0.00583715))));
}

float SmoothLimitRadius(float radiusPixels, float maximumPixels)
{
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

float4 main(float4 screenPosition : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    const float4 centerGuide = guideTexture.SampleLevel(
        pointSampler, uv, 0.0);
    if (centerGuide.w <= 0.0)
        return 0.0;

    const float3 center = ReconstructViewPositionFromZ(uv, centerGuide.w);
    const float3 centerNormal = normalize(centerGuide.xyz);
    const float3 centerEncoded = worldColorTexture.SampleLevel(
        linearSampler, uv, 0.0).rgb;
    const float3 centerRadiance = ssgiParameters0.w > 0.5
        ? centerEncoded
        : SrgbToLinear(centerEncoded);
    const float viewRadius = ssgiParameters0.x;
    const float2 projectedRadiusPixels = viewRadius *
        abs(projection0.xy) * 0.5 /
        max(center.z * projection1.zw, 0.0001);
    const float maximumProjectedRadius = max(
        projectedRadiusPixels.x,
        projectedRadiusPixels.y);
    const float limitedRadius = max(
        SmoothLimitRadius(maximumProjectedRadius, 320.0),
        1.5);
    const float2 radiusUv = projectedRadiusPixels *
        (limitedRadius / max(maximumProjectedRadius, 0.0001)) *
        projection1.zw;
    const float rotation =
        InterleavedGradientNoise(screenPosition.xy) * 6.28318530718;
    float3 bouncedRadiance = 0.0;

    [unroll]
    for (uint index = 0; index < 4; ++index)
    {
        const float sampleIndex = float(index) + 0.5;
        const float angle = rotation + sampleIndex * 2.39996322973;
        const float sampleRadius = sqrt(sampleIndex / 4.0);
        const float2 direction = float2(cos(angle), sin(angle));
        const float2 sampleUv = uv + direction * sampleRadius * radiusUv;
        if (any(sampleUv <= 0.001) || any(sampleUv >= 0.999))
            continue;

        const float4 sampleGuide = guideTexture.SampleLevel(
            pointSampler, sampleUv, 0.0);
        if (sampleGuide.w <= 0.0)
            continue;
        const float3 samplePosition = ReconstructViewPositionFromZ(
            sampleUv, sampleGuide.w);
        const float3 offset = samplePosition - center;
        const float distanceToSample = length(offset);
        if (distanceToSample <= 0.01 || distanceToSample >= viewRadius)
            continue;

        const float3 sampleDirection = offset / distanceToSample;
        const float receiverCosine = saturate(
            dot(centerNormal, sampleDirection));
        const float emitterCosine = saturate(
            dot(normalize(sampleGuide.xyz), -sampleDirection));
        const float normalDifference = saturate(
            1.0 - dot(centerNormal, normalize(sampleGuide.xyz)));
        // A point-sampled endpoint has zero solid angle on a coplanar surface
        // and made real BF1942 output contact-only. The finite-patch term
        // spreads only brighter-neighbour contrast, so a flat uniformly
        // coloured control remains unchanged while colour/light differences
        // reach broader receivers. Directional two-surface bounce remains.
        const float finitePatchFactor =
            0.05 + 0.15 * normalDifference;
        float rangeWeight = saturate(
            1.0 - distanceToSample / viewRadius);
        rangeWeight *= rangeWeight;
        const float3 encoded = worldColorTexture.SampleLevel(
            linearSampler, sampleUv, 0.0).rgb;
        const float3 sourceRadiance = ssgiParameters0.w > 0.5
            ? encoded
            : SrgbToLinear(encoded);
        const float directionalFactor = receiverCosine * emitterCosine;
        const float3 brighterNeighbour = max(
            sourceRadiance - centerRadiance,
            0.0);
        bouncedRadiance += (
            min(sourceRadiance, 1.5) * directionalFactor +
            min(brighterNeighbour, 1.5) * finitePatchFactor) *
            rangeWeight;
    }

    const float edgeDistance = min(
        min(uv.x, 1.0 - uv.x),
        min(uv.y, 1.0 - uv.y));
    const float edgeConfidence = smoothstep(
        0.0, ssgiParameters0.z, edgeDistance);
    const float resolutionConfidence = smoothstep(
        0.75, 2.0, maximumProjectedRadius);
    return float4(
        bouncedRadiance * (ssgiParameters0.y / 4.0) *
            edgeConfidence * resolutionConfidence,
        1.0);
}
)";

constexpr char kDenoisePixelShader[] = R"(
Texture2D radianceTexture : register(t0);
Texture2D guideTexture : register(t1);
SamplerState pointSampler : register(s0);

cbuffer Configuration : register(b0)
{
    float4 projection0;
    float4 projection1;
    float4 ssgiParameters0;
    float4 ssgiParameters1;
};

float4 main(float4 screenPosition : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    const float4 centerGuide = guideTexture.SampleLevel(
        pointSampler, uv, 0.0);
    if (centerGuide.w <= 0.0)
        return 0.0;
    const float3 centerNormal = normalize(centerGuide.xyz);
    const float2 texel = ssgiParameters1.xy;
    float3 weightedRadiance = 0.0;
    float totalWeight = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const float2 sampleUv = saturate(uv + float2(x, y) * texel);
            const float4 sampleGuide = guideTexture.SampleLevel(
                pointSampler, sampleUv, 0.0);
            if (sampleGuide.w <= 0.0)
                continue;
            const float spatialWeight = x == 0 && y == 0
                ? 1.0
                : (x == 0 || y == 0) ? 0.65 : 0.35;
            const float relativeDepthDifference =
                abs(sampleGuide.w - centerGuide.w) /
                max(abs(centerGuide.w), 0.0001);
            const float depthWeight = exp2(
                -relativeDepthDifference * ssgiParameters1.z);
            const float normalWeight = pow(
                saturate(dot(centerNormal, normalize(sampleGuide.xyz))),
                ssgiParameters1.w);
            const float weight = spatialWeight * depthWeight * normalWeight;
            weightedRadiance += radianceTexture.SampleLevel(
                pointSampler, sampleUv, 0.0).rgb * weight;
            totalWeight += weight;
        }
    }
    return float4(
        weightedRadiance / max(totalWeight, 0.0001),
        totalWeight > 0.0001 ? 1.0 : 0.0);
}
)";

bool CompileShader(
    const std::string& source,
    const char* target,
    ID3DBlob** bytecode,
    bfvr::shared::SharedTextureLogCallback logCallback,
    void* logContext)
{
    ID3DBlob* errors = nullptr;
    const HRESULT result = D3DCompile(
        source.data(),
        source.size(),
        "BFVR-D3D11ScreenSpaceGlobalIllumination",
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
        wchar_t message[1400] = {};
        swprintf_s(
            message,
            L"D3D11 SSGI could not compile %S: %S (HRESULT=0x%08lX).",
            target,
            errors != nullptr && errors->GetBufferPointer() != nullptr
                ? static_cast<const char*>(errors->GetBufferPointer())
                : "no compiler diagnostic",
            static_cast<unsigned long>(result));
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
D3D11ScreenSpaceGlobalIllumination::~D3D11ScreenSpaceGlobalIllumination()
{
    Shutdown();
}

bool D3D11ScreenSpaceGlobalIllumination::Initialize(
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

    const std::array<std::string, 4> sources = {
        std::string(kFullscreenVertexShader),
        std::string(kPreparePixelShaderPrefix) + kShaderCommon +
            kPreparePixelShaderBody,
        std::string(kEvaluatePixelShaderPrefix) + kShaderCommon +
            kEvaluatePixelShaderBody,
        std::string(kDenoisePixelShader)};
    ID3DBlob* bytecode = nullptr;
    HRESULT result = E_FAIL;
    if (CompileShader(
            sources[0], "vs_4_0", &bytecode, logCallback_, logContext_))
    {
        result = device_->CreateVertexShader(
            bytecode->GetBufferPointer(),
            bytecode->GetBufferSize(),
            nullptr,
            &vertexShader_);
    }
    ReleaseInterface(bytecode);
    const std::array<ID3D11PixelShader**, 3> pixelShaders = {
        &prepareShader_, &evaluateShader_, &denoiseShader_};
    for (std::size_t index = 0;
         index < pixelShaders.size() && SUCCEEDED(result);
         ++index)
    {
        if (!CompileShader(
                sources[index + 1],
                "ps_4_0",
                &bytecode,
                logCallback_,
                logContext_))
        {
            result = E_FAIL;
            break;
        }
        result = device_->CreatePixelShader(
            bytecode->GetBufferPointer(),
            bytecode->GetBufferSize(),
            nullptr,
            pixelShaders[index]);
        ReleaseInterface(bytecode);
    }
    ReleaseInterface(bytecode);

    D3D11_BUFFER_DESC bufferDescription = {};
    bufferDescription.ByteWidth = sizeof(float) * 16;
    bufferDescription.Usage = D3D11_USAGE_DEFAULT;
    bufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    result = SUCCEEDED(result)
        ? device_->CreateBuffer(
            &bufferDescription, nullptr, &configurationBuffer_)
        : result;

    D3D11_SAMPLER_DESC samplerDescription = {};
    samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
    result = SUCCEEDED(result)
        ? device_->CreateSamplerState(&samplerDescription, &pointSampler_)
        : result;
    samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    result = SUCCEEDED(result)
        ? device_->CreateSamplerState(&samplerDescription, &linearSampler_)
        : result;

    D3D11_QUERY_DESC queryDescription = {};
    queryDescription.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
    result = SUCCEEDED(result)
        ? device_->CreateQuery(&queryDescription, &disjointQuery_)
        : result;
    queryDescription.Query = D3D11_QUERY_TIMESTAMP;
    for (EyeResources& eye : eyes_)
    {
        result = SUCCEEDED(result)
            ? device_->CreateQuery(&queryDescription, &eye.timestampStart)
            : result;
        result = SUCCEEDED(result)
            ? device_->CreateQuery(&queryDescription, &eye.timestampEnd)
            : result;
    }

    if (FAILED(result) || vertexShader_ == nullptr ||
        prepareShader_ == nullptr || evaluateShader_ == nullptr ||
        denoiseShader_ == nullptr ||
        configurationBuffer_ == nullptr || pointSampler_ == nullptr ||
        linearSampler_ == nullptr || disjointQuery_ == nullptr)
    {
        WriteLog(
            L"D3D11 SSGI initialization failed (HRESULT=0x%08lX); SSGI remains disabled.",
            static_cast<unsigned long>(result));
        Shutdown();
        return false;
    }

    WriteLog(
        L"D3D11 SSGI initialized: per-eye native-resolution 4-sample directional-plus-contrast finite-patch radiosity, three 3x3 depth/normal-aware denoise passes, 4.0 m radius, a C1-continuous 320-pixel safety limit, and no temporal history.");
    return true;
}

bool D3D11ScreenSpaceGlobalIllumination::BeginFrame()
{
    if (context_ == nullptr || disjointQuery_ == nullptr || frameTimingActive_)
        return false;
    context_->Begin(disjointQuery_);
    frameEyesBuilt_ = {};
    frameTimingActive_ = true;
    return true;
}

bool D3D11ScreenSpaceGlobalIllumination::BuildEye(
    std::size_t eyeIndex,
    ID3D11ShaderResourceView* worldColor,
    bool worldColorAlreadyLinear,
    ID3D11ShaderResourceView* packedDepth,
    UINT depthWidth,
    UINT depthHeight,
    const float projection[16])
{
    if (context_ == nullptr || eyeIndex >= eyes_.size() ||
        worldColor == nullptr || packedDepth == nullptr || projection == nullptr ||
        !frameTimingActive_ ||
        !EnsureEyeResources(eyes_[eyeIndex], depthWidth, depthHeight))
    {
        return false;
    }

    EyeResources& eye = eyes_[eyeIndex];
    if (!UpdateConfiguration(
            depthWidth,
            depthHeight,
            eye.evaluationWidth,
            eye.evaluationHeight,
            worldColorAlreadyLinear,
            projection))
    {
        return false;
    }

    const float blendFactor[4] = {};
    context_->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFFU);
    context_->OMSetDepthStencilState(nullptr, 0);
    context_->RSSetState(nullptr);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertexShader_, nullptr, 0);
    context_->PSSetConstantBuffers(0, 1, &configurationBuffer_);
    ID3D11SamplerState* samplers[2] = {pointSampler_, linearSampler_};
    context_->PSSetSamplers(0, 2, samplers);
    context_->End(eye.timestampStart);

    const D3D11_VIEWPORT evaluationViewport = {
        0.0F,
        0.0F,
        static_cast<float>(eye.evaluationWidth),
        static_cast<float>(eye.evaluationHeight),
        0.0F,
        1.0F};
    const D3D11_RECT evaluationScissor = {
        0,
        0,
        static_cast<LONG>(eye.evaluationWidth),
        static_cast<LONG>(eye.evaluationHeight)};
    context_->RSSetViewports(1, &evaluationViewport);
    context_->RSSetScissorRects(1, &evaluationScissor);

    context_->OMSetRenderTargets(1, &eye.guideTarget, nullptr);
    context_->PSSetShader(prepareShader_, nullptr, 0);
    context_->PSSetShaderResources(0, 1, &packedDepth);
    context_->Draw(3, 0);

    ID3D11ShaderResourceView* nullViews[3] = {};
    context_->PSSetShaderResources(0, 3, nullViews);
    context_->OMSetRenderTargets(1, &eye.radianceTargets[0], nullptr);
    context_->PSSetShader(evaluateShader_, nullptr, 0);
    ID3D11ShaderResourceView* evaluateInputs[2] = {
        eye.guideView,
        worldColor};
    context_->PSSetShaderResources(0, 2, evaluateInputs);
    context_->Draw(3, 0);

    context_->PSSetShader(denoiseShader_, nullptr, 0);
    for (std::size_t pass = 0; pass < 3; ++pass)
    {
        const std::size_t inputIndex = pass % 2;
        const std::size_t outputIndex = 1 - inputIndex;
        context_->PSSetShaderResources(0, 3, nullViews);
        context_->OMSetRenderTargets(
            1,
            &eye.radianceTargets[outputIndex],
            nullptr);
        ID3D11ShaderResourceView* denoiseInputs[2] = {
            eye.radianceViews[inputIndex],
            eye.guideView};
        context_->PSSetShaderResources(0, 2, denoiseInputs);
        context_->Draw(3, 0);
    }

    context_->PSSetShaderResources(0, 3, nullViews);
    ID3D11RenderTargetView* nullTarget = nullptr;
    context_->OMSetRenderTargets(1, &nullTarget, nullptr);
    context_->End(eye.timestampEnd);
    frameEyesBuilt_[eyeIndex] = true;
    return true;
}

void D3D11ScreenSpaceGlobalIllumination::EndFrame()
{
    if (context_ != nullptr && disjointQuery_ != nullptr && frameTimingActive_)
        context_->End(disjointQuery_);
}

void D3D11ScreenSpaceGlobalIllumination::CollectFrameTimings()
{
    if (context_ == nullptr || !frameTimingActive_)
        return;
    frameTimingActive_ = false;
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint = {};
    if (context_->GetData(
            disjointQuery_, &disjoint, sizeof(disjoint), 0) != S_OK ||
        disjoint.Disjoint || disjoint.Frequency == 0)
    {
        return;
    }

    std::array<double, kDepthTextureCount> eyeMilliseconds = {};
    std::array<bool, kDepthTextureCount> valid = {};
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
            gpuMilliseconds_[eyeIndex].push_back(eyeMilliseconds[eyeIndex]);
            valid[eyeIndex] = true;
        }
    }
    if (valid[0] && valid[1] && stereoGpuMilliseconds_.size() < 8192)
    {
        stereoGpuMilliseconds_.push_back(
            eyeMilliseconds[0] + eyeMilliseconds[1]);
    }
}

ID3D11ShaderResourceView* D3D11ScreenSpaceGlobalIllumination::GetEyeView(
    std::size_t eye) const noexcept
{
    return eye < eyes_.size() ? eyes_[eye].radianceViews[1] : nullptr;
}

bool D3D11ScreenSpaceGlobalIllumination::EnsureEyeResources(
    EyeResources& resources,
    UINT width,
    UINT height)
{
    // The first live diagnostic reproduced the grouped horizontal bands that
    // BFVR's AO path eliminated by moving its screen-domain evaluation to
    // native resolution. Every SSGI guide/radiance pixel now corresponds
    // one-to-one with a world pixel.
    const UINT evaluationWidth = width;
    const UINT evaluationHeight = height;
    if (resources.width == width && resources.height == height &&
        resources.evaluationWidth == evaluationWidth &&
        resources.evaluationHeight == evaluationHeight &&
        resources.guideTexture != nullptr &&
        resources.radianceTextures[0] != nullptr &&
        resources.radianceTextures[1] != nullptr)
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

    auto createTarget = [this](
                            UINT targetWidth,
                            UINT targetHeight,
                            ID3D11Texture2D** texture,
                            ID3D11ShaderResourceView** view,
                            ID3D11RenderTargetView** target)
    {
        D3D11_TEXTURE2D_DESC description = {};
        description.Width = targetWidth;
        description.Height = targetHeight;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags =
            D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        HRESULT result = device_->CreateTexture2D(
            &description, nullptr, texture);
        result = SUCCEEDED(result)
            ? device_->CreateShaderResourceView(*texture, nullptr, view)
            : result;
        result = SUCCEEDED(result)
            ? device_->CreateRenderTargetView(*texture, nullptr, target)
            : result;
        return result;
    };

    HRESULT result = createTarget(
        evaluationWidth,
        evaluationHeight,
        &resources.guideTexture,
        &resources.guideView,
        &resources.guideTarget);
    for (std::size_t index = 0; index < 2 && SUCCEEDED(result); ++index)
    {
        result = createTarget(
            evaluationWidth,
            evaluationHeight,
            &resources.radianceTextures[index],
            &resources.radianceViews[index],
            &resources.radianceTargets[index]);
    }
    if (FAILED(result))
    {
        WriteLog(
            L"D3D11 SSGI could not allocate %ux%u/%ux%u eye resources (HRESULT=0x%08lX).",
            evaluationWidth,
            evaluationHeight,
            width,
            height,
            static_cast<unsigned long>(result));
        resources.timestampStart = nullptr;
        resources.timestampEnd = nullptr;
        ReleaseEyeResources(resources);
        resources.timestampStart = startQuery;
        resources.timestampEnd = endQuery;
        return false;
    }
    resources.width = width;
    resources.height = height;
    resources.evaluationWidth = evaluationWidth;
    resources.evaluationHeight = evaluationHeight;
    WriteLog(
        L"D3D11 SSGI allocated one native-resolution guide and two native-resolution RGBA16F radiance targets at %ux%u for one eye.",
        evaluationWidth,
        evaluationHeight);
    return true;
}

bool D3D11ScreenSpaceGlobalIllumination::UpdateConfiguration(
    UINT width,
    UINT height,
    UINT evaluationWidth,
    UINT evaluationHeight,
    bool worldColorAlreadyLinear,
    const float projection[16])
{
    constexpr std::array<std::size_t, 6> requiredIndices = {0, 5, 8, 9, 10, 14};
    for (const std::size_t index : requiredIndices)
    {
        if (!std::isfinite(projection[index]))
            return false;
    }
    if (width == 0 || height == 0 ||
        evaluationWidth == 0 || evaluationHeight == 0 ||
        std::fabs(projection[0]) < 0.0001F ||
        std::fabs(projection[5]) < 0.0001F ||
        std::fabs(projection[14]) < 0.0000001F)
    {
        return false;
    }

    const float configuration[16] = {
        projection[0], projection[5], projection[8], projection[9],
        projection[10], projection[14],
        1.0F / static_cast<float>(width),
        1.0F / static_cast<float>(height),
        4.0F, 4.0F, 0.06F, worldColorAlreadyLinear ? 1.0F : 0.0F,
        1.0F / static_cast<float>(evaluationWidth),
        1.0F / static_cast<float>(evaluationHeight),
        96.0F, 8.0F};
    context_->UpdateSubresource(
        configurationBuffer_, 0, nullptr, configuration, 0, 0);
    return true;
}

void D3D11ScreenSpaceGlobalIllumination::ReleaseEyeResources(
    EyeResources& resources)
{
    for (std::size_t index = 0; index < 2; ++index)
    {
        ReleaseInterface(resources.radianceTargets[index]);
        ReleaseInterface(resources.radianceViews[index]);
        ReleaseInterface(resources.radianceTextures[index]);
    }
    ReleaseInterface(resources.guideTarget);
    ReleaseInterface(resources.guideView);
    ReleaseInterface(resources.guideTexture);
    ReleaseInterface(resources.timestampEnd);
    ReleaseInterface(resources.timestampStart);
    resources.width = 0;
    resources.height = 0;
    resources.evaluationWidth = 0;
    resources.evaluationHeight = 0;
}

void D3D11ScreenSpaceGlobalIllumination::ReportTimings()
{
    if (timingsReported_)
        return;
    timingsReported_ = true;
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
            L"D3D11 SSGI GPU summary %s samples=%zu median=%.4f ms p95=%.4f ms max=%.4f ms.",
            label,
            sorted.size(),
            sorted[sorted.size() / 2],
            sorted[(std::min)(p95Index, sorted.size() - 1)],
            sorted.back());
    };
    report(L"left prepare+gather+three-pass-denoise", gpuMilliseconds_[0]);
    report(L"right prepare+gather+three-pass-denoise", gpuMilliseconds_[1]);
    report(
        L"stereo prepare+gather+three-pass-denoise",
        stereoGpuMilliseconds_);
}

void D3D11ScreenSpaceGlobalIllumination::Shutdown()
{
    ReportTimings();
    frameTimingActive_ = false;
    for (EyeResources& eye : eyes_)
        ReleaseEyeResources(eye);
    ReleaseInterface(disjointQuery_);
    ReleaseInterface(linearSampler_);
    ReleaseInterface(pointSampler_);
    ReleaseInterface(configurationBuffer_);
    ReleaseInterface(denoiseShader_);
    ReleaseInterface(evaluateShader_);
    ReleaseInterface(prepareShader_);
    ReleaseInterface(vertexShader_);
    ReleaseInterface(context_);
    ReleaseInterface(device_);
    gpuMilliseconds_ = {};
    stereoGpuMilliseconds_.clear();
    frameEyesBuilt_ = {};
    timingsReported_ = false;
    logCallback_ = nullptr;
    logContext_ = nullptr;
}

void D3D11ScreenSpaceGlobalIllumination::WriteLog(
    const wchar_t* format,
    ...) const
{
    if (logCallback_ == nullptr)
        return;
    wchar_t message[1400] = {};
    va_list arguments;
    va_start(arguments, format);
    _vsnwprintf_s(message, std::size(message), _TRUNCATE, format, arguments);
    va_end(arguments);
    logCallback_(logContext_, message);
}
} // namespace bfvr::shared
