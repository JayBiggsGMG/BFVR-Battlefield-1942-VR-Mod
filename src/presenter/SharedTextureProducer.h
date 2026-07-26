#pragma once

#include "presenter/SharedPresentationProtocol.h"

#include <d3d11.h>

#include <array>
#include <string>

namespace bfvr::shared
{
struct SharedTextureRequirements
{
    LUID adapterLuid = {};
    D3D_FEATURE_LEVEL minimumFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    UINT leftWorldWidth = 0;
    UINT leftWorldHeight = 0;
    UINT rightWorldWidth = 0;
    UINT rightWorldHeight = 0;
    UINT uiWidth = 0;
    UINT uiHeight = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
};

struct SharedTexturePixels
{
    const void* data = nullptr;
    UINT rowPitch = 0;
    UINT width = 0;
    UINT height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
};

class SharedTextureProducer
{
public:
    SharedTextureProducer() = default;
    ~SharedTextureProducer();

    SharedTextureProducer(const SharedTextureProducer&) = delete;
    SharedTextureProducer& operator=(const SharedTextureProducer&) = delete;

    bool Initialize(
        const wchar_t* channelName,
        const SharedTextureRequirements& requirements,
        SharedTextureLogCallback logCallback,
        void* logContext);
    bool PublishSyntheticFrame(DWORD frameIndex, bool brightWorld = false);
    bool PublishFrame(const std::array<SharedTexturePixels, kTextureCount>& frame);
    void CopyDescriptions(SharedTextureDescription* destination, std::size_t count) const;
    void Shutdown();

    [[nodiscard]] D3D_FEATURE_LEVEL DeviceFeatureLevel() const noexcept;

private:
    struct Texture
    {
        ID3D11Texture2D* resource = nullptr;
        ID3D11RenderTargetView* renderTargetView = nullptr;
        IDXGIKeyedMutex* keyedMutex = nullptr;
        HANDLE sharedHandle = nullptr;
        SharedTextureDescription description = {};
    };

    bool CreateAdapterDevice(const SharedTextureRequirements& requirements);
    bool CreateTexture(
        TextureSlot slot,
        UINT width,
        UINT height,
        DXGI_FORMAT format,
        const std::wstring& name);
    void WriteLog(const wchar_t* format, ...) const;
    void ReleaseTexture(Texture& texture);

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    D3D_FEATURE_LEVEL featureLevel_ = D3D_FEATURE_LEVEL_9_1;
    std::array<Texture, kTextureCount> textures_ = {};
    SharedTextureLogCallback logCallback_ = nullptr;
    void* logContext_ = nullptr;
};
} // namespace bfvr::shared
