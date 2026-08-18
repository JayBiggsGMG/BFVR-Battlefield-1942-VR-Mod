#include "presenter/DesktopMirror.h"

#include "stereo/QuickMenuMirrorMath.h"
#include "stereo/ScopeViewMath.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>

namespace
{
constexpr wchar_t kWindowClassName[] = L"BFVRDesktopMirrorCanvas";

constexpr char kVertexShaderSource[] = R"(
struct VertexOutput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

VertexOutput main(uint vertexId : SV_VertexID)
{
    VertexOutput output;
    const float2 position = vertexId == 0 ? float2(-1.0, -1.0) :
        vertexId == 1 ? float2(-1.0, 3.0) : float2(3.0, -1.0);
    output.position = float4(position, 0.0, 1.0);
    output.texcoord = float2((position.x + 1.0) * 0.5,
        (1.0 - position.y) * 0.5);
    return output;
}
)";

constexpr char kPixelShaderSource[] = R"(
Texture2D worldTexture : register(t0);
Texture2D uiTexture : register(t1);
SamplerState sourceSampler : register(s0);
cbuffer MirrorCrop : register(b0)
{
    float2 sourceScale;
    float2 sourceOffset;
};

float3 LinearToSrgb(float3 color)
{
    const float3 low = color * 12.92;
    const float3 high = 1.055 * pow(max(color, 0.0), 1.0 / 2.4) - 0.055;
    return lerp(high, low, step(color, 0.0031308));
}

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target
{
    const float2 sourceTexcoord = texcoord * sourceScale + sourceOffset;
    const float4 world = worldTexture.Sample(sourceSampler, sourceTexcoord);
    const float4 ui = uiTexture.Sample(sourceSampler, sourceTexcoord);
    return float4(LinearToSrgb(lerp(world.rgb, ui.rgb, saturate(ui.a))), 1.0);
}
)";

constexpr char kQuickMenuVertexShaderSource[] = R"(
cbuffer QuickMenuConfiguration : register(b0)
{
    float4 clipPositions[4];
    float convertFromLinear;
    float3 configurationPadding;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

VertexOutput main(uint vertexId : SV_VertexID)
{
    VertexOutput output;
    output.position = clipPositions[vertexId];
    output.texcoord = vertexId == 0 ? float2(0.0, 1.0) :
        vertexId == 1 ? float2(0.0, 0.0) :
        vertexId == 2 ? float2(1.0, 1.0) : float2(1.0, 0.0);
    return output;
}
)";

constexpr char kQuickMenuPixelShaderSource[] = R"(
Texture2D quickMenuTexture : register(t0);
SamplerState sourceSampler : register(s0);
cbuffer QuickMenuConfiguration : register(b0)
{
    float4 clipPositions[4];
    float convertFromLinear;
    float3 configurationPadding;
};

float3 LinearToSrgb(float3 color)
{
    const float3 low = color * 12.92;
    const float3 high = 1.055 * pow(max(color, 0.0), 1.0 / 2.4) - 0.055;
    return lerp(high, low, step(color, 0.0031308));
}

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target
{
    const float4 color = quickMenuTexture.Sample(sourceSampler, texcoord);
    const float3 encoded = lerp(
        color.rgb,
        LinearToSrgb(color.rgb),
        convertFromLinear);
    return float4(encoded, color.a);
}
)";

struct QuickMenuDrawConfiguration
{
    float clipPositions[4][4] = {};
    float convertFromLinear = 0.0F;
    float padding[3] = {};
};

static_assert(sizeof(QuickMenuDrawConfiguration) % 16 == 0);

bfvr::stereo::Pose ToStereoPose(
    const bfvr::OpenXRPresentationPose& source) noexcept
{
    return {
        {source.positionX, source.positionY, source.positionZ},
        {
            source.orientationX,
            source.orientationY,
            source.orientationZ,
            source.orientationW}};
}

bfvr::OpenXRPresentationPose ToPresentationPose(
    const bfvr::stereo::Pose& source) noexcept
{
    bfvr::OpenXRPresentationPose result = {};
    result.positionX = source.position.x;
    result.positionY = source.position.y;
    result.positionZ = source.position.z;
    result.orientationX = source.orientation.x;
    result.orientationY = source.orientation.y;
    result.orientationZ = source.orientation.z;
    result.orientationW = source.orientation.w;
    return result;
}

bool IsSrgbFormat(DXGI_FORMAT format) noexcept
{
    return format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
        format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
}

bool ReadDesktopMirrorEnabled()
{
    wchar_t value[2] = {};
    const DWORD length = GetEnvironmentVariableW(
        L"BFVR_DESKTOP_MIRROR", value, static_cast<DWORD>(std::size(value)));
    return !(length == 1 && value[0] == L'0');
}

struct WindowSearch
{
    DWORD processId = 0;
    HWND result = nullptr;
};

BOOL CALLBACK FindProducerWindow(HWND window, LPARAM parameter)
{
    WindowSearch* const search = reinterpret_cast<WindowSearch*>(parameter);
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId != search->processId || !IsWindowVisible(window) ||
        GetWindow(window, GW_OWNER) != nullptr ||
        (GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD) != 0)
    {
        return TRUE;
    }
    search->result = window;
    return FALSE;
}

bool CompileShader(const char* source, const char* target, ID3DBlob** bytecode)
{
    ID3DBlob* errors = nullptr;
    const HRESULT result = D3DCompile(
        source, std::strlen(source), "BFVR-DesktopMirror", nullptr, nullptr,
        "main", target, D3DCOMPILE_ENABLE_STRICTNESS, 0, bytecode, &errors);
    if (errors != nullptr)
    {
        errors->Release();
    }
    return SUCCEEDED(result) && *bytecode != nullptr;
}
} // namespace

namespace bfvr
{
DesktopMirror::~DesktopMirror()
{
    Shutdown();
}

bool DesktopMirror::Initialize(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    DWORD producerProcessId,
    OpenXRLogCallback logCallback,
    void* logContext)
{
    Shutdown();
    logCallback_ = logCallback;
    logContext_ = logContext;
    if (!ReadDesktopMirrorEnabled())
    {
        WriteLog(L"Desktop mirror disabled by BFVR_DESKTOP_MIRROR=0.");
        return false;
    }
    if (device == nullptr || context == nullptr || producerProcessId == 0)
    {
        WriteLog(L"Desktop mirror skipped because its device, context, or producer process is unavailable.");
        return false;
    }
    device_ = device;
    device_->AddRef();
    context_ = context;
    context_->AddRef();
    producerProcessId_ = producerProcessId;
    initialized_ = true;
    WriteLog(L"Desktop mirror is enabled; it will cover the BF1942 client at its native client resolution with a widescreen centre-cropped right-eye-plus-UI preview when the game window is available.");
    return true;
}

void DesktopMirror::PumpMessages()
{
    MSG message = {};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void DesktopMirror::Render(
    const OpenXRPresentationTextures& textures,
    const OpenXRPresentationView* rightEyeView,
    OpenXRUiPresentationMode uiPresentationMode,
    const OpenXRQuickMenuMirrorState* quickMenu)
{
    if (!initialized_ || permanentlyDisabled_ || textures.rightWorld == nullptr ||
        textures.ref2Ui == nullptr)
    {
        return;
    }
    if (!EnsureWindow() || !EnsureSwapchain() || !CreatePipeline() ||
        !EnsureSourceViews(textures))
    {
        return;
    }

    D3D11_TEXTURE2D_DESC sourceDescription = {};
    textures.rightWorld->GetDesc(&sourceDescription);
    if (sourceDescription.Width == 0 || sourceDescription.Height == 0 ||
        bufferWidth_ == 0 || bufferHeight_ == 0)
    {
        return;
    }
    const float sourceAspect =
        static_cast<float>(sourceDescription.Width) / sourceDescription.Height;
    const float destinationAspect =
        static_cast<float>(bufferWidth_) / bufferHeight_;
    struct MirrorCropConfiguration
    {
        float sourceScale[2] = {1.0F, 1.0F};
        float sourceOffset[2] = {0.0F, 0.0F};
    } crop = {};
    if (sourceAspect < destinationAspect)
    {
        crop.sourceScale[1] = sourceAspect / destinationAspect;
        crop.sourceOffset[1] = (1.0F - crop.sourceScale[1]) * 0.5F;
    }
    else if (sourceAspect > destinationAspect)
    {
        crop.sourceScale[0] = destinationAspect / sourceAspect;
        crop.sourceOffset[0] = (1.0F - crop.sourceScale[0]) * 0.5F;
    }
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(bufferWidth_);
    viewport.Height = static_cast<float>(bufferHeight_);
    viewport.MinDepth = 0.0F;
    viewport.MaxDepth = 1.0F;

    const float black[] = {0.0F, 0.0F, 0.0F, 1.0F};
    context_->OMSetRenderTargets(1, &targetView_, nullptr);
    context_->ClearRenderTargetView(targetView_, black);
    context_->RSSetViewports(1, &viewport);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertexShader_, nullptr, 0);
    context_->PSSetShader(pixelShader_, nullptr, 0);
    context_->PSSetSamplers(0, 1, &sampler_);
    context_->UpdateSubresource(cropConfiguration_, 0, nullptr, &crop, 0, 0);
    context_->PSSetConstantBuffers(0, 1, &cropConfiguration_);
    const bool eyeFillingScope =
        uiPresentationMode == OpenXRUiPresentationMode::EyeFillingScope &&
        rightEyeView != nullptr;
    // In ordinary modes the Ref2 layer is texture-aligned with the world.
    // A scope is different: the headset submits it as an eye-exclusive quad
    // centred on each eye's optical axis. Defer that UI draw so the desktop
    // path can reproduce the right-eye compositor placement below.
    const std::array<ID3D11ShaderResourceView*, 2> views = {
        worldView_, eyeFillingScope ? nullptr : uiView_};
    context_->PSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
    context_->Draw(3, 0);
    const std::array<ID3D11ShaderResourceView*, 2> cleared = {nullptr, nullptr};
    context_->PSSetShaderResources(0, static_cast<UINT>(cleared.size()), cleared.data());

    if (eyeFillingScope)
    {
        const bfvr::stereo::ScopeOverlayFov fov = {
            rightEyeView->fov.angleLeft,
            rightEyeView->fov.angleRight,
            rightEyeView->fov.angleUp,
            rightEyeView->fov.angleDown};
        const auto scopeQuad =
            bfvr::stereo::MakeEyeFillingScopeOverlayQuad(
                ToStereoPose(rightEyeView->pose),
                fov);
        const float blendFactor[4] = {};
        context_->OMSetBlendState(
            quickMenuBlendState_,
            blendFactor,
            0xFFFFFFFFU);
        const bool scopeDrawn = scopeQuad.has_value() &&
            DrawQuickMenuQuad(
                ToPresentationPose(scopeQuad->pose),
                scopeQuad->widthMeters,
                scopeQuad->heightMeters,
                uiView_,
                true,
                *rightEyeView,
                crop.sourceScale,
                crop.sourceOffset);
        context_->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFFU);
        if (scopeDrawn && !firstScopeMirroredLogged_)
        {
            firstScopeMirroredLogged_ = true;
            WriteLog(L"Desktop mirror composited its first scope Ref2 layer through the physical right-eye pose/FOV, matching the eye-exclusive headset quad instead of texture-centre alignment.");
        }
        else if (!scopeDrawn && !scopeMirrorFailureReported_)
        {
            scopeMirrorFailureReported_ = true;
            WriteLog(L"Desktop mirror could not project the right-eye scope Ref2 layer; the right-eye world preview and headset scope remain active.");
        }
    }

    if (rightEyeView != nullptr && quickMenu != nullptr &&
        quickMenu->visible && quickMenu->menuTexture != nullptr &&
        EnsureQuickMenuViews(*quickMenu))
    {
        const float blendFactor[4] = {};
        context_->OMSetBlendState(
            quickMenuBlendState_,
            blendFactor,
            0xFFFFFFFFU);
        D3D11_TEXTURE2D_DESC menuDescription = {};
        quickMenu->menuTexture->GetDesc(&menuDescription);
        const bool menuDrawn = DrawQuickMenuQuad(
            quickMenu->panelPose,
            quickMenu->panelWidthMeters,
            quickMenu->panelHeightMeters,
            quickMenuView_,
            IsSrgbFormat(menuDescription.Format),
            *rightEyeView,
            crop.sourceScale,
            crop.sourceOffset);
        if (quickMenuUtilityView_ != nullptr &&
            quickMenu->utilityTexture != nullptr)
        {
            D3D11_TEXTURE2D_DESC utilityDescription = {};
            quickMenu->utilityTexture->GetDesc(&utilityDescription);
            DrawQuickMenuQuad(
                quickMenu->utilityPose,
                quickMenu->utilityWidthMeters,
                quickMenu->utilityHeightMeters,
                quickMenuUtilityView_,
                IsSrgbFormat(utilityDescription.Format),
                *rightEyeView,
                crop.sourceScale,
                crop.sourceOffset);
        }
        if (quickMenuCommandView_ != nullptr &&
            quickMenu->commandTexture != nullptr)
        {
            D3D11_TEXTURE2D_DESC commandDescription = {};
            quickMenu->commandTexture->GetDesc(&commandDescription);
            DrawQuickMenuQuad(
                quickMenu->commandPose,
                quickMenu->commandWidthMeters,
                quickMenu->commandHeightMeters,
                quickMenuCommandView_,
                IsSrgbFormat(commandDescription.Format),
                *rightEyeView,
                crop.sourceScale,
                crop.sourceOffset);
        }
        if (quickMenu->pointerVisible &&
            quickMenuCursorView_ != nullptr &&
            quickMenu->cursorTexture != nullptr)
        {
            D3D11_TEXTURE2D_DESC cursorDescription = {};
            quickMenu->cursorTexture->GetDesc(&cursorDescription);
            DrawQuickMenuQuad(
                quickMenu->cursorPose,
                quickMenu->cursorWidthMeters,
                quickMenu->cursorHeightMeters,
                quickMenuCursorView_,
                IsSrgbFormat(cursorDescription.Format),
                *rightEyeView,
                crop.sourceScale,
                crop.sourceOffset);
        }
        if (menuDrawn && !firstQuickMenuMirroredLogged_)
        {
            firstQuickMenuMirroredLogged_ = true;
            WriteLog(L"Desktop mirror composited its first Quick Menu frame through the current right-eye pose/FOV and centre crop.");
        }
        else if (!menuDrawn && !quickMenuMirrorFailureReported_)
        {
            quickMenuMirrorFailureReported_ = true;
            WriteLog(L"Desktop mirror could not project the current Quick Menu pose; the headset menu and ordinary right-eye preview remain active.");
        }
        context_->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFFU);
    }
    context_->OMSetRenderTargets(0, nullptr, nullptr);

    const HRESULT result = swapchain_->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
    if (FAILED(result) && result != DXGI_ERROR_WAS_STILL_DRAWING)
    {
        Disable(L"Desktop mirror stopped after its nonblocking present failed; the headset presentation remains active.");
    }
}

void DesktopMirror::Shutdown()
{
    ReleaseQuickMenuViews();
    ReleaseSourceViews();
    ReleaseSwapchain();
    if (window_ != nullptr)
    {
        DestroyWindow(window_);
        window_ = nullptr;
    }
    if (sampler_ != nullptr)
    {
        sampler_->Release();
        sampler_ = nullptr;
    }
    if (cropConfiguration_ != nullptr)
    {
        cropConfiguration_->Release();
        cropConfiguration_ = nullptr;
    }
    if (quickMenuBlendState_ != nullptr)
    {
        quickMenuBlendState_->Release();
        quickMenuBlendState_ = nullptr;
    }
    if (quickMenuConfiguration_ != nullptr)
    {
        quickMenuConfiguration_->Release();
        quickMenuConfiguration_ = nullptr;
    }
    if (quickMenuPixelShader_ != nullptr)
    {
        quickMenuPixelShader_->Release();
        quickMenuPixelShader_ = nullptr;
    }
    if (quickMenuVertexShader_ != nullptr)
    {
        quickMenuVertexShader_->Release();
        quickMenuVertexShader_ = nullptr;
    }
    if (pixelShader_ != nullptr)
    {
        pixelShader_->Release();
        pixelShader_ = nullptr;
    }
    if (vertexShader_ != nullptr)
    {
        vertexShader_->Release();
        vertexShader_ = nullptr;
    }
    parentWindow_ = nullptr;
    bufferWidth_ = 0;
    bufferHeight_ = 0;
    producerProcessId_ = 0;
    initialized_ = false;
    permanentlyDisabled_ = false;
    quickMenuMirrorFailureReported_ = false;
    firstQuickMenuMirroredLogged_ = false;
    scopeMirrorFailureReported_ = false;
    firstScopeMirroredLogged_ = false;
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
    logCallback_ = nullptr;
    logContext_ = nullptr;
}

LRESULT CALLBACK DesktopMirror::WindowProcedure(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        const CREATESTRUCTW* const create =
            reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(
            window, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    if (message == WM_NCHITTEST)
    {
        return HTTRANSPARENT;
    }
    if (message == WM_MOUSEACTIVATE)
    {
        return MA_NOACTIVATE;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool DesktopMirror::EnsureWindow()
{
    if (window_ != nullptr && IsWindow(window_) && parentWindow_ != nullptr &&
        IsWindow(parentWindow_))
    {
        UpdateWindowBounds();
        return true;
    }
    if (window_ != nullptr)
    {
        DestroyWindow(window_);
        window_ = nullptr;
    }
    ReleaseSwapchain();
    parentWindow_ = nullptr;

    WindowSearch search = {};
    search.processId = producerProcessId_;
    EnumWindows(FindProducerWindow, reinterpret_cast<LPARAM>(&search));
    if (search.result == nullptr)
    {
        return false;
    }
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = kWindowClassName;
    windowClass.lpfnWndProc = WindowProcedure;
    if (RegisterClassExW(&windowClass) == 0 &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        Disable(L"Desktop mirror could not register its noninteractive window class; the headset presentation remains active.");
        return false;
    }
    parentWindow_ = search.result;
    window_ = CreateWindowExW(
        WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
        kWindowClassName,
        L"BFVR Desktop Mirror",
        // The parent belongs to the game process. Disable this display-only
        // child as well as returning HTTRANSPARENT so it can never become an
        // input target across the process boundary.
        WS_CHILD | WS_VISIBLE | WS_DISABLED,
        0, 0, 1, 1,
        parentWindow_,
        nullptr,
        GetModuleHandleW(nullptr),
        this);
    if (window_ == nullptr)
    {
        parentWindow_ = nullptr;
        Disable(L"Desktop mirror could not create its BF1942 child canvas; the headset presentation remains active.");
        return false;
    }
    UpdateWindowBounds();
    WriteLog(L"Desktop mirror attached to the BF1942 window. It presents each accepted BFVR source frame without blocking the headset frame path.");
    return true;
}

bool DesktopMirror::EnsureSwapchain()
{
    RECT rectangle = {};
    if (!GetClientRect(parentWindow_, &rectangle))
    {
        return false;
    }
    const UINT parentWidth = static_cast<UINT>(
        std::max<LONG>(rectangle.right - rectangle.left, 1));
    const UINT parentHeight = static_cast<UINT>(
        std::max<LONG>(rectangle.bottom - rectangle.top, 1));
    if (swapchain_ != nullptr && targetView_ != nullptr &&
        bufferWidth_ == parentWidth && bufferHeight_ == parentHeight)
    {
        return true;
    }

    // Render at the BF1942 client size rather than into a smaller buffer that
    // DXGI subsequently stretches. Recreate only when the client size changes;
    // ordinary frames continue to reuse the existing flip-model swapchain.
    ReleaseSwapchain();
    bufferWidth_ = parentWidth;
    bufferHeight_ = parentHeight;

    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory2* factory = nullptr;
    HRESULT result = device_->QueryInterface(
        __uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));
    if (SUCCEEDED(result))
    {
        result = dxgiDevice->GetAdapter(&adapter);
    }
    if (SUCCEEDED(result))
    {
        result = adapter->GetParent(
            __uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory));
    }
    DXGI_SWAP_CHAIN_DESC1 description = {};
    description.Width = bufferWidth_;
    description.Height = bufferHeight_;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2;
    description.Scaling = DXGI_SCALING_STRETCH;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    if (SUCCEEDED(result))
    {
        result = factory->CreateSwapChainForHwnd(
            device_, window_, &description, nullptr, nullptr, &swapchain_);
    }
    if (SUCCEEDED(result))
    {
        factory->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER);
        ID3D11Texture2D* backbuffer = nullptr;
        result = swapchain_->GetBuffer(
            0, __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(&backbuffer));
        if (SUCCEEDED(result))
        {
            result = device_->CreateRenderTargetView(backbuffer, nullptr, &targetView_);
        }
        if (backbuffer != nullptr)
        {
            backbuffer->Release();
        }
    }
    if (factory != nullptr)
    {
        factory->Release();
    }
    if (adapter != nullptr)
    {
        adapter->Release();
    }
    if (dxgiDevice != nullptr)
    {
        dxgiDevice->Release();
    }
    if (FAILED(result))
    {
        Disable(L"Desktop mirror could not create its D3D11 preview swapchain; the headset presentation remains active.");
        return false;
    }
    return true;
}

bool DesktopMirror::EnsureSourceViews(const OpenXRPresentationTextures& textures)
{
    if (worldTexture_ == textures.rightWorld && uiTexture_ == textures.ref2Ui &&
        worldView_ != nullptr && uiView_ != nullptr)
    {
        return true;
    }
    ReleaseSourceViews();
    HRESULT result = device_->CreateShaderResourceView(
        textures.rightWorld, nullptr, &worldView_);
    if (SUCCEEDED(result))
    {
        result = device_->CreateShaderResourceView(textures.ref2Ui, nullptr, &uiView_);
    }
    if (FAILED(result))
    {
        ReleaseSourceViews();
        Disable(L"Desktop mirror could not create its source views; the headset presentation remains active.");
        return false;
    }
    worldTexture_ = textures.rightWorld;
    uiTexture_ = textures.ref2Ui;
    return true;
}

bool DesktopMirror::EnsureQuickMenuViews(
    const OpenXRQuickMenuMirrorState& quickMenu)
{
    if (quickMenuTexture_ == quickMenu.menuTexture &&
        quickMenuUtilityTexture_ == quickMenu.utilityTexture &&
        quickMenuCommandTexture_ == quickMenu.commandTexture &&
        quickMenuCursorTexture_ == quickMenu.cursorTexture &&
        quickMenuView_ != nullptr &&
        (quickMenu.utilityTexture == nullptr ||
         quickMenuUtilityView_ != nullptr) &&
        (quickMenu.commandTexture == nullptr ||
         quickMenuCommandView_ != nullptr) &&
        (!quickMenu.pointerVisible || quickMenuCursorView_ != nullptr))
    {
        return true;
    }

    ReleaseQuickMenuViews();
    HRESULT result = quickMenu.menuTexture == nullptr
        ? E_INVALIDARG
        : device_->CreateShaderResourceView(
            quickMenu.menuTexture,
            nullptr,
            &quickMenuView_);
    if (SUCCEEDED(result))
    {
        result = quickMenu.utilityTexture == nullptr
            ? S_OK
            : device_->CreateShaderResourceView(
                quickMenu.utilityTexture,
                nullptr,
                &quickMenuUtilityView_);
    }
    if (SUCCEEDED(result))
    {
        result = quickMenu.commandTexture == nullptr
            ? S_OK
            : device_->CreateShaderResourceView(
                quickMenu.commandTexture,
                nullptr,
                &quickMenuCommandView_);
    }
    if (SUCCEEDED(result) && quickMenu.cursorTexture != nullptr)
    {
        result = device_->CreateShaderResourceView(
            quickMenu.cursorTexture,
            nullptr,
            &quickMenuCursorView_);
    }
    if (FAILED(result))
    {
        ReleaseQuickMenuViews();
        if (!quickMenuMirrorFailureReported_)
        {
            quickMenuMirrorFailureReported_ = true;
            WriteLog(L"Desktop mirror could not create Quick Menu source views; the headset menu and ordinary right-eye preview remain active.");
        }
        return false;
    }
    quickMenuTexture_ = quickMenu.menuTexture;
    quickMenuUtilityTexture_ = quickMenu.utilityTexture;
    quickMenuCommandTexture_ = quickMenu.commandTexture;
    quickMenuCursorTexture_ = quickMenu.cursorTexture;
    return true;
}

bool DesktopMirror::DrawQuickMenuQuad(
    const OpenXRPresentationPose& pose,
    float widthMeters,
    float heightMeters,
    ID3D11ShaderResourceView* textureView,
    bool sourceIsSrgb,
    const OpenXRPresentationView& rightEyeView,
    const float sourceScale[2],
    const float sourceOffset[2])
{
    if (textureView == nullptr || sourceScale == nullptr ||
        sourceOffset == nullptr)
    {
        return false;
    }
    stereo::QuickMenuMirrorView eye = {};
    eye.pose = ToStereoPose(rightEyeView.pose);
    eye.angleLeft = rightEyeView.fov.angleLeft;
    eye.angleRight = rightEyeView.fov.angleRight;
    eye.angleUp = rightEyeView.fov.angleUp;
    eye.angleDown = rightEyeView.fov.angleDown;
    const stereo::QuickMenuMirrorCrop crop = {
        sourceScale[0],
        sourceScale[1],
        sourceOffset[0],
        sourceOffset[1]};
    std::array<stereo::QuickMenuMirrorVertex, 4> vertices = {};
    if (!stereo::ProjectQuickMenuQuadToMirror(
            ToStereoPose(pose),
            widthMeters,
            heightMeters,
            eye,
            crop,
            vertices))
    {
        return false;
    }

    QuickMenuDrawConfiguration configuration = {};
    for (std::size_t index = 0; index < vertices.size(); ++index)
    {
        configuration.clipPositions[index][0] = vertices[index].clipX;
        configuration.clipPositions[index][1] = vertices[index].clipY;
        configuration.clipPositions[index][2] = vertices[index].clipZ;
        configuration.clipPositions[index][3] = vertices[index].clipW;
    }
    configuration.convertFromLinear = sourceIsSrgb ? 1.0F : 0.0F;
    context_->UpdateSubresource(
        quickMenuConfiguration_,
        0,
        nullptr,
        &configuration,
        0,
        0);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context_->VSSetShader(quickMenuVertexShader_, nullptr, 0);
    context_->VSSetConstantBuffers(0, 1, &quickMenuConfiguration_);
    context_->PSSetShader(quickMenuPixelShader_, nullptr, 0);
    context_->PSSetConstantBuffers(0, 1, &quickMenuConfiguration_);
    context_->PSSetSamplers(0, 1, &sampler_);
    context_->PSSetShaderResources(0, 1, &textureView);
    context_->Draw(4, 0);
    ID3D11ShaderResourceView* nullView = nullptr;
    context_->PSSetShaderResources(0, 1, &nullView);
    return true;
}

bool DesktopMirror::CreatePipeline()
{
    if (vertexShader_ != nullptr && pixelShader_ != nullptr &&
        quickMenuVertexShader_ != nullptr &&
        quickMenuPixelShader_ != nullptr && sampler_ != nullptr &&
        cropConfiguration_ != nullptr &&
        quickMenuConfiguration_ != nullptr &&
        quickMenuBlendState_ != nullptr)
    {
        return true;
    }
    ID3DBlob* vertexBytecode = nullptr;
    ID3DBlob* pixelBytecode = nullptr;
    ID3DBlob* quickMenuVertexBytecode = nullptr;
    ID3DBlob* quickMenuPixelBytecode = nullptr;
    HRESULT result = E_FAIL;
    if (CompileShader(kVertexShaderSource, "vs_4_0", &vertexBytecode))
    {
        result = device_->CreateVertexShader(
            vertexBytecode->GetBufferPointer(), vertexBytecode->GetBufferSize(),
            nullptr, &vertexShader_);
    }
    if (SUCCEEDED(result))
    {
        result = CompileShader(
            kPixelShaderSource,
            "ps_4_0",
            &pixelBytecode)
            ? device_->CreatePixelShader(
                pixelBytecode->GetBufferPointer(),
                pixelBytecode->GetBufferSize(),
                nullptr,
                &pixelShader_)
            : E_FAIL;
    }
    if (SUCCEEDED(result))
    {
        result = CompileShader(
            kQuickMenuVertexShaderSource,
            "vs_4_0",
            &quickMenuVertexBytecode)
            ? device_->CreateVertexShader(
                quickMenuVertexBytecode->GetBufferPointer(),
                quickMenuVertexBytecode->GetBufferSize(),
                nullptr,
                &quickMenuVertexShader_)
            : E_FAIL;
    }
    if (SUCCEEDED(result))
    {
        result = CompileShader(
            kQuickMenuPixelShaderSource,
            "ps_4_0",
            &quickMenuPixelBytecode)
            ? device_->CreatePixelShader(
                quickMenuPixelBytecode->GetBufferPointer(),
                quickMenuPixelBytecode->GetBufferSize(),
                nullptr,
                &quickMenuPixelShader_)
            : E_FAIL;
    }
    if (SUCCEEDED(result))
    {
        D3D11_SAMPLER_DESC description = {};
        description.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        description.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        description.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        description.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        description.MaxLOD = D3D11_FLOAT32_MAX;
        result = device_->CreateSamplerState(&description, &sampler_);
    }
    if (SUCCEEDED(result))
    {
        D3D11_BUFFER_DESC description = {};
        description.ByteWidth = sizeof(float) * 4;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        result = device_->CreateBuffer(&description, nullptr, &cropConfiguration_);
    }
    if (SUCCEEDED(result))
    {
        D3D11_BUFFER_DESC description = {};
        description.ByteWidth = sizeof(QuickMenuDrawConfiguration);
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        result = device_->CreateBuffer(
            &description,
            nullptr,
            &quickMenuConfiguration_);
    }
    if (SUCCEEDED(result))
    {
        D3D11_BLEND_DESC description = {};
        D3D11_RENDER_TARGET_BLEND_DESC& target =
            description.RenderTarget[0];
        target.BlendEnable = TRUE;
        target.SrcBlend = D3D11_BLEND_ONE;
        target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        target.BlendOp = D3D11_BLEND_OP_ADD;
        target.SrcBlendAlpha = D3D11_BLEND_ONE;
        target.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        result = device_->CreateBlendState(
            &description,
            &quickMenuBlendState_);
    }
    if (quickMenuPixelBytecode != nullptr)
    {
        quickMenuPixelBytecode->Release();
    }
    if (quickMenuVertexBytecode != nullptr)
    {
        quickMenuVertexBytecode->Release();
    }
    if (pixelBytecode != nullptr)
    {
        pixelBytecode->Release();
    }
    if (vertexBytecode != nullptr)
    {
        vertexBytecode->Release();
    }
    if (FAILED(result))
    {
        Disable(L"Desktop mirror could not create its composite shader; the headset presentation remains active.");
        return false;
    }
    return true;
}

void DesktopMirror::UpdateWindowBounds()
{
    RECT rectangle = {};
    if (parentWindow_ == nullptr || !GetClientRect(parentWindow_, &rectangle))
    {
        return;
    }
    SetWindowPos(
        window_, HWND_TOP, 0, 0,
        std::max<LONG>(rectangle.right, 1), std::max<LONG>(rectangle.bottom, 1),
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void DesktopMirror::ReleaseSourceViews()
{
    if (uiView_ != nullptr)
    {
        uiView_->Release();
        uiView_ = nullptr;
    }
    if (worldView_ != nullptr)
    {
        worldView_->Release();
        worldView_ = nullptr;
    }
    worldTexture_ = nullptr;
    uiTexture_ = nullptr;
}

void DesktopMirror::ReleaseQuickMenuViews()
{
    if (quickMenuCursorView_ != nullptr)
    {
        quickMenuCursorView_->Release();
        quickMenuCursorView_ = nullptr;
    }
    if (quickMenuUtilityView_ != nullptr)
    {
        quickMenuUtilityView_->Release();
        quickMenuUtilityView_ = nullptr;
    }
    if (quickMenuCommandView_ != nullptr)
    {
        quickMenuCommandView_->Release();
        quickMenuCommandView_ = nullptr;
    }
    if (quickMenuView_ != nullptr)
    {
        quickMenuView_->Release();
        quickMenuView_ = nullptr;
    }
    quickMenuTexture_ = nullptr;
    quickMenuUtilityTexture_ = nullptr;
    quickMenuCommandTexture_ = nullptr;
    quickMenuCursorTexture_ = nullptr;
}

void DesktopMirror::ReleaseSwapchain()
{
    if (targetView_ != nullptr)
    {
        targetView_->Release();
        targetView_ = nullptr;
    }
    if (swapchain_ != nullptr)
    {
        swapchain_->Release();
        swapchain_ = nullptr;
    }
}

void DesktopMirror::Disable(const wchar_t* message)
{
    if (!permanentlyDisabled_)
    {
        WriteLog(message);
    }
    permanentlyDisabled_ = true;
    if (window_ != nullptr)
    {
        ShowWindow(window_, SW_HIDE);
    }
}

void DesktopMirror::WriteLog(const wchar_t* message) const
{
    if (logCallback_ != nullptr)
    {
        logCallback_(logContext_, message);
    }
}
} // namespace bfvr
