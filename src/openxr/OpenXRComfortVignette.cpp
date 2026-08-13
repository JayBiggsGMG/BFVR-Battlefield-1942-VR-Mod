#include "openxr/OpenXRComfortVignette.h"

#include "openxr/OpenXRScopeOverlayLayer.h"
#include "stereo/ComfortVignette.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace
{
constexpr UINT kVignetteTextureSize = 256;
constexpr float kVisibleStrengthThreshold = 0.001F;

constexpr char kVertexShader[] = R"(
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

constexpr char kPixelShader[] = R"(
cbuffer Configuration : register(b0)
{
    float strength;
    float deathBlend;
    float2 padding;
};

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target
{
    const float radius = length(texcoord * 2.0 - 1.0);
    // At rest, the clear inner radius is outside every corner. While moving,
    // the Quest-class target leaves a clear centre near 50-55 degrees, a
    // broad blurred transition, and a fully black outer band with meaningful
    // reserve inside the physical headset FOV instead of ending at its edge.
    const float innerRadius = lerp(1.50, 0.38, saturate(strength));
    const float outerRadius = lerp(1.80, 0.68, saturate(strength));
    const float movementOpacity = smoothstep(innerRadius, outerRadius, radius);
    // Death comfort takes priority inside this same compositor. It narrows
    // farther than movement comfort and uses a muted dark red rather than a
    // bright warning red. The clear center remains fully stereo and tracked.
    const float deathOpacity = smoothstep(0.16, 0.52, radius);
    const float death = saturate(deathBlend);
    const float opacity = lerp(movementOpacity, deathOpacity, death);
    const float3 color = lerp(
        float3(0.0, 0.0, 0.0),
        float3(0.22, 0.012, 0.008),
        death);
    return float4(color * opacity, opacity);
}
)";

template <typename T>
void ReleaseInterface(T*& value) noexcept
{
    if (value != nullptr)
    {
        value->Release();
        value = nullptr;
    }
}

bool CompileShader(
    const char* source,
    const char* target,
    ID3DBlob** bytecode,
    bfvr::OpenXRLogCallback logCallback,
    void* logContext)
{
    ID3DBlob* errors = nullptr;
    const HRESULT result = D3DCompile(
        source,
        std::strlen(source),
        "BFVR-OpenXRComfortVignette",
        nullptr,
        nullptr,
        "main",
        target,
        D3DCOMPILE_ENABLE_STRICTNESS,
        0,
        bytecode,
        &errors);
    if (FAILED(result) && logCallback != nullptr)
    {
        std::array<wchar_t, 1000> message = {};
        if (errors != nullptr && errors->GetBufferPointer() != nullptr)
        {
            _snwprintf_s(
                message.data(),
                message.size(),
                _TRUNCATE,
                L"Comfort vignette could not compile %S: %S (HRESULT=0x%08lX).",
                target,
                static_cast<const char*>(errors->GetBufferPointer()),
                static_cast<unsigned long>(result));
        }
        else
        {
            _snwprintf_s(
                message.data(),
                message.size(),
                _TRUNCATE,
                L"Comfort vignette could not compile %S (HRESULT=0x%08lX).",
                target,
                static_cast<unsigned long>(result));
        }
        logCallback(logContext, message.data());
    }
    ReleaseInterface(errors);
    return SUCCEEDED(result) && bytecode != nullptr && *bytecode != nullptr;
}
} // namespace

namespace bfvr
{

OpenXRComfortVignette::~OpenXRComfortVignette()
{
    Shutdown();
}

bool OpenXRComfortVignette::Initialize(
    XrSession session,
    DXGI_FORMAT swapchainFormat,
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    const OpenXRComfortVignetteApi& api,
    OpenXRLogCallback logCallback,
    void* logContext)
{
    Shutdown();
    logCallback_ = logCallback;
    logContext_ = logContext;
    if (session == XR_NULL_HANDLE || device == nullptr || context == nullptr ||
        swapchainFormat == DXGI_FORMAT_UNKNOWN ||
        api.createSwapchain == nullptr || api.destroySwapchain == nullptr ||
        api.enumerateSwapchainImages == nullptr ||
        api.acquireSwapchainImage == nullptr ||
        api.waitSwapchainImage == nullptr ||
        api.releaseSwapchainImage == nullptr)
    {
        WriteLog(L"Comfort vignette received incomplete OpenXR/D3D11 initialization state.");
        return false;
    }
    session_ = session;
    swapchainFormat_ = swapchainFormat;
    api_ = api;
    device_ = device;
    device_->AddRef();
    context_ = context;
    context_->AddRef();
    if (!CreateShaders() || !CreateSwapchain())
    {
        Shutdown();
        return false;
    }
    WriteLog(
        L"Comfort vignette initialized as one 256x256 procedural soft-aperture pass and two eye-exclusive VIEW quads; it is ordered above the world projection and below every HUD/menu overlay.");
    return true;
}

std::size_t OpenXRComfortVignette::AppendLayers(
    float targetStrength,
    float targetDeathBlend,
    float deltaSeconds,
    XrSpace viewSpace,
    const XrPosef& headInLocalSpace,
    const std::array<XrView, 2>& viewsInLocalSpace,
    const XrCompositionLayerBaseHeader** destination,
    std::size_t capacity)
{
    currentStrength_ = stereo::AdvanceComfortVignetteStrength(
        currentStrength_,
        targetStrength,
        deltaSeconds);
    currentDeathBlend_ = stereo::AdvanceComfortVignetteStrength(
        currentDeathBlend_,
        targetDeathBlend,
        deltaSeconds);
    if (currentStrength_ <= kVisibleStrengthThreshold &&
        currentDeathBlend_ <= kVisibleStrengthThreshold)
    {
        currentStrength_ = 0.0F;
        currentDeathBlend_ = 0.0F;
        return 0;
    }
    if (destination == nullptr || capacity < layers_.size() ||
        swapchain_.handle == XR_NULL_HANDLE ||
        !Render(currentStrength_, currentDeathBlend_) ||
        !BuildOpenXREyeFillingScopeLayers(
            viewSpace,
            swapchain_.handle,
            swapchain_.width,
            swapchain_.height,
            headInLocalSpace,
            viewsInLocalSpace,
            layers_))
    {
        return 0;
    }
    for (std::size_t eye = 0; eye < layers_.size(); ++eye)
    {
        destination[eye] = reinterpret_cast<
            const XrCompositionLayerBaseHeader*>(&layers_[eye]);
    }
    if (!firstVisibleFrameLogged_)
    {
        WriteLog(
            L"Comfort vignette submitted its first world-only layer at movement strength %.3f and death blend %.3f; Ref2 HUD, scope, Quick Menu, and VR Settings remain above it and unobscured.",
            currentStrength_,
            currentDeathBlend_);
        firstVisibleFrameLogged_ = true;
    }
    if (currentDeathBlend_ > kVisibleStrengthThreshold &&
        !firstDeathFrameLogged_)
    {
        WriteLog(
            L"Death-camera comfort submitted its first muted dark-red aperture through the existing single vignette compositor.");
        firstDeathFrameLogged_ = true;
    }
    return layers_.size();
}

float OpenXRComfortVignette::CurrentStrength() const noexcept
{
    return currentStrength_;
}

void OpenXRComfortVignette::Shutdown()
{
    DestroySwapchain();
    ReleaseInterface(configurationBuffer_);
    ReleaseInterface(pixelShader_);
    ReleaseInterface(vertexShader_);
    ReleaseInterface(context_);
    ReleaseInterface(device_);
    api_ = {};
    session_ = XR_NULL_HANDLE;
    swapchainFormat_ = DXGI_FORMAT_UNKNOWN;
    layers_ = {};
    currentStrength_ = 0.0F;
    currentDeathBlend_ = 0.0F;
    firstVisibleFrameLogged_ = false;
    firstDeathFrameLogged_ = false;
    logCallback_ = nullptr;
    logContext_ = nullptr;
}

bool OpenXRComfortVignette::CreateShaders()
{
    ID3DBlob* vertexBytecode = nullptr;
    ID3DBlob* pixelBytecode = nullptr;
    HRESULT result = E_FAIL;
    if (CompileShader(
            kVertexShader,
            "vs_4_0",
            &vertexBytecode,
            logCallback_,
            logContext_))
    {
        result = device_->CreateVertexShader(
            vertexBytecode->GetBufferPointer(),
            vertexBytecode->GetBufferSize(),
            nullptr,
            &vertexShader_);
    }
    if (SUCCEEDED(result) && CompileShader(
            kPixelShader,
            "ps_4_0",
            &pixelBytecode,
            logCallback_,
            logContext_))
    {
        result = device_->CreatePixelShader(
            pixelBytecode->GetBufferPointer(),
            pixelBytecode->GetBufferSize(),
            nullptr,
            &pixelShader_);
    }
    ReleaseInterface(pixelBytecode);
    ReleaseInterface(vertexBytecode);

    D3D11_BUFFER_DESC bufferDescription = {};
    bufferDescription.ByteWidth = sizeof(float) * 4;
    bufferDescription.Usage = D3D11_USAGE_DEFAULT;
    bufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (SUCCEEDED(result))
    {
        result = device_->CreateBuffer(
            &bufferDescription,
            nullptr,
            &configurationBuffer_);
    }
    if (FAILED(result) || vertexShader_ == nullptr || pixelShader_ == nullptr ||
        configurationBuffer_ == nullptr)
    {
        WriteLog(
            L"Comfort vignette D3D11 shader initialization failed (HRESULT=0x%08lX).",
            static_cast<unsigned long>(result));
        return false;
    }
    return true;
}

bool OpenXRComfortVignette::CreateSwapchain()
{
    XrSwapchainCreateInfo createInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    createInfo.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
        XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.format = static_cast<std::int64_t>(swapchainFormat_);
    createInfo.sampleCount = 1;
    createInfo.width = kVignetteTextureSize;
    createInfo.height = kVignetteTextureSize;
    createInfo.faceCount = 1;
    createInfo.arraySize = 1;
    createInfo.mipCount = 1;
    XrResult result = api_.createSwapchain(
        session_,
        &createInfo,
        &swapchain_.handle);
    if (XR_FAILED(result) || swapchain_.handle == XR_NULL_HANDLE)
    {
        WriteLog(
            L"Comfort vignette swapchain creation failed (result=%ld).",
            static_cast<long>(result));
        return false;
    }
    std::uint32_t imageCount = 0;
    result = api_.enumerateSwapchainImages(
        swapchain_.handle,
        0,
        &imageCount,
        nullptr);
    if (XR_FAILED(result) || imageCount == 0)
    {
        WriteLog(
            L"Comfort vignette swapchain image-count query failed (result=%ld count=%u).",
            static_cast<long>(result),
            imageCount);
        return false;
    }
    swapchain_.images.resize(imageCount);
    for (XrSwapchainImageD3D11KHR& image : swapchain_.images)
    {
        image.type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
        image.next = nullptr;
    }
    result = api_.enumerateSwapchainImages(
        swapchain_.handle,
        imageCount,
        &imageCount,
        reinterpret_cast<XrSwapchainImageBaseHeader*>(
            swapchain_.images.data()));
    if (XR_FAILED(result))
    {
        WriteLog(
            L"Comfort vignette swapchain image enumeration failed (result=%ld count=%u).",
            static_cast<long>(result),
            imageCount);
        return false;
    }
    swapchain_.images.resize(imageCount);
    swapchain_.targets.resize(imageCount);
    for (std::size_t index = 0; index < swapchain_.images.size(); ++index)
    {
        ID3D11Texture2D* const texture = swapchain_.images[index].texture;
        D3D11_TEXTURE2D_DESC textureDescription = {};
        if (texture != nullptr)
        {
            texture->GetDesc(&textureDescription);
        }
        // OpenXR runtimes commonly expose a typeless resource even though the
        // application selected a typed sRGB swapchain format. A null RTV
        // description inherits that typeless resource format and fails with
        // E_INVALIDARG. Name the negotiated view format explicitly.
        D3D11_RENDER_TARGET_VIEW_DESC targetDescription = {};
        targetDescription.Format = swapchainFormat_;
        targetDescription.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        targetDescription.Texture2D.MipSlice = 0;
        const HRESULT targetResult = texture == nullptr
            ? E_POINTER
            : device_->CreateRenderTargetView(
                texture,
                &targetDescription,
                &swapchain_.targets[index]);
        if (FAILED(targetResult))
        {
            WriteLog(
                L"Comfort vignette could not create swapchain RTV %zu: resourceFormat=%u requestedFormat=%u bindFlags=0x%08X array=%u samples=%u (HRESULT=0x%08lX).",
                index,
                static_cast<unsigned int>(textureDescription.Format),
                static_cast<unsigned int>(swapchainFormat_),
                textureDescription.BindFlags,
                textureDescription.ArraySize,
                textureDescription.SampleDesc.Count,
                static_cast<unsigned long>(targetResult));
            return false;
        }
    }
    swapchain_.width = kVignetteTextureSize;
    swapchain_.height = kVignetteTextureSize;
    return true;
}

bool OpenXRComfortVignette::Render(float strength, float deathBlend)
{
    std::uint32_t imageIndex = 0;
    XrSwapchainImageAcquireInfo acquireInfo{
        XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrResult result = api_.acquireSwapchainImage(
        swapchain_.handle,
        &acquireInfo,
        &imageIndex);
    if (XR_FAILED(result) || imageIndex >= swapchain_.targets.size())
    {
        return false;
    }
    XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    result = api_.waitSwapchainImage(swapchain_.handle, &waitInfo);
    if (XR_SUCCEEDED(result) && swapchain_.targets[imageIndex] != nullptr)
    {
        const std::array<float, 4> configuration = {
            std::clamp(strength, 0.0F, 1.0F),
            std::clamp(deathBlend, 0.0F, 1.0F),
            0.0F,
            0.0F};
        context_->UpdateSubresource(
            configurationBuffer_,
            0,
            nullptr,
            configuration.data(),
            0,
            0);
        ID3D11RenderTargetView* target = swapchain_.targets[imageIndex];
        const D3D11_VIEWPORT viewport = {
            0.0F,
            0.0F,
            static_cast<float>(swapchain_.width),
            static_cast<float>(swapchain_.height),
            0.0F,
            1.0F};
        const float blendFactor[4] = {};
        context_->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFFU);
        context_->OMSetDepthStencilState(nullptr, 0);
        context_->OMSetRenderTargets(1, &target, nullptr);
        context_->RSSetState(nullptr);
        context_->RSSetViewports(1, &viewport);
        context_->IASetInputLayout(nullptr);
        context_->IASetPrimitiveTopology(
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(vertexShader_, nullptr, 0);
        context_->PSSetShader(pixelShader_, nullptr, 0);
        context_->PSSetConstantBuffers(0, 1, &configurationBuffer_);
        context_->Draw(3, 0);
        ID3D11RenderTargetView* nullTarget = nullptr;
        ID3D11Buffer* nullBuffer = nullptr;
        context_->OMSetRenderTargets(1, &nullTarget, nullptr);
        context_->PSSetConstantBuffers(0, 1, &nullBuffer);
        context_->PSSetShader(nullptr, nullptr, 0);
        context_->VSSetShader(nullptr, nullptr, 0);
    }
    XrSwapchainImageReleaseInfo releaseInfo{
        XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    const XrResult releaseResult = api_.releaseSwapchainImage(
        swapchain_.handle,
        &releaseInfo);
    return XR_SUCCEEDED(result) && XR_SUCCEEDED(releaseResult);
}

void OpenXRComfortVignette::DestroySwapchain() noexcept
{
    for (ID3D11RenderTargetView*& target : swapchain_.targets)
    {
        ReleaseInterface(target);
    }
    if (swapchain_.handle != XR_NULL_HANDLE &&
        api_.destroySwapchain != nullptr)
    {
        api_.destroySwapchain(swapchain_.handle);
    }
    swapchain_ = {};
}

void OpenXRComfortVignette::WriteLog(const wchar_t* format, ...) const
{
    if (logCallback_ == nullptr || format == nullptr)
    {
        return;
    }
    std::array<wchar_t, 1200> message = {};
    va_list arguments;
    va_start(arguments, format);
    _vsnwprintf_s(
        message.data(),
        message.size(),
        _TRUNCATE,
        format,
        arguments);
    va_end(arguments);
    logCallback_(logContext_, message.data());
}

} // namespace bfvr
