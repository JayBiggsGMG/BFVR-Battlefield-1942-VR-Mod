#include "client/D3D8WorldCrosshairRenderer.h"

#include "client/CrosshairOverlay.h"
#include "stereo/WorldCrosshairMath.h"

#include "d3d8.hpp"

#include <windows.h>
#include <wincodec.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace
{

constexpr std::size_t kIUnknownReleaseSlot = 2;
constexpr std::size_t kDeviceCreateTextureSlot = 20;
constexpr std::size_t kDeviceSetRenderTargetSlot = 31;
constexpr std::size_t kDeviceGetRenderTargetSlot = 32;
constexpr std::size_t kDeviceGetDepthStencilSurfaceSlot = 33;
constexpr std::size_t kDeviceBeginSceneSlot = 34;
constexpr std::size_t kDeviceEndSceneSlot = 35;
constexpr std::size_t kDeviceSetViewportSlot = 40;
constexpr std::size_t kDeviceGetViewportSlot = 41;
constexpr std::size_t kDeviceSetRenderStateSlot = 50;
constexpr std::size_t kDeviceGetRenderStateSlot = 51;
constexpr std::size_t kDeviceGetTextureSlot = 60;
constexpr std::size_t kDeviceSetTextureSlot = 61;
constexpr std::size_t kDeviceGetTextureStageStateSlot = 62;
constexpr std::size_t kDeviceSetTextureStageStateSlot = 63;
constexpr std::size_t kDeviceDrawPrimitiveUpSlot = 72;
constexpr std::size_t kDeviceSetVertexShaderSlot = 76;
constexpr std::size_t kDeviceGetVertexShaderSlot = 77;
constexpr std::size_t kDeviceSetPixelShaderSlot = 88;
constexpr std::size_t kDeviceGetPixelShaderSlot = 89;
constexpr DWORD kD3DFormatA8R8G8B8 = 21;
constexpr DWORD kD3DPoolManaged = 1;
constexpr DWORD kD3DPrimitiveTriangleStrip = 5;
constexpr DWORD kD3DFvfXyzRhwTex1 = 0x104;
constexpr DWORD kD3DRenderStateZEnable = 7;
constexpr DWORD kD3DRenderStateZWriteEnable = 14;
constexpr DWORD kD3DRenderStateAlphaTestEnable = 15;
constexpr DWORD kD3DRenderStateSourceBlend = 19;
constexpr DWORD kD3DRenderStateDestinationBlend = 20;
constexpr DWORD kD3DRenderStateCullMode = 22;
constexpr DWORD kD3DRenderStateAlphaBlendEnable = 27;
constexpr DWORD kD3DRenderStateFogEnable = 28;
constexpr DWORD kD3DRenderStateLighting = 137;
constexpr DWORD kD3DBlendOne = 2;
constexpr DWORD kD3DBlendInverseSourceAlpha = 6;
constexpr DWORD kD3DCullNone = 1;
constexpr DWORD kD3DTextureStageColorOp = 1;
constexpr DWORD kD3DTextureStageColorArg1 = 2;
constexpr DWORD kD3DTextureStageAlphaOp = 4;
constexpr DWORD kD3DTextureStageAlphaArg1 = 5;
constexpr DWORD kD3DTextureStageAddressU = 13;
constexpr DWORD kD3DTextureStageAddressV = 14;
constexpr DWORD kD3DTextureStageMagFilter = 16;
constexpr DWORD kD3DTextureStageMinFilter = 17;
constexpr DWORD kD3DTextureStageMipFilter = 18;
constexpr DWORD kD3DTextureOpDisable = 1;
constexpr DWORD kD3DTextureOpSelectArg1 = 2;
constexpr DWORD kD3DTextureArgumentTexture = 2;
constexpr DWORD kD3DTextureAddressClamp = 3;
constexpr DWORD kD3DTextureFilterNone = 0;
constexpr DWORD kD3DTextureFilterLinear = 2;
constexpr UINT kExpectedArtSize = 64;
constexpr wchar_t kCrosshairName[] = L"Crosshair.png";
constexpr wchar_t kHitMarkerName[] = L"HitMarker.png";
static_assert(
    sizeof(D3DLOCKED_RECT) == sizeof(void*) * 2,
    "The pinned D3D8 texture-lock ABI changed unexpectedly.");

using ReleaseFn = ULONG(STDMETHODCALLTYPE*)(void* value);
using CreateTextureFn = HRESULT(WINAPI*)(
    void* device,
    UINT width,
    UINT height,
    UINT levels,
    DWORD usage,
    DWORD format,
    DWORD pool,
    void** texture);
using SetRenderTargetFn = HRESULT(WINAPI*)(void*, void*, void*);
using GetRenderTargetFn = HRESULT(WINAPI*)(void*, void**);
using GetDepthStencilSurfaceFn = HRESULT(WINAPI*)(void*, void**);
using BeginSceneFn = HRESULT(WINAPI*)(void*);
using EndSceneFn = HRESULT(WINAPI*)(void*);
using SetViewportFn = HRESULT(WINAPI*)(
    void*, const bfvr::d3d8probe::D3DViewport*);
using GetViewportFn = HRESULT(WINAPI*)(
    void*, bfvr::d3d8probe::D3DViewport*);
using SetRenderStateFn = HRESULT(WINAPI*)(void*, DWORD, DWORD);
using GetRenderStateFn = HRESULT(WINAPI*)(void*, DWORD, DWORD*);
using GetTextureFn = HRESULT(WINAPI*)(void*, DWORD, void**);
using SetTextureFn = HRESULT(WINAPI*)(void*, DWORD, void*);
using GetTextureStageStateFn = HRESULT(WINAPI*)(
    void*, DWORD, DWORD, DWORD*);
using SetTextureStageStateFn = HRESULT(WINAPI*)(
    void*, DWORD, DWORD, DWORD);
using DrawPrimitiveUpFn = HRESULT(WINAPI*)(
    void*, DWORD, UINT, const void*, UINT);
using SetVertexShaderFn = HRESULT(WINAPI*)(void*, DWORD);
using GetVertexShaderFn = HRESULT(WINAPI*)(void*, DWORD*);
using SetPixelShaderFn = HRESULT(WINAPI*)(void*, DWORD);
using GetPixelShaderFn = HRESULT(WINAPI*)(void*, DWORD*);

struct DeviceApi
{
    CreateTextureFn createTexture = nullptr;
    SetRenderTargetFn setRenderTarget = nullptr;
    GetRenderTargetFn getRenderTarget = nullptr;
    GetDepthStencilSurfaceFn getDepthStencilSurface = nullptr;
    BeginSceneFn beginScene = nullptr;
    EndSceneFn endScene = nullptr;
    SetViewportFn setViewport = nullptr;
    GetViewportFn getViewport = nullptr;
    SetRenderStateFn setRenderState = nullptr;
    GetRenderStateFn getRenderState = nullptr;
    GetTextureFn getTexture = nullptr;
    SetTextureFn setTexture = nullptr;
    GetTextureStageStateFn getTextureStageState = nullptr;
    SetTextureStageStateFn setTextureStageState = nullptr;
    DrawPrimitiveUpFn drawPrimitiveUp = nullptr;
    SetVertexShaderFn setVertexShader = nullptr;
    GetVertexShaderFn getVertexShader = nullptr;
    SetPixelShaderFn setPixelShader = nullptr;
    GetPixelShaderFn getPixelShader = nullptr;
};

struct Image
{
    UINT width = 0;
    UINT height = 0;
    std::vector<std::uint32_t> pixels;
};

struct RenderStateValue
{
    DWORD state = 0;
    DWORD value = 0;
};

struct TextureStageStateValue
{
    DWORD stage = 0;
    DWORD state = 0;
    DWORD value = 0;
};

struct SavedState
{
    void* colorTarget = nullptr;
    void* depthTarget = nullptr;
    void* textures[2] = {};
    bfvr::d3d8probe::D3DViewport viewport = {};
    DWORD vertexShader = 0;
    DWORD pixelShader = 0;
    std::array<RenderStateValue, 9> renderStates = {{
        {kD3DRenderStateZEnable, 0},
        {kD3DRenderStateAlphaTestEnable, 0},
        {kD3DRenderStateSourceBlend, 0},
        {kD3DRenderStateDestinationBlend, 0},
        {kD3DRenderStateCullMode, 0},
        {kD3DRenderStateAlphaBlendEnable, 0},
        {kD3DRenderStateFogEnable, 0},
        {kD3DRenderStateLighting, 0},
        {kD3DRenderStateZWriteEnable, 0}}};
    std::array<TextureStageStateValue, 10> textureStageStates = {{
        {0, kD3DTextureStageColorOp, 0},
        {0, kD3DTextureStageColorArg1, 0},
        {0, kD3DTextureStageAlphaOp, 0},
        {0, kD3DTextureStageAlphaArg1, 0},
        {0, kD3DTextureStageAddressU, 0},
        {0, kD3DTextureStageAddressV, 0},
        {0, kD3DTextureStageMagFilter, 0},
        {0, kD3DTextureStageMinFilter, 0},
        {0, kD3DTextureStageMipFilter, 0},
        {1, kD3DTextureStageColorOp, 0}}};
};

struct Vertex
{
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float rhw = 1.0F;
    float u = 0.0F;
    float v = 0.0F;
};

Image g_crosshairImage = {};
Image g_hitMarkerImage = {};
void* g_crosshairTexture = nullptr;
void* g_hitMarkerTexture = nullptr;
void* g_textureDevice = nullptr;
void (*g_appendLog)(const wchar_t* message) = nullptr;
volatile LONG g_ready = 0;
volatile LONG g_firstRenderLogged = 0;
volatile LONG g_firstFailureLogged = 0;
int g_moduleAnchor = 0;

template <typename T>
void ReleaseInterface(T*& value) noexcept
{
    if (value != nullptr)
    {
        value->Release();
        value = nullptr;
    }
}

void ReleaseUnknown(void*& value) noexcept
{
    if (value == nullptr)
    {
        return;
    }
    auto** const vtable = *reinterpret_cast<void***>(value);
    const auto release = vtable == nullptr
        ? nullptr
        : reinterpret_cast<ReleaseFn>(vtable[kIUnknownReleaseSlot]);
    if (release != nullptr)
    {
        release(value);
    }
    value = nullptr;
}

void WriteLog(const wchar_t* format, ...) noexcept
{
    if (g_appendLog == nullptr || format == nullptr)
    {
        return;
    }
    std::array<wchar_t, 1000> message = {};
    va_list arguments;
    va_start(arguments, format);
    _vsnwprintf_s(
        message.data(),
        message.size(),
        _TRUNCATE,
        format,
        arguments);
    va_end(arguments);
    g_appendLog(message.data());
}

std::wstring ParentDirectory(const std::wstring& path)
{
    const auto separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos
        ? std::wstring{}
        : path.substr(0, separator);
}

std::wstring JoinPath(const std::wstring& directory, const wchar_t* child)
{
    if (directory.empty() || child == nullptr || child[0] == L'\0')
    {
        return {};
    }
    std::wstring result = directory;
    if (result.back() != L'\\' && result.back() != L'/')
    {
        result.push_back(L'\\');
    }
    result.append(child);
    return result;
}

std::wstring ModuleDirectory(HMODULE module)
{
    std::array<wchar_t, 32768> path = {};
    const DWORD length = GetModuleFileNameW(
        module,
        path.data(),
        static_cast<DWORD>(path.size()));
    return length == 0 || length >= path.size()
        ? std::wstring{}
        : ParentDirectory(std::wstring(path.data(), length));
}

bool IsDirectory(const std::wstring& path) noexcept
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::wstring FindAssetsDirectory()
{
    HMODULE clientModule = nullptr;
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&g_moduleAnchor),
            &clientModule))
    {
        const std::wstring besideClient = JoinPath(
            ModuleDirectory(clientModule),
            L"assets");
        if (IsDirectory(besideClient))
        {
            return besideClient;
        }
    }
    const std::wstring sourceAssets = JoinPath(
        ModuleDirectory(GetModuleHandleW(nullptr)),
        L"BFVR\\assets");
    return IsDirectory(sourceAssets) ? sourceAssets : std::wstring{};
}

bool LoadPng(IWICImagingFactory* factory, const std::wstring& path, Image& image)
{
    image = {};
    if (factory == nullptr)
    {
        return false;
    }
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    HRESULT status = factory->CreateDecoderFromFilename(
        path.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,
        &decoder);
    if (SUCCEEDED(status))
    {
        status = decoder->GetFrame(0, &frame);
    }
    if (SUCCEEDED(status))
    {
        status = factory->CreateFormatConverter(&converter);
    }
    if (SUCCEEDED(status))
    {
        status = converter->Initialize(
            frame,
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom);
    }
    UINT width = 0;
    UINT height = 0;
    if (SUCCEEDED(status))
    {
        status = converter->GetSize(&width, &height);
    }
    const std::uint64_t pixelCount =
        static_cast<std::uint64_t>(width) * height;
    if (SUCCEEDED(status) &&
        (width == 0 || height == 0 ||
         pixelCount > std::numeric_limits<std::size_t>::max() /
             sizeof(std::uint32_t) ||
         pixelCount > std::numeric_limits<UINT>::max() /
             sizeof(std::uint32_t) ||
         width > std::numeric_limits<UINT>::max() /
             sizeof(std::uint32_t)))
    {
        status = E_INVALIDARG;
    }
    std::vector<std::uint32_t> pixels;
    if (SUCCEEDED(status))
    {
        pixels.resize(static_cast<std::size_t>(pixelCount));
        status = converter->CopyPixels(
            nullptr,
            width * sizeof(std::uint32_t),
            static_cast<UINT>(pixels.size() * sizeof(std::uint32_t)),
            reinterpret_cast<BYTE*>(pixels.data()));
    }
    ReleaseInterface(converter);
    ReleaseInterface(frame);
    ReleaseInterface(decoder);
    if (FAILED(status))
    {
        return false;
    }
    image = {width, height, std::move(pixels)};
    return true;
}

template <typename Function>
Function DeviceMethod(void* device, std::size_t slot) noexcept
{
    if (device == nullptr)
    {
        return nullptr;
    }
    auto** const vtable = *reinterpret_cast<void***>(device);
    return vtable == nullptr
        ? nullptr
        : reinterpret_cast<Function>(vtable[slot]);
}

bool ResolveApi(void* device, DeviceApi& api) noexcept
{
    api = {
        DeviceMethod<CreateTextureFn>(device, kDeviceCreateTextureSlot),
        DeviceMethod<SetRenderTargetFn>(device, kDeviceSetRenderTargetSlot),
        DeviceMethod<GetRenderTargetFn>(device, kDeviceGetRenderTargetSlot),
        DeviceMethod<GetDepthStencilSurfaceFn>(
            device, kDeviceGetDepthStencilSurfaceSlot),
        DeviceMethod<BeginSceneFn>(device, kDeviceBeginSceneSlot),
        DeviceMethod<EndSceneFn>(device, kDeviceEndSceneSlot),
        DeviceMethod<SetViewportFn>(device, kDeviceSetViewportSlot),
        DeviceMethod<GetViewportFn>(device, kDeviceGetViewportSlot),
        DeviceMethod<SetRenderStateFn>(device, kDeviceSetRenderStateSlot),
        DeviceMethod<GetRenderStateFn>(device, kDeviceGetRenderStateSlot),
        DeviceMethod<GetTextureFn>(device, kDeviceGetTextureSlot),
        DeviceMethod<SetTextureFn>(device, kDeviceSetTextureSlot),
        DeviceMethod<GetTextureStageStateFn>(
            device, kDeviceGetTextureStageStateSlot),
        DeviceMethod<SetTextureStageStateFn>(
            device, kDeviceSetTextureStageStateSlot),
        DeviceMethod<DrawPrimitiveUpFn>(device, kDeviceDrawPrimitiveUpSlot),
        DeviceMethod<SetVertexShaderFn>(device, kDeviceSetVertexShaderSlot),
        DeviceMethod<GetVertexShaderFn>(device, kDeviceGetVertexShaderSlot),
        DeviceMethod<SetPixelShaderFn>(device, kDeviceSetPixelShaderSlot),
        DeviceMethod<GetPixelShaderFn>(device, kDeviceGetPixelShaderSlot)};
    return api.createTexture != nullptr && api.setRenderTarget != nullptr &&
        api.getRenderTarget != nullptr &&
        api.getDepthStencilSurface != nullptr && api.beginScene != nullptr &&
        api.endScene != nullptr && api.setViewport != nullptr &&
        api.getViewport != nullptr && api.setRenderState != nullptr &&
        api.getRenderState != nullptr && api.getTexture != nullptr &&
        api.setTexture != nullptr && api.getTextureStageState != nullptr &&
        api.setTextureStageState != nullptr && api.drawPrimitiveUp != nullptr &&
        api.setVertexShader != nullptr && api.getVertexShader != nullptr &&
        api.setPixelShader != nullptr && api.getPixelShader != nullptr;
}

bool UploadTexture(
    const DeviceApi& api,
    void* device,
    const Image& image,
    void*& texture) noexcept
{
    texture = nullptr;
    if (image.width != kExpectedArtSize || image.height != kExpectedArtSize ||
        image.pixels.size() !=
            static_cast<std::size_t>(image.width) * image.height ||
        FAILED(api.createTexture(
            device,
            image.width,
            image.height,
            1,
            0,
            kD3DFormatA8R8G8B8,
            kD3DPoolManaged,
            &texture)) ||
        texture == nullptr)
    {
        return false;
    }
    // Use the pinned D3D8 interface definition here instead of a raw vtable
    // slot. IDirect3DTexture8 inherits the three IDirect3DBaseTexture8 methods,
    // so LockRect/UnlockRect are slots 16/17, not 13/14. The typed dispatch
    // keeps that inheritance layout and its x86 stdcall cleanup authoritative.
    auto* const d3dTexture = static_cast<IDirect3DTexture8*>(texture);
    D3DLOCKED_RECT locked = {};
    if (FAILED(d3dTexture->LockRect(0, &locked, nullptr, 0)) ||
        locked.pBits == nullptr || locked.Pitch <
            static_cast<INT>(image.width * sizeof(std::uint32_t)))
    {
        ReleaseUnknown(texture);
        return false;
    }
    auto* destination = static_cast<std::byte*>(locked.pBits);
    for (UINT row = 0; row < image.height; ++row)
    {
        std::memcpy(
            destination + static_cast<std::size_t>(row) * locked.Pitch,
            image.pixels.data() + static_cast<std::size_t>(row) * image.width,
            static_cast<std::size_t>(image.width) * sizeof(std::uint32_t));
    }
    const HRESULT unlockStatus = d3dTexture->UnlockRect(0);
    if (FAILED(unlockStatus))
    {
        ReleaseUnknown(texture);
        return false;
    }
    return true;
}

bool EnsureTextures(
    const DeviceApi& api,
    void* device) noexcept
{
    if (g_textureDevice != device)
    {
        ReleaseUnknown(g_crosshairTexture);
        ReleaseUnknown(g_hitMarkerTexture);
        g_textureDevice = device;
    }
    if (g_crosshairTexture != nullptr && g_hitMarkerTexture != nullptr)
    {
        return true;
    }
    ReleaseUnknown(g_crosshairTexture);
    ReleaseUnknown(g_hitMarkerTexture);
    if (!UploadTexture(
            api, device, g_crosshairImage, g_crosshairTexture) ||
        !UploadTexture(
            api, device, g_hitMarkerImage, g_hitMarkerTexture))
    {
        ReleaseUnknown(g_crosshairTexture);
        ReleaseUnknown(g_hitMarkerTexture);
        return false;
    }
    return true;
}

bool SaveDeviceState(
    const DeviceApi& api,
    void* device,
    SavedState& saved) noexcept
{
    saved = {};
    if (FAILED(api.getRenderTarget(device, &saved.colorTarget)) ||
        FAILED(api.getDepthStencilSurface(device, &saved.depthTarget)) ||
        FAILED(api.getViewport(device, &saved.viewport)) ||
        FAILED(api.getVertexShader(device, &saved.vertexShader)) ||
        FAILED(api.getPixelShader(device, &saved.pixelShader)) ||
        FAILED(api.getTexture(device, 0, &saved.textures[0])) ||
        FAILED(api.getTexture(device, 1, &saved.textures[1])))
    {
        ReleaseUnknown(saved.colorTarget);
        ReleaseUnknown(saved.depthTarget);
        ReleaseUnknown(saved.textures[0]);
        ReleaseUnknown(saved.textures[1]);
        return false;
    }
    for (RenderStateValue& state : saved.renderStates)
    {
        if (FAILED(api.getRenderState(device, state.state, &state.value)))
        {
            return false;
        }
    }
    for (TextureStageStateValue& state : saved.textureStageStates)
    {
        if (FAILED(api.getTextureStageState(
                device, state.stage, state.state, &state.value)))
        {
            return false;
        }
    }
    return true;
}

bool ApplyCrosshairState(const DeviceApi& api, void* device) noexcept
{
    return SUCCEEDED(api.setVertexShader(device, kD3DFvfXyzRhwTex1)) &&
        SUCCEEDED(api.setPixelShader(device, 0)) &&
        SUCCEEDED(api.setRenderState(device, kD3DRenderStateZEnable, FALSE)) &&
        SUCCEEDED(api.setRenderState(
            device, kD3DRenderStateAlphaTestEnable, FALSE)) &&
        SUCCEEDED(api.setRenderState(
            device, kD3DRenderStateAlphaBlendEnable, TRUE)) &&
        SUCCEEDED(api.setRenderState(
            device, kD3DRenderStateSourceBlend, kD3DBlendOne)) &&
        SUCCEEDED(api.setRenderState(
            device,
            kD3DRenderStateDestinationBlend,
            kD3DBlendInverseSourceAlpha)) &&
        SUCCEEDED(api.setRenderState(
            device, kD3DRenderStateCullMode, kD3DCullNone)) &&
        SUCCEEDED(api.setRenderState(device, kD3DRenderStateFogEnable, FALSE)) &&
        SUCCEEDED(api.setRenderState(device, kD3DRenderStateLighting, FALSE)) &&
        SUCCEEDED(api.setRenderState(
            device, kD3DRenderStateZWriteEnable, FALSE)) &&
        SUCCEEDED(api.setTextureStageState(
            device, 0, kD3DTextureStageColorOp, kD3DTextureOpSelectArg1)) &&
        SUCCEEDED(api.setTextureStageState(
            device, 0, kD3DTextureStageColorArg1, kD3DTextureArgumentTexture)) &&
        SUCCEEDED(api.setTextureStageState(
            device, 0, kD3DTextureStageAlphaOp, kD3DTextureOpSelectArg1)) &&
        SUCCEEDED(api.setTextureStageState(
            device, 0, kD3DTextureStageAlphaArg1, kD3DTextureArgumentTexture)) &&
        SUCCEEDED(api.setTextureStageState(
            device, 0, kD3DTextureStageAddressU, kD3DTextureAddressClamp)) &&
        SUCCEEDED(api.setTextureStageState(
            device, 0, kD3DTextureStageAddressV, kD3DTextureAddressClamp)) &&
        SUCCEEDED(api.setTextureStageState(
            device, 0, kD3DTextureStageMagFilter, kD3DTextureFilterLinear)) &&
        SUCCEEDED(api.setTextureStageState(
            device, 0, kD3DTextureStageMinFilter, kD3DTextureFilterLinear)) &&
        SUCCEEDED(api.setTextureStageState(
            device, 0, kD3DTextureStageMipFilter, kD3DTextureFilterNone)) &&
        SUCCEEDED(api.setTextureStageState(
            device, 1, kD3DTextureStageColorOp, kD3DTextureOpDisable));
}

bool RestoreDeviceState(
    const DeviceApi& api,
    void* device,
    SavedState& saved) noexcept
{
    bool restored = true;
    restored = SUCCEEDED(api.setTexture(device, 0, saved.textures[0])) && restored;
    restored = SUCCEEDED(api.setTexture(device, 1, saved.textures[1])) && restored;
    for (const TextureStageStateValue& state : saved.textureStageStates)
    {
        restored = SUCCEEDED(api.setTextureStageState(
            device, state.stage, state.state, state.value)) && restored;
    }
    for (const RenderStateValue& state : saved.renderStates)
    {
        restored = SUCCEEDED(api.setRenderState(
            device, state.state, state.value)) && restored;
    }
    restored = SUCCEEDED(api.setVertexShader(device, saved.vertexShader)) && restored;
    restored = SUCCEEDED(api.setPixelShader(device, saved.pixelShader)) && restored;
    restored = SUCCEEDED(api.setRenderTarget(
        device, saved.colorTarget, saved.depthTarget)) && restored;
    restored = SUCCEEDED(api.setViewport(device, &saved.viewport)) && restored;
    ReleaseUnknown(saved.colorTarget);
    ReleaseUnknown(saved.depthTarget);
    ReleaseUnknown(saved.textures[0]);
    ReleaseUnknown(saved.textures[1]);
    return restored;
}

bool DrawLayer(
    const DeviceApi& api,
    void* device,
    void* texture,
    const bfvr::stereo::WorldCrosshairProjection& projection,
    const bfvr::d3d8probe::D3DViewport& viewport) noexcept
{
    if (texture == nullptr)
    {
        return false;
    }
    const float left =
        static_cast<float>(viewport.x) + projection.centerX -
        projection.halfExtentPixels;
    const float top =
        static_cast<float>(viewport.y) + projection.centerY -
        projection.halfExtentPixels;
    const float right =
        static_cast<float>(viewport.x) + projection.centerX +
        projection.halfExtentPixels;
    const float bottom =
        static_cast<float>(viewport.y) + projection.centerY +
        projection.halfExtentPixels;
    const std::array<Vertex, 4> vertices = {{
        {left, top, projection.depth, 1.0F, 0.0F, 0.0F},
        {right, top, projection.depth, 1.0F, 1.0F, 0.0F},
        {left, bottom, projection.depth, 1.0F, 0.0F, 1.0F},
        {right, bottom, projection.depth, 1.0F, 1.0F, 1.0F}}};
    return SUCCEEDED(api.setTexture(device, 0, texture)) &&
        SUCCEEDED(api.drawPrimitiveUp(
            device,
            kD3DPrimitiveTriangleStrip,
            2,
            vertices.data(),
            sizeof(Vertex)));
}

} // namespace

namespace bfvr
{

void InitializeD3D8WorldCrosshairRenderer(
    void (*appendLog)(const wchar_t* message))
{
    ShutdownD3D8WorldCrosshairRenderer();
    g_appendLog = appendLog;
    const std::wstring assetsDirectory = FindAssetsDirectory();
    if (assetsDirectory.empty())
    {
        WriteLog(
            L"3D crosshair is unavailable: BFVR's assets directory was not found.");
        return;
    }
    const HRESULT comStatus = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitializeCom = comStatus == S_OK || comStatus == S_FALSE;
    if (FAILED(comStatus) && comStatus != RPC_E_CHANGED_MODE)
    {
        WriteLog(
            L"3D crosshair could not initialize Windows imaging (HRESULT=0x%08lX).",
            static_cast<unsigned long>(comStatus));
        return;
    }
    IWICImagingFactory* factory = nullptr;
    const HRESULT factoryStatus = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    const bool loaded = SUCCEEDED(factoryStatus) && factory != nullptr &&
        LoadPng(
            factory,
            JoinPath(assetsDirectory, kCrosshairName),
            g_crosshairImage) &&
        LoadPng(
            factory,
            JoinPath(assetsDirectory, kHitMarkerName),
            g_hitMarkerImage);
    ReleaseInterface(factory);
    if (uninitializeCom)
    {
        CoUninitialize();
    }
    if (!loaded || g_crosshairImage.width != kExpectedArtSize ||
        g_crosshairImage.height != kExpectedArtSize ||
        g_hitMarkerImage.width != g_crosshairImage.width ||
        g_hitMarkerImage.height != g_crosshairImage.height)
    {
        g_crosshairImage = {};
        g_hitMarkerImage = {};
        WriteLog(
            L"3D crosshair is unavailable: Crosshair.png and HitMarker.png must both decode as aligned 64x64 premultiplied images in %s.",
            assetsDirectory.c_str());
        return;
    }
    InterlockedExchange(&g_ready, 1);
    WriteLog(
        L"3D crosshair loaded aligned 64x64 Crosshair.png and HitMarker.png layers from %s.",
        assetsDirectory.c_str());
}

void ShutdownD3D8WorldCrosshairRenderer() noexcept
{
    InterlockedExchange(&g_ready, 0);
    ReleaseUnknown(g_crosshairTexture);
    ReleaseUnknown(g_hitMarkerTexture);
    g_textureDevice = nullptr;
    g_crosshairImage = {};
    g_hitMarkerImage = {};
    g_appendLog = nullptr;
    InterlockedExchange(&g_firstRenderLogged, 0);
    InterlockedExchange(&g_firstFailureLogged, 0);
}

bool RenderD3D8WorldCrosshair(
    const D3D8WorldCrosshairRenderFrame& frame) noexcept
{
    if (InterlockedCompareExchange(&g_ready, 0, 0) == 0 ||
        frame.device == nullptr || frame.colorTargets[0] == nullptr ||
        frame.colorTargets[1] == nullptr || frame.depthTargets[0] == nullptr ||
        frame.depthTargets[1] == nullptr || frame.viewport.width == 0 ||
        frame.viewport.height == 0)
    {
        return false;
    }

    WorldCrosshairFrameState crosshair = {};
    if (!ReadWorldCrosshairFrameState(crosshair))
    {
        return false;
    }

    DeviceApi api = {};
    if (!ResolveApi(frame.device, api) ||
        !EnsureTextures(api, frame.device))
    {
        if (InterlockedCompareExchange(&g_firstFailureLogged, 1, 0) == 0)
        {
            WriteLog(
                L"3D crosshair skipped rendering because its D3D8 API or managed textures could not be prepared.");
        }
        return false;
    }

    std::array<stereo::WorldCrosshairProjection, 2> projections = {};
    for (std::size_t eye = 0; eye < projections.size(); ++eye)
    {
        stereo::Matrix4 view = {};
        stereo::Matrix4 projection = {};
        std::memcpy(&view, &frame.eyeViews[eye], sizeof(view));
        std::memcpy(
            &projection,
            &frame.eyeProjections[eye],
            sizeof(projection));
        const auto projected = stereo::ProjectWorldCrosshairEndpoint(
            crosshair.endpoint,
            view,
            projection,
            static_cast<float>(frame.viewport.width),
            static_cast<float>(frame.viewport.height),
            crosshair.angularDiameterDegrees);
        if (!projected.has_value())
        {
            return false;
        }
        projections[eye] = *projected;
    }

    SavedState saved = {};
    if (!SaveDeviceState(api, frame.device, saved))
    {
        ReleaseUnknown(saved.colorTarget);
        ReleaseUnknown(saved.depthTarget);
        ReleaseUnknown(saved.textures[0]);
        ReleaseUnknown(saved.textures[1]);
        return false;
    }

    bool rendered = false;
    const HRESULT beginStatus = api.beginScene(frame.device);
    if (SUCCEEDED(beginStatus) && ApplyCrosshairState(api, frame.device))
    {
        rendered = true;
        for (std::size_t eye = 0; eye < projections.size(); ++eye)
        {
            const bool targetReady = SUCCEEDED(api.setRenderTarget(
                    frame.device,
                    frame.colorTargets[eye],
                    frame.depthTargets[eye])) &&
                SUCCEEDED(api.setViewport(frame.device, &frame.viewport));
            const bool baseDrawn = targetReady && DrawLayer(
                api,
                frame.device,
                g_crosshairTexture,
                projections[eye],
                frame.viewport);
            const bool markerDrawn = !crosshair.hitMarkerVisible ||
                (baseDrawn && DrawLayer(
                    api,
                    frame.device,
                    g_hitMarkerTexture,
                    projections[eye],
                    frame.viewport));
            rendered = rendered && baseDrawn && markerDrawn;
        }
    }
    if (SUCCEEDED(beginStatus))
    {
        rendered = SUCCEEDED(api.endScene(frame.device)) && rendered;
    }
    rendered = RestoreDeviceState(api, frame.device, saved) && rendered;

    if (rendered &&
        InterlockedCompareExchange(&g_firstRenderLogged, 1, 0) == 0)
    {
        WriteLog(
            L"3D crosshair rendered its first stereo world endpoint using the exact per-eye replay transforms; hitMarker=%d. The finite endpoint is a no-hit range fallback and makes no surface-collision claim.",
            crosshair.hitMarkerVisible ? 1 : 0);
    }
    else if (!rendered &&
        InterlockedCompareExchange(&g_firstFailureLogged, 1, 0) == 0)
    {
        WriteLog(
            L"3D crosshair failed one owned-target draw or exact D3D8 state restoration; the frame continues without a reticle.");
    }
    return rendered;
}

} // namespace bfvr
