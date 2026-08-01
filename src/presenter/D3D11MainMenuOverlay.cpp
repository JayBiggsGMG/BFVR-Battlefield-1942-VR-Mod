#include "presenter/D3D11MainMenuOverlay.h"

#include "client/MainMenuOverlay.h"
#include "stereo/MainMenuOverlayLayout.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cwchar>
#include <iterator>
#include <vector>

namespace
{

constexpr char kVertexShader[] = R"(
struct VertexOutput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

VertexOutput main(uint vertexId : SV_VertexID)
{
    const float2 texcoord = float2(
        (vertexId << 1) & 2,
        vertexId & 2);
    VertexOutput output;
    output.position = float4(
        texcoord.x * 2.0 - 1.0,
        1.0 - texcoord.y * 2.0,
        0.0,
        1.0);
    output.texcoord = texcoord;
    return output;
}
)";

constexpr char kPixelShader[] = R"(
Texture2D buttonTexture : register(t0);
SamplerState buttonSampler : register(s0);

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target
{
    const float4 premultipliedSrgb =
        buttonTexture.SampleLevel(buttonSampler, texcoord, 0.0);
    const float alpha = premultipliedSrgb.a;
    if (alpha <= 0.0)
    {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
    const float3 straightSrgb =
        saturate(premultipliedSrgb.rgb / alpha);
    const float3 premultipliedLinear =
        pow(straightSrgb, 2.2) * alpha;
    return float4(premultipliedLinear, alpha);
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
    bfvr::shared::SharedTextureLogCallback logCallback,
    void* logContext)
{
    ID3DBlob* errors = nullptr;
    const HRESULT result = D3DCompile(
        source,
        std::char_traits<char>::length(source),
        "BFVR-D3D11MainMenuOverlay",
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
        wchar_t message[1024] = {};
        const char* detail = errors != nullptr
                && errors->GetBufferPointer() != nullptr
            ? static_cast<const char*>(errors->GetBufferPointer())
            : "no compiler detail";
        swprintf_s(
            message,
            L"D3D11 main-menu overlay could not compile %S: %S (HRESULT=0x%08lX).",
            target,
            detail,
            static_cast<unsigned long>(result));
        logCallback(logContext, message);
    }
    ReleaseInterface(errors);
    return SUCCEEDED(result) && bytecode != nullptr && *bytecode != nullptr;
}

} // namespace

namespace bfvr::shared
{

D3D11MainMenuOverlay::~D3D11MainMenuOverlay()
{
    Shutdown();
}

bool D3D11MainMenuOverlay::Initialize(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    SharedTextureLogCallback logCallback,
    void* logContext)
{
    Shutdown();
    logCallback_ = logCallback;
    logContext_ = logContext;
    if (device == nullptr || context == nullptr)
    {
        WriteLog(L"D3D11 main-menu overlay received a null device or context.");
        return false;
    }
    device_ = device;
    device_->AddRef();
    context_ = context;
    context_->AddRef();

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
    if (SUCCEEDED(result) &&
        CompileShader(
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

    D3D11_SAMPLER_DESC samplerDescription = {};
    samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
    if (SUCCEEDED(result))
    {
        result = device_->CreateSamplerState(
            &samplerDescription,
            &sampler_);
    }

    D3D11_BLEND_DESC blendDescription = {};
    D3D11_RENDER_TARGET_BLEND_DESC& target =
        blendDescription.RenderTarget[0];
    target.BlendEnable = TRUE;
    target.SrcBlend = D3D11_BLEND_ONE;
    target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    target.BlendOp = D3D11_BLEND_OP_ADD;
    target.SrcBlendAlpha = D3D11_BLEND_ONE;
    target.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (SUCCEEDED(result))
    {
        result = device_->CreateBlendState(
            &blendDescription,
            &blendState_);
    }
    if (SUCCEEDED(result) && !CreateButtonView(false, &buttonViews_[0]))
    {
        result = E_FAIL;
    }
    if (SUCCEEDED(result) && !CreateButtonView(true, &buttonViews_[1]))
    {
        result = E_FAIL;
    }
    if (FAILED(result) || !IsReady())
    {
        WriteLog(
            L"D3D11 main-menu overlay initialization failed (HRESULT=0x%08lX).",
            static_cast<unsigned long>(result));
        Shutdown();
        return false;
    }
    const stereo::UiCanvasRect buttonRect =
        stereo::BackToGameButtonRect();
    WriteLog(
        L"D3D11 main-menu overlay is ready on the presenter's local Ref2 texture; authored scales=(%.2f,%.2f), logical rectangle=(%.1f,%.1f)-(%.1f,%.1f).",
        stereo::kBackToGameButtonWidthScale,
        stereo::kBackToGameButtonHeightScale,
        buttonRect.left,
        buttonRect.top,
        buttonRect.right,
        buttonRect.bottom);
    return true;
}

bool D3D11MainMenuOverlay::Composite(
    ID3D11RenderTargetView* destination,
    UINT sourceUiWidth,
    UINT sourceUiHeight,
    UINT destinationWidth,
    UINT destinationHeight,
    bool visible,
    bool hovered)
{
    if (!visible)
    {
        return true;
    }
    if (!IsReady() || destination == nullptr ||
        sourceUiWidth == 0 || sourceUiHeight == 0 ||
        destinationWidth == 0 || destinationHeight == 0)
    {
        return false;
    }

    const stereo::UiCanvasRect logical =
        stereo::BackToGameButtonRect();
    // Match D3D11TextureScaler's source-aspect fit exactly. The Ref2 source
    // is normally 16:9 while the OpenXR UI texture is portrait-shaped; using
    // the entire destination height would put this raster below the visible
    // native menu panel.
    const stereo::UiCanvasRect fitted =
        stereo::MapUiCanvasRectThroughAspectFit(
            logical,
            static_cast<float>(sourceUiWidth),
            static_cast<float>(sourceUiHeight),
            static_cast<float>(destinationWidth),
            static_cast<float>(destinationHeight));
    const D3D11_VIEWPORT viewport = {
        fitted.left,
        fitted.top,
        fitted.right - fitted.left,
        fitted.bottom - fitted.top,
        0.0F,
        1.0F};
    ID3D11ShaderResourceView* const source =
        buttonViews_[hovered ? 1 : 0];
    constexpr float blendFactor[4] = {};
    context_->OMSetRenderTargets(1, &destination, nullptr);
    context_->OMSetBlendState(
        blendState_,
        blendFactor,
        0xFFFFFFFFU);
    context_->RSSetViewports(1, &viewport);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertexShader_, nullptr, 0);
    context_->PSSetShader(pixelShader_, nullptr, 0);
    context_->PSSetSamplers(0, 1, &sampler_);
    context_->PSSetShaderResources(0, 1, &source);
    context_->Draw(3, 0);

    ID3D11ShaderResourceView* nullSource = nullptr;
    ID3D11RenderTargetView* nullTarget = nullptr;
    context_->PSSetShaderResources(0, 1, &nullSource);
    context_->OMSetRenderTargets(1, &nullTarget, nullptr);
    context_->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFU);
    if (!firstCompositeLogged_)
    {
        firstCompositeLogged_ = true;
        WriteLog(
            L"D3D11 main-menu overlay composited its first %s frame at runtime rectangle=(%.1f,%.1f)-(%.1f,%.1f), fitted from source UI %ux%u into %ux%u.",
            hovered ? L"hover" : L"normal",
            viewport.TopLeftX,
            viewport.TopLeftY,
            viewport.TopLeftX + viewport.Width,
            viewport.TopLeftY + viewport.Height,
            sourceUiWidth,
            sourceUiHeight,
            destinationWidth,
            destinationHeight);
    }
    return true;
}

bool D3D11MainMenuOverlay::IsReady() const noexcept
{
    return device_ != nullptr && context_ != nullptr &&
        vertexShader_ != nullptr && pixelShader_ != nullptr &&
        sampler_ != nullptr && blendState_ != nullptr &&
        buttonViews_[0] != nullptr && buttonViews_[1] != nullptr;
}

void D3D11MainMenuOverlay::Shutdown()
{
    for (ID3D11ShaderResourceView*& view : buttonViews_)
    {
        ReleaseInterface(view);
    }
    ReleaseInterface(blendState_);
    ReleaseInterface(sampler_);
    ReleaseInterface(pixelShader_);
    ReleaseInterface(vertexShader_);
    ReleaseInterface(context_);
    ReleaseInterface(device_);
    firstCompositeLogged_ = false;
}

bool D3D11MainMenuOverlay::CreateButtonView(
    bool hovered,
    ID3D11ShaderResourceView** view)
{
    if (device_ == nullptr || view == nullptr)
    {
        return false;
    }
    MainMenuOverlay button;
    if (!button.Initialize(nullptr))
    {
        return false;
    }
    std::vector<std::uint32_t> pixels;
    UINT width = 0;
    UINT height = 0;
    if (!button.CopyButtonPixels(hovered, pixels, width, height) ||
        pixels.empty())
    {
        return false;
    }

    D3D11_TEXTURE2D_DESC description = {};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA data = {
        pixels.data(),
        width * sizeof(std::uint32_t),
        0};
    ID3D11Texture2D* texture = nullptr;
    HRESULT result = device_->CreateTexture2D(
        &description,
        &data,
        &texture);
    if (SUCCEEDED(result))
    {
        result = device_->CreateShaderResourceView(
            texture,
            nullptr,
            view);
    }
    ReleaseInterface(texture);
    return SUCCEEDED(result) && *view != nullptr;
}

void D3D11MainMenuOverlay::WriteLog(const wchar_t* format, ...) const
{
    if (logCallback_ == nullptr || format == nullptr)
    {
        return;
    }
    wchar_t message[1200] = {};
    va_list arguments;
    va_start(arguments, format);
    _vsnwprintf_s(
        message,
        std::size(message),
        _TRUNCATE,
        format,
        arguments);
    va_end(arguments);
    logCallback_(logContext_, message);
}

} // namespace bfvr::shared
