#pragma once

#include "openxr/OpenXRPresentation.h"
#include "presenter/D3D11AmbientOcclusion.h"
#include "presenter/D3D11MainMenuOverlay.h"
#include "presenter/D3D11ScreenSpaceGlobalIllumination.h"
#include "presenter/D3D11TextureScaler.h"
#include "presenter/D3D11WaterReflection.h"
#include "presenter/SharedPresentationProtocol.h"

#include <d3d11_1.h>

#include <array>

namespace bfvr::shared
{
class SharedTextureConsumer
{
public:
    SharedTextureConsumer() = default;
    ~SharedTextureConsumer();

    SharedTextureConsumer(const SharedTextureConsumer&) = delete;
    SharedTextureConsumer& operator=(const SharedTextureConsumer&) = delete;

    bool Initialize(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const SharedTextureDescription* descriptions,
        std::size_t count,
        const SharedTextureDescription* depthDescriptions,
        std::size_t depthCount,
        DepthEncoding depthEncoding,
        DWORD producerFlags,
        const PresentationRequirements& destinationRequirements,
        SharedTextureLogCallback logCallback,
        void* logContext);
    bool ConsumeFrame(
        LONG frameOverlayFlags = 0,
        bool depthValid = false,
        const SharedDepthFrameParameters* depthFrame = nullptr,
        bool waterMaskValid = false);
    [[nodiscard]] OpenXRPresentationTextures GetLocalTextures() const noexcept;
    bool ReadCenterPixels(DWORD* pixels, std::size_t count);
    void Shutdown();

private:
    struct Texture
    {
        ID3D11Texture2D* shared = nullptr;
        IDXGIKeyedMutex* keyedMutex = nullptr;
        ID3D11ShaderResourceView* sharedView = nullptr;
        ID3D11Texture2D* local = nullptr;
        ID3D11RenderTargetView* localTarget = nullptr;
        UINT sourceWidth = 0;
        UINT sourceHeight = 0;
        UINT destinationWidth = 0;
        UINT destinationHeight = 0;
        bool requiresScaling = false;
        bool transparentPadding = false;
        bool sourceAlreadyLinear = false;
        bool applyAntialiasing = false;
        bool applyBloom = false;
    };

    struct DepthTexture
    {
        ID3D11Texture2D* shared = nullptr;
        IDXGIKeyedMutex* keyedMutex = nullptr;
        ID3D11ShaderResourceView* sharedView = nullptr;
        UINT width = 0;
        UINT height = 0;
    };

    bool OpenTexture(
        ID3D11Device1* device,
        std::size_t index,
        const SharedTextureDescription& description,
        UINT destinationWidth,
        UINT destinationHeight,
        DXGI_FORMAT destinationFormat);
    bool OpenDepthTexture(
        ID3D11Device1* device,
        std::size_t index,
        const SharedTextureDescription& description);
    bool ReadCenterPixel(const Texture& texture, DWORD& pixel);
    void ApplySavedLiveSettings();
    void WriteLog(const wchar_t* format, ...) const;
    void ReleaseTexture(Texture& texture);
    void ReleaseDepthTexture(DepthTexture& texture);

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    ID3D11Query* legacyCompletionQuery_ = nullptr;
    D3D11MainMenuOverlay mainMenuOverlay_;
    D3D11AmbientOcclusion ambientOcclusion_;
    D3D11ScreenSpaceGlobalIllumination screenSpaceGlobalIllumination_;
    D3D11WaterReflection waterReflection_;
    D3D11TextureScaler scaler_;
    bool requiresLegacyCompletionWait_ = false;
    bool scalerRequired_ = false;
    bool worldFxaaEnabled_ = true;
    float worldFxaaSharpeningStrength_ = 0.25F;
    bool worldBloomEnabled_ = false;
    bool ambientOcclusionEnabled_ = false;
    bool screenSpaceGlobalIlluminationEnabled_ = false;
    bool waterReflectionsEnabled_ = false;
    float ambientOcclusionIntensity_ = 1.0F;
    float ambientOcclusionRadiusMeters_ = 0.60F;
    LONG ambientOcclusionFrameFailures_ = 0;
    float screenSpaceGlobalIlluminationIntensity_ = 0.65F;
    float screenSpaceGlobalIlluminationDebugMode_ = 0.0F;
    LONG screenSpaceGlobalIlluminationFrameFailures_ = 0;
    float waterReflectionIntensity_ = 1.0F;
    LONG waterReflectionFrameFailures_ = 0;
    float worldBloomThreshold_ = 0.55F;
    float worldBloomIntensity_ = 0.35F;
    std::array<Texture, kTextureCount> textures_ = {};
    std::array<DepthTexture, kDepthTextureCount> depthTextures_ = {};
    SharedTextureLogCallback logCallback_ = nullptr;
    void* logContext_ = nullptr;
};
} // namespace bfvr::shared
