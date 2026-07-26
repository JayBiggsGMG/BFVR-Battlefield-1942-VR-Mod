#include "presenter/SharedTextureProducer.h"
#include "presenter/D3DSystemRuntime.h"

#include <dxgi1_2.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <iterator>

namespace
{
bool EqualLuid(const LUID& left, const LUID& right)
{
    return left.HighPart == right.HighPart && left.LowPart == right.LowPart;
}

std::wstring BuildTextureName(
    const wchar_t* channelName,
    bfvr::shared::TextureSlot slot)
{
    const wchar_t* suffix = L"-Unknown";
    switch (slot)
    {
    case bfvr::shared::TextureSlot::LeftWorld:
        suffix = L"-LeftWorld";
        break;
    case bfvr::shared::TextureSlot::RightWorld:
        suffix = L"-RightWorld";
        break;
    case bfvr::shared::TextureSlot::Ref2Ui:
        suffix = L"-Ref2Ui";
        break;
    }
    return std::wstring(channelName) + suffix;
}
} // namespace

namespace bfvr::shared
{
SharedTextureProducer::~SharedTextureProducer()
{
    Shutdown();
}

bool SharedTextureProducer::Initialize(
    const wchar_t* channelName,
    const SharedTextureRequirements& requirements,
    SharedTextureLogCallback logCallback,
    void* logContext)
{
    Shutdown();
    logCallback_ = logCallback;
    logContext_ = logContext;
    if (channelName == nullptr || *channelName == L'\0' ||
        requirements.format == DXGI_FORMAT_UNKNOWN ||
        requirements.leftWorldWidth == 0 || requirements.leftWorldHeight == 0 ||
        requirements.rightWorldWidth == 0 || requirements.rightWorldHeight == 0 ||
        requirements.uiWidth == 0 || requirements.uiHeight == 0)
    {
        WriteLog(L"Shared texture producer received invalid requirements.");
        return false;
    }
    if (!CreateAdapterDevice(requirements))
    {
        Shutdown();
        return false;
    }

    const bool created =
        CreateTexture(
            TextureSlot::LeftWorld,
            requirements.leftWorldWidth,
            requirements.leftWorldHeight,
            requirements.format,
            BuildTextureName(channelName, TextureSlot::LeftWorld)) &&
        CreateTexture(
            TextureSlot::RightWorld,
            requirements.rightWorldWidth,
            requirements.rightWorldHeight,
            requirements.format,
            BuildTextureName(channelName, TextureSlot::RightWorld)) &&
        CreateTexture(
            TextureSlot::Ref2Ui,
            requirements.uiWidth,
            requirements.uiHeight,
            requirements.format,
            BuildTextureName(channelName, TextureSlot::Ref2Ui));
    if (!created)
    {
        Shutdown();
        return false;
    }

    WriteLog(
        L"Shared texture producer created three named keyed-mutex textures at D3D feature level 0x%04X.",
        static_cast<unsigned int>(featureLevel_));
    return true;
}

bool SharedTextureProducer::PublishSyntheticFrame(
    DWORD frameIndex,
    bool brightWorld)
{
    if (context_ == nullptr)
    {
        return false;
    }

    std::array<bool, kTextureCount> acquired = {};
    for (std::size_t index = 0; index < textures_.size(); ++index)
    {
        const HRESULT result = textures_[index].keyedMutex->AcquireSync(0, 500);
        if (FAILED(result))
        {
            WriteLog(
                L"Shared texture producer could not acquire slot %zu for frame %lu (HRESULT=0x%08lX).",
                index,
                static_cast<unsigned long>(frameIndex),
                static_cast<unsigned long>(result));
            for (std::size_t releaseIndex = 0; releaseIndex < index; ++releaseIndex)
            {
                if (acquired[releaseIndex])
                {
                    textures_[releaseIndex].keyedMutex->ReleaseSync(1);
                }
            }
            return false;
        }
        acquired[index] = true;
    }

    const float phase = static_cast<float>(frameIndex % 180U) / 180.0F;
    const float leftColor[4] = {
        brightWorld ? 0.90F : 0.08F + phase * 0.12F,
        brightWorld ? 0.90F : 0.12F,
        brightWorld ? 0.90F : 0.24F,
        1.0F};
    const float rightColor[4] = {
        brightWorld ? 0.85F : 0.10F,
        brightWorld ? 0.85F : 0.14F + phase * 0.12F,
        brightWorld ? 0.85F : 0.26F,
        1.0F};
    const float uiColor[4] = {0.08F, 0.32F, 0.92F, 0.62F};
    const std::array<const float*, kTextureCount> colors = {
        leftColor,
        rightColor,
        uiColor};
    for (std::size_t index = 0; index < textures_.size(); ++index)
    {
        context_->ClearRenderTargetView(textures_[index].renderTargetView, colors[index]);
    }
    context_->Flush();

    bool released = true;
    for (Texture& texture : textures_)
    {
        released = SUCCEEDED(texture.keyedMutex->ReleaseSync(1)) && released;
    }
    return released;
}

bool SharedTextureProducer::PublishFrame(
    const std::array<SharedTexturePixels, kTextureCount>& frame)
{
    if (context_ == nullptr)
    {
        return false;
    }
    for (std::size_t index = 0; index < frame.size(); ++index)
    {
        const SharedTexturePixels& pixels = frame[index];
        const SharedTextureDescription& description = textures_[index].description;
        if (pixels.data == nullptr ||
            pixels.width != description.width ||
            pixels.height != description.height ||
            static_cast<DWORD>(pixels.format) != description.format ||
            pixels.rowPitch < pixels.width * sizeof(DWORD))
        {
            WriteLog(
                L"Shared texture producer rejected slot %zu pixels: source=%ux%u format=%u pitch=%u, target=%lux%lu format=%lu.",
                index,
                pixels.width,
                pixels.height,
                static_cast<unsigned int>(pixels.format),
                pixels.rowPitch,
                static_cast<unsigned long>(description.width),
                static_cast<unsigned long>(description.height),
                static_cast<unsigned long>(description.format));
            return false;
        }
    }

    std::array<bool, kTextureCount> acquired = {};
    for (std::size_t index = 0; index < textures_.size(); ++index)
    {
        const HRESULT result = textures_[index].keyedMutex->AcquireSync(0, 500);
        if (FAILED(result))
        {
            WriteLog(
                L"Shared texture producer could not acquire slot %zu for a captured frame (HRESULT=0x%08lX).",
                index,
                static_cast<unsigned long>(result));
            for (std::size_t releaseIndex = 0; releaseIndex < index; ++releaseIndex)
            {
                if (acquired[releaseIndex])
                {
                    textures_[releaseIndex].keyedMutex->ReleaseSync(1);
                }
            }
            return false;
        }
        acquired[index] = true;
    }

    for (std::size_t index = 0; index < textures_.size(); ++index)
    {
        context_->UpdateSubresource(
            textures_[index].resource,
            0,
            nullptr,
            frame[index].data,
            frame[index].rowPitch,
            0);
    }
    context_->Flush();

    bool released = true;
    for (Texture& texture : textures_)
    {
        released = SUCCEEDED(texture.keyedMutex->ReleaseSync(1)) && released;
    }
    return released;
}

void SharedTextureProducer::CopyDescriptions(
    SharedTextureDescription* destination,
    std::size_t count) const
{
    if (destination == nullptr || count < textures_.size())
    {
        return;
    }
    for (std::size_t index = 0; index < textures_.size(); ++index)
    {
        destination[index] = textures_[index].description;
    }
}

void SharedTextureProducer::Shutdown()
{
    for (Texture& texture : textures_)
    {
        ReleaseTexture(texture);
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
    featureLevel_ = D3D_FEATURE_LEVEL_9_1;
}

D3D_FEATURE_LEVEL SharedTextureProducer::DeviceFeatureLevel() const noexcept
{
    return featureLevel_;
}

bool SharedTextureProducer::CreateAdapterDevice(
    const SharedTextureRequirements& requirements)
{
    const bfvr::D3DSystemRuntime& runtime =
        bfvr::GetD3DSystemRuntime();
    if (!runtime.IsAvailable())
    {
        WriteLog(
            L"Shared texture producer could not resolve system D3D11/DXGI entry points (error=%lu).",
            static_cast<unsigned long>(runtime.error));
        return false;
    }

    IDXGIFactory1* factory = nullptr;
    HRESULT result = runtime.createDXGIFactory1(
        __uuidof(IDXGIFactory1),
        reinterpret_cast<void**>(&factory));
    if (FAILED(result) || factory == nullptr)
    {
        WriteLog(L"Shared texture producer could not create DXGI factory (HRESULT=0x%08lX).", result);
        return false;
    }

    IDXGIAdapter1* selectedAdapter = nullptr;
    for (UINT index = 0;; ++index)
    {
        IDXGIAdapter1* candidate = nullptr;
        result = factory->EnumAdapters1(index, &candidate);
        if (result == DXGI_ERROR_NOT_FOUND)
        {
            break;
        }
        if (FAILED(result) || candidate == nullptr)
        {
            continue;
        }
        DXGI_ADAPTER_DESC1 description = {};
        if (SUCCEEDED(candidate->GetDesc1(&description)) &&
            EqualLuid(description.AdapterLuid, requirements.adapterLuid))
        {
            selectedAdapter = candidate;
            break;
        }
        candidate->Release();
    }
    factory->Release();
    if (selectedAdapter == nullptr)
    {
        WriteLog(
            L"Shared texture producer could not find runtime adapter LUID %08lX:%08lX.",
            static_cast<unsigned long>(requirements.adapterLuid.HighPart),
            static_cast<unsigned long>(requirements.adapterLuid.LowPart));
        return false;
    }

    constexpr std::array<D3D_FEATURE_LEVEL, 6> allLevels = {
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0};
    std::array<D3D_FEATURE_LEVEL, allLevels.size()> eligibleLevels = {};
    const auto eligibleEnd = std::copy_if(
        allLevels.begin(),
        allLevels.end(),
        eligibleLevels.begin(),
        [&](D3D_FEATURE_LEVEL level)
        {
            return level >= requirements.minimumFeatureLevel;
        });
    const UINT levelCount = static_cast<UINT>(
        std::distance(eligibleLevels.begin(), eligibleEnd));
    result = levelCount == 0
        ? E_INVALIDARG
        : runtime.createD3D11Device(
            selectedAdapter,
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            eligibleLevels.data(),
            levelCount,
            D3D11_SDK_VERSION,
            &device_,
            &featureLevel_,
            &context_);
    selectedAdapter->Release();
    if (FAILED(result) || device_ == nullptr || context_ == nullptr)
    {
        WriteLog(
            L"Shared texture producer could not create its adapter-matched D3D11 device (HRESULT=0x%08lX).",
            static_cast<unsigned long>(result));
        return false;
    }
    return true;
}

bool SharedTextureProducer::CreateTexture(
    TextureSlot slot,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    const std::wstring& name)
{
    if (name.size() >= kSharedNameCapacity)
    {
        WriteLog(L"Shared texture name is too long.");
        return false;
    }

    Texture& texture = textures_[static_cast<std::size_t>(slot)];
    D3D11_TEXTURE2D_DESC description = {};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = format;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    description.MiscFlags =
        D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
        D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    HRESULT result = device_->CreateTexture2D(&description, nullptr, &texture.resource);
    if (SUCCEEDED(result))
    {
        result = device_->CreateRenderTargetView(
            texture.resource,
            nullptr,
            &texture.renderTargetView);
    }
    if (SUCCEEDED(result))
    {
        result = texture.resource->QueryInterface(
            __uuidof(IDXGIKeyedMutex),
            reinterpret_cast<void**>(&texture.keyedMutex));
    }

    IDXGIResource1* dxgiResource = nullptr;
    if (SUCCEEDED(result))
    {
        result = texture.resource->QueryInterface(
            __uuidof(IDXGIResource1),
            reinterpret_cast<void**>(&dxgiResource));
    }
    if (SUCCEEDED(result) && dxgiResource != nullptr)
    {
        result = dxgiResource->CreateSharedHandle(
            nullptr,
            DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
            name.c_str(),
            &texture.sharedHandle);
    }
    if (dxgiResource != nullptr)
    {
        dxgiResource->Release();
    }
    if (FAILED(result) || texture.sharedHandle == nullptr)
    {
        WriteLog(
            L"Shared texture producer could not create %ux%u named resource '%s' (HRESULT=0x%08lX).",
            width,
            height,
            name.c_str(),
            static_cast<unsigned long>(result));
        ReleaseTexture(texture);
        return false;
    }

    texture.description.width = width;
    texture.description.height = height;
    texture.description.format = static_cast<DWORD>(format);
    texture.description.transport =
        static_cast<DWORD>(SharedTextureTransport::NamedNtHandle);
    wcsncpy_s(texture.description.name, name.c_str(), _TRUNCATE);
    return true;
}

void SharedTextureProducer::WriteLog(const wchar_t* format, ...) const
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

void SharedTextureProducer::ReleaseTexture(Texture& texture)
{
    if (texture.sharedHandle != nullptr)
    {
        CloseHandle(texture.sharedHandle);
        texture.sharedHandle = nullptr;
    }
    if (texture.keyedMutex != nullptr)
    {
        texture.keyedMutex->Release();
        texture.keyedMutex = nullptr;
    }
    if (texture.renderTargetView != nullptr)
    {
        texture.renderTargetView->Release();
        texture.renderTargetView = nullptr;
    }
    if (texture.resource != nullptr)
    {
        texture.resource->Release();
        texture.resource = nullptr;
    }
    texture.description = {};
}
} // namespace bfvr::shared
