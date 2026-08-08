#include "presenter/D3D11WaterReflection.h"

#include <d3dcompiler.h>

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

constexpr char kWaterReflectionPixelShader[] = R"(
Texture2D worldColorTexture : register(t0);
Texture2D depthMaskTexture : register(t1);
SamplerState linearSampler : register(s0);
SamplerState pointSampler : register(s1);

cbuffer Configuration : register(b0)
{
    float4 projectionA; // m00, m11, m20, m21
    float4 projectionB; // m22, m23, m32, m33
    float4 textureInfo; // 1/width, 1/height, width, height
    float4 materialInfo; // source-is-linear, reserved
};

float DecodeDepth(float3 encoded)
{
    return dot(encoded, float3(1.0, 1.0 / 255.0, 1.0 / 65025.0));
}

float3 ReconstructViewPosition(float2 uv, float depth)
{
    const float denominator = depth * projectionB.y - projectionB.x;
    const float viewZ = abs(denominator) > 0.0000001
        ? (projectionB.z - depth * projectionB.w) / denominator
        : 0.0;
    const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    return float3(
        viewZ * (ndc.x - projectionA.z) / projectionA.x,
        viewZ * (ndc.y - projectionA.w) / projectionA.y,
        viewZ);
}

float2 ProjectViewPosition(float3 position)
{
    const float clipX = position.x * projectionA.x + position.z * projectionA.z;
    const float clipY = position.y * projectionA.y + position.z * projectionA.w;
    const float clipW = position.z * projectionB.y + projectionB.w;
    const float2 ndc = float2(clipX, clipY) / clipW;
    return float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
}

float3 SrgbToLinear(float3 color)
{
    const float3 low = color / 12.92;
    const float3 high = pow((color + 0.055) / 1.055, 2.4);
    return lerp(high, low, step(color, 0.04045));
}

bool ReadWaterPosition(float2 uv, out float3 position)
{
    const float4 packed = depthMaskTexture.SampleLevel(pointSampler, uv, 0.0);
    const float depth = DecodeDepth(packed.rgb);
    position = ReconstructViewPosition(uv, depth);
    return packed.a > (1.0 / 255.0) && depth < 0.99999 && position.z > 0.0;
}

float4 main(float4 screenPosition : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    float3 center;
    if (!ReadWaterPosition(uv, center))
        return 0.0;

    float3 leftPosition;
    float3 rightPosition;
    float3 upPosition;
    float3 downPosition;
    if (!ReadWaterPosition(uv - float2(textureInfo.x, 0.0), leftPosition) ||
        !ReadWaterPosition(uv + float2(textureInfo.x, 0.0), rightPosition) ||
        !ReadWaterPosition(uv - float2(0.0, textureInfo.y), upPosition) ||
        !ReadWaterPosition(uv + float2(0.0, textureInfo.y), downPosition))
    {
        return 0.0;
    }

    const float3 dx = abs(rightPosition.z - center.z) <
            abs(center.z - leftPosition.z)
        ? rightPosition - center
        : center - leftPosition;
    const float3 dy = abs(downPosition.z - center.z) <
            abs(center.z - upPosition.z)
        ? downPosition - center
        : center - upPosition;
    const float3 normalVector = cross(dx, dy);
    const float normalLength = length(normalVector);
    if (normalLength <= 0.000001)
        return 0.0;
    float3 normal = normalVector / normalLength;
    const float3 incident = normalize(center);
    if (dot(normal, -incident) < 0.0)
        normal = -normal;
    const float normalValidity = saturate(normalLength * 1000.0);

    const float3 reflected = normalize(reflect(incident, normal));
    const float originBias = max(0.025, center.z * 0.0005);
    const float3 origin = center + normal * originBias;
    float stepLength = max(0.08, center.z * 0.006);
    float travel = stepLength * 2.0;
    float previousDryTravel = 0.0;
    float previousDrySeparation = 0.0;
    bool previousDrySample = false;
    float2 hitUv = 0.0;
    float hitTravel = 0.0;
    bool hit = false;

    [loop]
    for (uint stepIndex = 0; stepIndex < 24; ++stepIndex)
    {
        const float3 rayPosition = origin + reflected * travel;
        if (rayPosition.z <= 0.001)
            break;
        const float2 rayUv = ProjectViewPosition(rayPosition);
        if (any(rayUv <= 0.001) || any(rayUv >= 0.999))
            break;

        const float4 scenePacked = depthMaskTexture.SampleLevel(
            pointSampler, rayUv, 0.0);
        const float sceneDepth = DecodeDepth(scenePacked.rgb);
        if (sceneDepth < 0.99999 && scenePacked.a <= (1.0 / 255.0))
        {
            const float3 scenePosition = ReconstructViewPosition(
                rayUv, sceneDepth);
            const float separation = rayPosition.z - scenePosition.z;
            if (separation >= 0.0)
            {
                float candidateTravel = travel;
                float2 candidateUv = rayUv;
                float3 candidateScenePosition = scenePosition;
                float candidateSeparation = separation;

                // A coarse ray step can cross the scene surface completely.
                // The former single-sample thickness test then accepted only
                // certain screen rows, creating horizontal stipple bands.
                // Refine every continuous front-to-back depth crossing before
                // applying the deliberately conservative thickness rejection.
                if (previousDrySample && previousDrySeparation < 0.0)
                {
                    float lowerTravel = previousDryTravel;
                    float upperTravel = travel;
                    [unroll]
                    for (uint refinement = 0; refinement < 6; ++refinement)
                    {
                        const float midpointTravel =
                            (lowerTravel + upperTravel) * 0.5;
                        const float3 midpointRay =
                            origin + reflected * midpointTravel;
                        const float2 midpointUv =
                            ProjectViewPosition(midpointRay);
                        const float4 midpointPacked =
                            depthMaskTexture.SampleLevel(
                                pointSampler, midpointUv, 0.0);
                        const float midpointDepth =
                            DecodeDepth(midpointPacked.rgb);
                        if (any(midpointUv <= 0.001) ||
                            any(midpointUv >= 0.999) ||
                            midpointDepth >= 0.99999 ||
                            midpointPacked.a > (1.0 / 255.0))
                        {
                            lowerTravel = midpointTravel;
                            continue;
                        }
                        const float3 midpointScene =
                            ReconstructViewPosition(
                                midpointUv, midpointDepth);
                        const float midpointSeparation =
                            midpointRay.z - midpointScene.z;
                        if (midpointSeparation >= 0.0)
                        {
                            upperTravel = midpointTravel;
                            candidateTravel = midpointTravel;
                            candidateUv = midpointUv;
                            candidateScenePosition = midpointScene;
                            candidateSeparation = midpointSeparation;
                        }
                        else
                        {
                            lowerTravel = midpointTravel;
                        }
                    }
                }

                const float thickness = max(
                    0.08,
                    candidateScenePosition.z * 0.0035);
                if (candidateSeparation <= thickness)
                {
                    hit = true;
                    hitUv = candidateUv;
                    hitTravel = candidateTravel;
                    break;
                }
            }
            previousDryTravel = travel;
            previousDrySeparation = separation;
            previousDrySample = true;
        }
        else
        {
            previousDrySample = false;
        }
        travel += stepLength;
        stepLength *= 1.12;
    }
    if (!hit)
        return 0.0;

    // BF1942 is LDR and supplies no roughness buffer. A fixed, conservative
    // five-tap footprint gives water a little gloss without temporal history.
    const float blurRadius = 1.5 + min(hitTravel * 0.015, 2.5);
    const float2 blurX = float2(textureInfo.x * blurRadius, 0.0);
    const float2 blurY = float2(0.0, textureInfo.y * blurRadius);
    float3 reflectedColor =
        worldColorTexture.SampleLevel(linearSampler, hitUv, 0.0).rgb * 0.40;
    reflectedColor +=
        worldColorTexture.SampleLevel(linearSampler, hitUv + blurX, 0.0).rgb * 0.15;
    reflectedColor +=
        worldColorTexture.SampleLevel(linearSampler, hitUv - blurX, 0.0).rgb * 0.15;
    reflectedColor +=
        worldColorTexture.SampleLevel(linearSampler, hitUv + blurY, 0.0).rgb * 0.15;
    reflectedColor +=
        worldColorTexture.SampleLevel(linearSampler, hitUv - blurY, 0.0).rgb * 0.15;
    reflectedColor = materialInfo.x > 0.5
        ? reflectedColor
        : SrgbToLinear(reflectedColor);

    const float viewFacing = saturate(dot(normal, -incident));
    const float fresnel = 0.02 + 0.98 * pow(1.0 - viewFacing, 5.0);
    const float edgeDistance = min(
        min(hitUv.x, 1.0 - hitUv.x),
        min(hitUv.y, 1.0 - hitUv.y));
    const float edgeConfidence = smoothstep(0.0, 0.08, edgeDistance);
    const float distanceConfidence = 1.0 - saturate(
        hitTravel / max(12.0, center.z * 1.5));
    const float sourceMask = depthMaskTexture.SampleLevel(
        pointSampler, uv, 0.0).a;
    const float confidence = saturate(sourceMask * 4.0) *
        edgeConfidence * distanceConfidence * normalValidity;
    return float4(reflectedColor, fresnel * confidence);
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
        "BFVR-D3D11WaterReflection",
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
        wchar_t message[1024] = {};
        swprintf_s(
            message,
            L"D3D11 water reflection could not compile %S: %S (HRESULT=0x%08lX).",
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
} // namespace

namespace bfvr::shared
{
D3D11WaterReflection::~D3D11WaterReflection()
{
    Shutdown();
}

bool D3D11WaterReflection::Initialize(
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
    if (SUCCEEDED(result) && CompileShader(
            kWaterReflectionPixelShader,
            "ps_4_0",
            &pixelBytecode,
            logCallback_,
            logContext_))
    {
        result = device->CreatePixelShader(
            pixelBytecode->GetBufferPointer(),
            pixelBytecode->GetBufferSize(),
            nullptr,
            &reflectionShader_);
    }
    if (pixelBytecode != nullptr)
        pixelBytecode->Release();
    if (vertexBytecode != nullptr)
        vertexBytecode->Release();

    D3D11_BUFFER_DESC bufferDescription = {};
    bufferDescription.ByteWidth = sizeof(float) * 16;
    bufferDescription.Usage = D3D11_USAGE_DEFAULT;
    bufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (SUCCEEDED(result))
        result = device->CreateBuffer(
            &bufferDescription,
            nullptr,
            &configurationBuffer_);

    D3D11_SAMPLER_DESC samplerDescription = {};
    samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
    if (SUCCEEDED(result))
        result = device->CreateSamplerState(
            &samplerDescription,
            &linearSampler_);
    samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    if (SUCCEEDED(result))
        result = device->CreateSamplerState(
            &samplerDescription,
            &pointSampler_);

    if (FAILED(result) || vertexShader_ == nullptr ||
        reflectionShader_ == nullptr || configurationBuffer_ == nullptr ||
        linearSampler_ == nullptr || pointSampler_ == nullptr)
    {
        WriteLog(
            L"D3D11 water reflection initialization failed (HRESULT=0x%08lX).",
            static_cast<unsigned long>(result));
        Shutdown();
        return false;
    }

    device_ = device;
    device_->AddRef();
    context_ = context;
    context_->AddRef();
    WriteLog(
        L"D3D11 water-only SSR initialized: per-eye 24-step spatial ray marching with six-step depth-crossing refinement, fixed roughness, Schlick Fresnel, packed-depth alpha mask, no temporal history or UI input.");
    return true;
}

bool D3D11WaterReflection::BuildEye(
    std::size_t eye,
    ID3D11ShaderResourceView* worldColor,
    bool worldColorAlreadyLinear,
    ID3D11ShaderResourceView* packedDepthAndMask,
    UINT width,
    UINT height,
    const float projection[16])
{
    if (eye >= eyes_.size() || context_ == nullptr ||
        worldColor == nullptr || packedDepthAndMask == nullptr ||
        width == 0 || height == 0 || projection == nullptr ||
        !EnsureEyeResources(eyes_[eye], width, height) ||
        !UpdateConfiguration(
            width,
            height,
            projection,
            worldColorAlreadyLinear))
    {
        return false;
    }

    const D3D11_VIEWPORT viewport = {
        0.0F,
        0.0F,
        static_cast<float>(width),
        static_cast<float>(height),
        0.0F,
        1.0F};
    const D3D11_RECT scissor = {
        0,
        0,
        static_cast<LONG>(width),
        static_cast<LONG>(height)};
    const float blendFactor[4] = {};
    const float clear[4] = {};
    context_->ClearRenderTargetView(eyes_[eye].target, clear);
    context_->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFFU);
    context_->OMSetDepthStencilState(nullptr, 0);
    context_->OMSetRenderTargets(1, &eyes_[eye].target, nullptr);
    context_->RSSetState(nullptr);
    context_->RSSetScissorRects(1, &scissor);
    context_->RSSetViewports(1, &viewport);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertexShader_, nullptr, 0);
    context_->PSSetShader(reflectionShader_, nullptr, 0);
    context_->PSSetConstantBuffers(0, 1, &configurationBuffer_);
    ID3D11SamplerState* samplers[2] = {linearSampler_, pointSampler_};
    context_->PSSetSamplers(0, 2, samplers);
    ID3D11ShaderResourceView* inputs[2] = {
        worldColor,
        packedDepthAndMask};
    context_->PSSetShaderResources(0, 2, inputs);
    context_->Draw(3, 0);

    ID3D11ShaderResourceView* nullInputs[2] = {};
    context_->PSSetShaderResources(0, 2, nullInputs);
    ID3D11RenderTargetView* nullTarget = nullptr;
    context_->OMSetRenderTargets(1, &nullTarget, nullptr);
    return true;
}

ID3D11ShaderResourceView* D3D11WaterReflection::GetEyeView(
    std::size_t eye) const noexcept
{
    return eye < eyes_.size() ? eyes_[eye].view : nullptr;
}

bool D3D11WaterReflection::EnsureEyeResources(
    EyeResources& resources,
    UINT width,
    UINT height)
{
    if (resources.width == width && resources.height == height &&
        resources.texture != nullptr && resources.view != nullptr &&
        resources.target != nullptr)
    {
        return true;
    }
    ReleaseEyeResources(resources);
    D3D11_TEXTURE2D_DESC description = {};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags =
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    HRESULT result = device_->CreateTexture2D(
        &description,
        nullptr,
        &resources.texture);
    if (SUCCEEDED(result))
        result = device_->CreateShaderResourceView(
            resources.texture,
            nullptr,
            &resources.view);
    if (SUCCEEDED(result))
        result = device_->CreateRenderTargetView(
            resources.texture,
            nullptr,
            &resources.target);
    if (FAILED(result))
    {
        ReleaseEyeResources(resources);
        return false;
    }
    resources.width = width;
    resources.height = height;
    return true;
}

bool D3D11WaterReflection::UpdateConfiguration(
    UINT width,
    UINT height,
    const float projection[16],
    bool worldColorAlreadyLinear)
{
    const float configuration[16] = {
        projection[0],
        projection[5],
        projection[8],
        projection[9],
        projection[10],
        projection[11],
        projection[14],
        projection[15],
        1.0F / static_cast<float>(width),
        1.0F / static_cast<float>(height),
        static_cast<float>(width),
        static_cast<float>(height),
        worldColorAlreadyLinear ? 1.0F : 0.0F,
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
    return true;
}

void D3D11WaterReflection::ReleaseEyeResources(EyeResources& resources)
{
    if (resources.target != nullptr)
        resources.target->Release();
    if (resources.view != nullptr)
        resources.view->Release();
    if (resources.texture != nullptr)
        resources.texture->Release();
    resources = {};
}

void D3D11WaterReflection::Shutdown()
{
    for (EyeResources& resources : eyes_)
        ReleaseEyeResources(resources);
    if (pointSampler_ != nullptr)
        pointSampler_->Release();
    if (linearSampler_ != nullptr)
        linearSampler_->Release();
    if (configurationBuffer_ != nullptr)
        configurationBuffer_->Release();
    if (reflectionShader_ != nullptr)
        reflectionShader_->Release();
    if (vertexShader_ != nullptr)
        vertexShader_->Release();
    if (context_ != nullptr)
        context_->Release();
    if (device_ != nullptr)
        device_->Release();
    pointSampler_ = nullptr;
    linearSampler_ = nullptr;
    configurationBuffer_ = nullptr;
    reflectionShader_ = nullptr;
    vertexShader_ = nullptr;
    context_ = nullptr;
    device_ = nullptr;
    logCallback_ = nullptr;
    logContext_ = nullptr;
}

void D3D11WaterReflection::WriteLog(const wchar_t* format, ...) const
{
    if (logCallback_ == nullptr)
        return;
    wchar_t message[1024] = {};
    va_list arguments;
    va_start(arguments, format);
    _vsnwprintf_s(message, std::size(message), _TRUNCATE, format, arguments);
    va_end(arguments);
    logCallback_(logContext_, message);
}
} // namespace bfvr::shared
