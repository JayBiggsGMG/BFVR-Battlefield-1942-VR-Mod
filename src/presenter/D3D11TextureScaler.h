#pragma once

#include "presenter/SharedPresentationProtocol.h"

#include <d3d11.h>

#include <array>
#include <vector>

namespace bfvr::shared
{
class D3D11TextureScaler
{
public:
    D3D11TextureScaler() = default;
    ~D3D11TextureScaler();

    D3D11TextureScaler(const D3D11TextureScaler&) = delete;
    D3D11TextureScaler& operator=(const D3D11TextureScaler&) = delete;

    bool Initialize(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        SharedTextureLogCallback logCallback,
        void* logContext,
        bool enableBloom,
        bool enableAmbientOcclusion,
        bool enableScreenSpaceGlobalIllumination,
        bool enableWaterReflections,
        bool collectBloomTimings = true);
    bool ScaleAspectFit(
        ID3D11ShaderResourceView* sourceView,
        UINT sourceWidth,
        UINT sourceHeight,
        ID3D11RenderTargetView* destinationView,
        UINT destinationWidth,
        UINT destinationHeight,
        bool transparentPadding,
        bool sourceAlreadyLinear,
        bool applyAntialiasing,
        float fxaaSharpeningStrength,
        ID3D11ShaderResourceView* ambientOcclusionView,
        float ambientOcclusionIntensity,
        ID3D11ShaderResourceView* screenSpaceGlobalIlluminationView,
        float screenSpaceGlobalIlluminationIntensity,
        float screenSpaceGlobalIlluminationDebugMode,
        ID3D11ShaderResourceView* waterReflectionView,
        float waterReflectionIntensity,
        bool applyBloom,
        float bloomThreshold,
        float bloomIntensity);
    bool BeginBloomFrame();
    void EndBloomFrame();
    void CollectBloomFrameTimings();
    void Shutdown();

private:
    bool EnsureBloomResources(UINT sourceWidth, UINT sourceHeight);
    bool BuildBloom(
        ID3D11ShaderResourceView* sourceView,
        bool sourceAlreadyLinear,
        float bloomThreshold);
    void ReleaseBloomResources();
    void ReportBloomTimings();
    void UpdateConfiguration(
        bool sourceAlreadyLinear,
        float ambientOcclusionIntensity,
        float bloomThreshold,
        float bloomIntensity,
        float bloomTexelWidth,
        float bloomTexelHeight,
        float screenSpaceGlobalIlluminationIntensity,
        float screenSpaceGlobalIlluminationDebugMode,
        float waterReflectionIntensity,
        float fxaaSharpeningStrength);
    void WriteLog(const wchar_t* format, ...) const;

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    ID3D11VertexShader* vertexShader_ = nullptr;
    ID3D11PixelShader* colorPixelShader_ = nullptr;
    ID3D11PixelShader* fxaaPixelShader_ = nullptr;
    ID3D11PixelShader* compositeColorPixelShader_ = nullptr;
    ID3D11PixelShader* compositeFxaaPixelShader_ = nullptr;
    ID3D11PixelShader* bloomDownsamplePixelShader_ = nullptr;
    ID3D11PixelShader* bloomBlurHorizontalPixelShader_ = nullptr;
    ID3D11PixelShader* bloomBlurVerticalPixelShader_ = nullptr;
    ID3D11Buffer* configurationBuffer_ = nullptr;
    ID3D11SamplerState* sampler_ = nullptr;
    ID3D11Texture2D* bloomTextures_[2] = {};
    ID3D11ShaderResourceView* bloomViews_[2] = {};
    ID3D11RenderTargetView* bloomTargets_[2] = {};
    ID3D11Query* bloomDisjointQuery_ = nullptr;
    std::array<ID3D11Query*, 2> bloomTimestampStarts_ = {};
    std::array<ID3D11Query*, 2> bloomTimestampEnds_ = {};
    std::array<std::vector<double>, 2> bloomGpuMilliseconds_ = {};
    std::vector<double> bloomStereoGpuMilliseconds_;
    UINT bloomWidth_ = 0;
    UINT bloomHeight_ = 0;
    std::array<bool, 2> bloomFrameEyesBuilt_ = {};
    std::size_t bloomFrameEyeCount_ = 0;
    bool bloomFrameTimingActive_ = false;
    bool bloomFrameActive_ = false;
    bool collectBloomTimings_ = true;
    bool bloomTimingsReported_ = false;
    SharedTextureLogCallback logCallback_ = nullptr;
    void* logContext_ = nullptr;
};
} // namespace bfvr::shared
