#include "presenter/SharedTextureConsumer.h"

#include <dxgi1_2.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <iterator>

namespace
{
bool IsSrgbFormat(DXGI_FORMAT format)
{
    return format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
        format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
}

bool ReadWorldFxaaEnabled()
{
    wchar_t value[2] = {};
    const DWORD length = GetEnvironmentVariableW(
        L"BFVR_OPENXR_FXAA",
        value,
        static_cast<DWORD>(std::size(value)));
    // The owner has accepted this quality path. Disable it only for a
    // deliberate measured A/B; it must not silently change visual quality.
    return !(length == 1 && value[0] == L'0');
}

bool IsConvertibleSharedFormat(
    DXGI_FORMAT source,
    DXGI_FORMAT destination)
{
    if (source == destination)
    {
        return true;
    }
    const bool supportedDestination =
        destination == DXGI_FORMAT_B8G8R8A8_UNORM ||
        destination == DXGI_FORMAT_R8G8B8A8_UNORM ||
        destination == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
        destination == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    if (!supportedDestination)
    {
        return false;
    }
    return source == DXGI_FORMAT_R10G10B10A2_UNORM ||
        source == DXGI_FORMAT_R16G16B16A16_FLOAT ||
        source == DXGI_FORMAT_B8G8R8A8_UNORM ||
        source == DXGI_FORMAT_R8G8B8A8_UNORM ||
        source == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
        source == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
}
} // namespace

namespace bfvr::shared
{
SharedTextureConsumer::~SharedTextureConsumer()
{
    Shutdown();
}

bool SharedTextureConsumer::Initialize(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    const SharedTextureDescription* descriptions,
    std::size_t count,
    const PresentationRequirements& destinationRequirements,
    SharedTextureLogCallback logCallback,
    void* logContext)
{
    Shutdown();
    logCallback_ = logCallback;
    logContext_ = logContext;
    if (device == nullptr || context == nullptr || descriptions == nullptr ||
        count < textures_.size())
    {
        WriteLog(L"Shared texture consumer received an invalid device or descriptor set.");
        return false;
    }

    ID3D11Device1* device1 = nullptr;
    HRESULT result = device->QueryInterface(
        __uuidof(ID3D11Device1),
        reinterpret_cast<void**>(&device1));
    if (FAILED(result) || device1 == nullptr)
    {
        WriteLog(
            L"Shared texture consumer requires ID3D11Device1 (HRESULT=0x%08lX).",
            static_cast<unsigned long>(result));
        return false;
    }

    device_ = device;
    device_->AddRef();
    context_ = context;
    context_->AddRef();
    worldFxaaEnabled_ = ReadWorldFxaaEnabled();
    // Bloom is intentionally disabled: it was not perceptible in-headset and
    // adds three half-resolution GPU passes per world eye. Keep the feature
    // code dormant rather than allocating its resources or compiling shaders.
    worldBloomEnabled_ = false;
    worldBloomThreshold_ = 0.0F;
    worldBloomIntensity_ = 0.0F;
    const std::array<UINT, kTextureCount> destinationWidths = {
        destinationRequirements.leftWorldWidth,
        destinationRequirements.rightWorldWidth,
        destinationRequirements.uiWidth};
    const std::array<UINT, kTextureCount> destinationHeights = {
        destinationRequirements.leftWorldHeight,
        destinationRequirements.rightWorldHeight,
        destinationRequirements.uiHeight};
    const DXGI_FORMAT destinationFormat =
        static_cast<DXGI_FORMAT>(destinationRequirements.format);
    bool opened = destinationFormat != DXGI_FORMAT_UNKNOWN;
    for (std::size_t index = 0; index < textures_.size(); ++index)
    {
        requiresLegacyCompletionWait_ =
            requiresLegacyCompletionWait_ ||
            static_cast<SharedTextureTransport>(
                descriptions[index].transport) ==
                SharedTextureTransport::D3D9LegacyHandle;
        opened = opened &&
            OpenTexture(
                device1,
                index,
                descriptions[index],
                destinationWidths[index],
                destinationHeights[index],
                destinationFormat);
        if (!opened)
        {
            break;
        }
    }
    device1->Release();
    if (!opened)
    {
        Shutdown();
        return false;
    }
    if (scalerRequired_ &&
        !scaler_.Initialize(
            device_,
            context_,
            logCallback_,
            logContext_,
            worldBloomEnabled_))
    {
        Shutdown();
        return false;
    }
    if (requiresLegacyCompletionWait_)
    {
        D3D11_QUERY_DESC queryDescription = {};
        queryDescription.Query = D3D11_QUERY_EVENT;
        result = device_->CreateQuery(
            &queryDescription,
            &legacyCompletionQuery_);
        if (FAILED(result) || legacyCompletionQuery_ == nullptr)
        {
            WriteLog(
                L"Shared texture consumer could not create its legacy-handle GPU completion query (HRESULT=0x%08lX).",
                static_cast<unsigned long>(result));
            Shutdown();
            return false;
        }
    }

    if (scalerRequired_)
    {
        WriteLog(
            worldFxaaEnabled_
                ? L"Shared texture consumer opened all three x86-produced resources and enabled x64 aspect-fit conversion with world FXAA; bloom is fully disabled."
                : L"Shared texture consumer opened all three x86-produced resources and enabled x64 aspect-fit conversion with world FXAA disabled by BFVR_OPENXR_FXAA=0; bloom is fully disabled.");
    }
    else
    {
        WriteLog(L"Shared texture consumer opened all three x86-produced resources at their exact destination sizes.");
    }
    return true;
}

bool SharedTextureConsumer::ConsumeFrame()
{
    if (context_ == nullptr)
    {
        return false;
    }

    std::array<bool, kTextureCount> acquired = {};
    for (std::size_t index = 0; index < textures_.size(); ++index)
    {
        if (textures_[index].keyedMutex == nullptr)
        {
            continue;
        }
        const HRESULT result = textures_[index].keyedMutex->AcquireSync(1, 500);
        if (FAILED(result))
        {
            WriteLog(
                L"Shared texture consumer could not acquire slot %zu (HRESULT=0x%08lX).",
                index,
                static_cast<unsigned long>(result));
            for (std::size_t releaseIndex = 0; releaseIndex < index; ++releaseIndex)
            {
                if (acquired[releaseIndex])
                {
                    textures_[releaseIndex].keyedMutex->ReleaseSync(0);
                }
            }
            return false;
        }
        acquired[index] = true;
    }

    bool copied = true;
    for (const Texture& texture : textures_)
    {
        if (texture.requiresScaling)
        {
            copied = scaler_.ScaleAspectFit(
                texture.sharedView,
                texture.sourceWidth,
                texture.sourceHeight,
                texture.localTarget,
                texture.destinationWidth,
                texture.destinationHeight,
                texture.transparentPadding,
                texture.sourceAlreadyLinear,
                texture.applyAntialiasing,
                texture.applyBloom,
                worldBloomThreshold_,
                worldBloomIntensity_) && copied;
        }
        else
        {
            context_->CopyResource(texture.local, texture.shared);
        }
    }
    if (legacyCompletionQuery_ != nullptr)
    {
        context_->End(legacyCompletionQuery_);
    }
    context_->Flush();

    bool completed = true;
    if (legacyCompletionQuery_ != nullptr)
    {
        completed = false;
        const DWORD waitStarted = GetTickCount();
        while (GetTickCount() - waitStarted < 1000)
        {
            const HRESULT result =
                context_->GetData(
                    legacyCompletionQuery_,
                    nullptr,
                    0,
                    0);
            if (result == S_OK)
            {
                completed = true;
                break;
            }
            if (FAILED(result))
            {
                WriteLog(
                    L"Shared texture consumer legacy-handle GPU completion query failed (HRESULT=0x%08lX).",
                    static_cast<unsigned long>(result));
                break;
            }
            Sleep(1);
        }
        if (!completed)
        {
            WriteLog(
                L"Shared texture consumer timed out waiting for legacy-handle GPU reads to complete.");
        }
    }

    bool released = true;
    for (Texture& texture : textures_)
    {
        if (texture.keyedMutex != nullptr)
        {
            released =
                SUCCEEDED(texture.keyedMutex->ReleaseSync(0)) && released;
        }
    }
    return copied && completed && released;
}

OpenXRPresentationTextures SharedTextureConsumer::GetLocalTextures() const noexcept
{
    OpenXRPresentationTextures result = {};
    result.leftWorld = textures_[static_cast<std::size_t>(TextureSlot::LeftWorld)].local;
    result.rightWorld = textures_[static_cast<std::size_t>(TextureSlot::RightWorld)].local;
    result.ref2Ui = textures_[static_cast<std::size_t>(TextureSlot::Ref2Ui)].local;
    return result;
}

bool SharedTextureConsumer::ReadCenterPixels(DWORD* pixels, std::size_t count)
{
    if (pixels == nullptr || count < textures_.size())
    {
        return false;
    }
    bool read = true;
    for (std::size_t index = 0; index < textures_.size(); ++index)
    {
        read = ReadCenterPixel(textures_[index], pixels[index]) && read;
    }
    return read;
}

void SharedTextureConsumer::Shutdown()
{
    scaler_.Shutdown();
    scalerRequired_ = false;
    requiresLegacyCompletionWait_ = false;
    worldFxaaEnabled_ = true;
    worldBloomEnabled_ = false;
    worldBloomThreshold_ = 0.0F;
    worldBloomIntensity_ = 0.0F;
    for (Texture& texture : textures_)
    {
        ReleaseTexture(texture);
    }
    if (legacyCompletionQuery_ != nullptr)
    {
        legacyCompletionQuery_->Release();
        legacyCompletionQuery_ = nullptr;
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
}

bool SharedTextureConsumer::OpenTexture(
    ID3D11Device1* device,
    std::size_t index,
    const SharedTextureDescription& description,
    UINT destinationWidth,
    UINT destinationHeight,
    DXGI_FORMAT destinationFormat)
{
    if (device == nullptr || index >= textures_.size() ||
        description.width == 0 || description.height == 0 ||
        destinationWidth == 0 || destinationHeight == 0 ||
        destinationFormat == DXGI_FORMAT_UNKNOWN ||
        description.format == DXGI_FORMAT_UNKNOWN)
    {
        return false;
    }

    Texture& texture = textures_[index];
    const SharedTextureTransport transport =
        static_cast<SharedTextureTransport>(description.transport);
    HRESULT result = E_INVALIDARG;
    if (transport == SharedTextureTransport::NamedNtHandle &&
        description.name[0] != L'\0')
    {
        result = device->OpenSharedResourceByName(
            description.name,
            DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
            __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(&texture.shared));
        if (SUCCEEDED(result))
        {
            result = texture.shared->QueryInterface(
                __uuidof(IDXGIKeyedMutex),
                reinterpret_cast<void**>(&texture.keyedMutex));
        }
    }
    else if (transport == SharedTextureTransport::D3D9LegacyHandle)
    {
        const HANDLE sharedHandle = LoadLegacySharedHandle(description);
        result = sharedHandle == nullptr
            ? E_INVALIDARG
            : device_->OpenSharedResource(
                sharedHandle,
                __uuidof(ID3D11Texture2D),
                reinterpret_cast<void**>(&texture.shared));
    }

    D3D11_TEXTURE2D_DESC sourceDescription = {};
    if (SUCCEEDED(result))
    {
        texture.shared->GetDesc(&sourceDescription);
        if (sourceDescription.Width != description.width ||
            sourceDescription.Height != description.height ||
            sourceDescription.Format != static_cast<DXGI_FORMAT>(description.format) ||
            sourceDescription.SampleDesc.Count != 1 ||
            sourceDescription.ArraySize != 1)
        {
            result = E_INVALIDARG;
        }
    }
    const DXGI_FORMAT sourceFormat = sourceDescription.Format;
    if (SUCCEEDED(result))
    {
        if (!IsConvertibleSharedFormat(
                sourceFormat,
                destinationFormat))
        {
            result = E_INVALIDARG;
        }
    }
    if (SUCCEEDED(result))
    {
        result = device_->CreateShaderResourceView(
            texture.shared,
            nullptr,
            &texture.sharedView);
    }
    if (SUCCEEDED(result))
    {
        sourceDescription.Usage = D3D11_USAGE_DEFAULT;
        sourceDescription.Width = destinationWidth;
        sourceDescription.Height = destinationHeight;
        sourceDescription.Format = destinationFormat;
        sourceDescription.BindFlags =
            D3D11_BIND_SHADER_RESOURCE |
            D3D11_BIND_RENDER_TARGET;
        sourceDescription.CPUAccessFlags = 0;
        sourceDescription.MiscFlags = 0;
        result = device_->CreateTexture2D(
            &sourceDescription,
            nullptr,
            &texture.local);
    }
    if (SUCCEEDED(result))
    {
        result = device_->CreateRenderTargetView(
            texture.local,
            nullptr,
            &texture.localTarget);
    }
    if (FAILED(result) || texture.local == nullptr)
    {
        WriteLog(
            L"Shared texture consumer could not open slot %zu '%s' (HRESULT=0x%08lX).",
            index,
            description.name,
            static_cast<unsigned long>(result));
        ReleaseTexture(texture);
        return false;
    }
    texture.sourceWidth = description.width;
    texture.sourceHeight = description.height;
    texture.destinationWidth = destinationWidth;
    texture.destinationHeight = destinationHeight;
    texture.requiresScaling =
        texture.sourceWidth != texture.destinationWidth ||
        texture.sourceHeight != texture.destinationHeight ||
        sourceFormat != destinationFormat ||
        !IsSrgbFormat(destinationFormat);
    texture.transparentPadding =
        index == static_cast<std::size_t>(TextureSlot::Ref2Ui);
    texture.sourceAlreadyLinear = IsSrgbFormat(sourceFormat);
    texture.applyAntialiasing =
        worldFxaaEnabled_ &&
        index != static_cast<std::size_t>(TextureSlot::Ref2Ui);
    texture.applyBloom =
        worldBloomEnabled_ &&
        index != static_cast<std::size_t>(TextureSlot::Ref2Ui);
    texture.requiresScaling =
        texture.requiresScaling || texture.applyAntialiasing || texture.applyBloom;
    scalerRequired_ = scalerRequired_ || texture.requiresScaling;
    WriteLog(
        transport == SharedTextureTransport::D3D9LegacyHandle
            ? L"Shared texture consumer opened legacy D3D9 slot %zu handle=%p sourceFormat=%u destinationFormat=%u."
            : L"Shared texture consumer opened named keyed-mutex slot %zu '%s' sourceFormat=%u destinationFormat=%u.",
        index,
        transport == SharedTextureTransport::D3D9LegacyHandle
            ? LoadLegacySharedHandle(description)
            : description.name,
        static_cast<unsigned int>(sourceFormat),
        static_cast<unsigned int>(destinationFormat));
    return true;
}

bool SharedTextureConsumer::ReadCenterPixel(const Texture& texture, DWORD& pixel)
{
    if (device_ == nullptr || context_ == nullptr || texture.local == nullptr)
    {
        return false;
    }

    D3D11_TEXTURE2D_DESC sourceDescription = {};
    texture.local->GetDesc(&sourceDescription);
    D3D11_TEXTURE2D_DESC stagingDescription = {};
    stagingDescription.Width = 1;
    stagingDescription.Height = 1;
    stagingDescription.MipLevels = 1;
    stagingDescription.ArraySize = 1;
    stagingDescription.Format = sourceDescription.Format;
    stagingDescription.SampleDesc.Count = 1;
    stagingDescription.Usage = D3D11_USAGE_STAGING;
    stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    ID3D11Texture2D* staging = nullptr;
    HRESULT result = device_->CreateTexture2D(
        &stagingDescription,
        nullptr,
        &staging);
    if (FAILED(result) || staging == nullptr)
    {
        return false;
    }

    const UINT centerX = sourceDescription.Width / 2;
    const UINT centerY = sourceDescription.Height / 2;
    const D3D11_BOX sourceBox = {
        centerX,
        centerY,
        0,
        centerX + 1,
        centerY + 1,
        1};
    context_->CopySubresourceRegion(staging, 0, 0, 0, 0, texture.local, 0, &sourceBox);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    result = context_->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (SUCCEEDED(result) && mapped.pData != nullptr && mapped.RowPitch >= sizeof(DWORD))
    {
        pixel = *static_cast<const DWORD*>(mapped.pData);
        context_->Unmap(staging, 0);
    }
    staging->Release();
    return SUCCEEDED(result);
}

void SharedTextureConsumer::WriteLog(const wchar_t* format, ...) const
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

void SharedTextureConsumer::ReleaseTexture(Texture& texture)
{
    if (texture.localTarget != nullptr)
    {
        texture.localTarget->Release();
        texture.localTarget = nullptr;
    }
    if (texture.local != nullptr)
    {
        texture.local->Release();
        texture.local = nullptr;
    }
    if (texture.keyedMutex != nullptr)
    {
        texture.keyedMutex->Release();
        texture.keyedMutex = nullptr;
    }
    if (texture.sharedView != nullptr)
    {
        texture.sharedView->Release();
        texture.sharedView = nullptr;
    }
    if (texture.shared != nullptr)
    {
        texture.shared->Release();
        texture.shared = nullptr;
    }
    texture.sourceWidth = 0;
    texture.sourceHeight = 0;
    texture.destinationWidth = 0;
    texture.destinationHeight = 0;
    texture.requiresScaling = false;
    texture.transparentPadding = false;
    texture.sourceAlreadyLinear = false;
    texture.applyAntialiasing = false;
    texture.applyBloom = false;
}
} // namespace bfvr::shared
