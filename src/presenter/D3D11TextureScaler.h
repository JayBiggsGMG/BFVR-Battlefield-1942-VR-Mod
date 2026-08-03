#pragma once

#include "presenter/SharedPresentationProtocol.h"

#include <d3d11.h>

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
        bool enableAmbientOcclusion);
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
        ID3D11ShaderResourceView* ambientOcclusionView,
        float ambientOcclusionIntensity,
        bool applyBloom,
        float bloomThreshold,
        float bloomIntensity);
    void Shutdown();

private:
    bool EnsureBloomResources(UINT sourceWidth, UINT sourceHeight);
    bool BuildBloom(
        ID3D11ShaderResourceView* sourceView,
        bool sourceAlreadyLinear,
        float bloomThreshold);
    void ReleaseBloomResources();
    void UpdateConfiguration(
        bool sourceAlreadyLinear,
        float ambientOcclusionIntensity,
        float bloomThreshold,
        float bloomIntensity,
        float bloomTexelWidth,
        float bloomTexelHeight);
    void WriteLog(const wchar_t* format, ...) const;

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    ID3D11VertexShader* vertexShader_ = nullptr;
    ID3D11PixelShader* colorPixelShader_ = nullptr;
    ID3D11PixelShader* fxaaPixelShader_ = nullptr;
    ID3D11PixelShader* aoColorPixelShader_ = nullptr;
    ID3D11PixelShader* aoFxaaPixelShader_ = nullptr;
    ID3D11PixelShader* bloomDownsamplePixelShader_ = nullptr;
    ID3D11PixelShader* bloomBlurHorizontalPixelShader_ = nullptr;
    ID3D11PixelShader* bloomBlurVerticalPixelShader_ = nullptr;
    ID3D11Buffer* configurationBuffer_ = nullptr;
    ID3D11SamplerState* sampler_ = nullptr;
    ID3D11Texture2D* bloomTextures_[2] = {};
    ID3D11ShaderResourceView* bloomViews_[2] = {};
    ID3D11RenderTargetView* bloomTargets_[2] = {};
    UINT bloomWidth_ = 0;
    UINT bloomHeight_ = 0;
    SharedTextureLogCallback logCallback_ = nullptr;
    void* logContext_ = nullptr;
};
} // namespace bfvr::shared
