#pragma once

#include "presenter/SharedPresentationProtocol.h"

#include <d3d11.h>

#include <array>
#include <cstddef>
#include <vector>

namespace bfvr::shared
{
class D3D11ScreenSpaceGlobalIllumination
{
public:
    D3D11ScreenSpaceGlobalIllumination() = default;
    ~D3D11ScreenSpaceGlobalIllumination();

    D3D11ScreenSpaceGlobalIllumination(
        const D3D11ScreenSpaceGlobalIllumination&) = delete;
    D3D11ScreenSpaceGlobalIllumination& operator=(
        const D3D11ScreenSpaceGlobalIllumination&) = delete;

    bool Initialize(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        SharedTextureLogCallback logCallback,
        void* logContext);
    bool BeginFrame();
    bool BuildEye(
        std::size_t eye,
        ID3D11ShaderResourceView* worldColor,
        bool worldColorAlreadyLinear,
        ID3D11ShaderResourceView* packedDepth,
        UINT depthWidth,
        UINT depthHeight,
        const float projection[16]);
    void EndFrame();
    void CollectFrameTimings();
    [[nodiscard]] ID3D11ShaderResourceView* GetEyeView(
        std::size_t eye) const noexcept;
    void Shutdown();

private:
    struct EyeResources
    {
        ID3D11Texture2D* guideTexture = nullptr;
        ID3D11ShaderResourceView* guideView = nullptr;
        ID3D11RenderTargetView* guideTarget = nullptr;
        ID3D11Texture2D* radianceTextures[2] = {};
        ID3D11ShaderResourceView* radianceViews[2] = {};
        ID3D11RenderTargetView* radianceTargets[2] = {};
        ID3D11Query* timestampStart = nullptr;
        ID3D11Query* timestampEnd = nullptr;
        UINT width = 0;
        UINT height = 0;
        UINT evaluationWidth = 0;
        UINT evaluationHeight = 0;
    };

    bool EnsureEyeResources(
        EyeResources& resources,
        UINT width,
        UINT height);
    bool UpdateConfiguration(
        UINT width,
        UINT height,
        UINT evaluationWidth,
        UINT evaluationHeight,
        bool worldColorAlreadyLinear,
        const float projection[16]);
    void ReleaseEyeResources(EyeResources& resources);
    void ReportTimings();
    void WriteLog(const wchar_t* format, ...) const;

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    ID3D11VertexShader* vertexShader_ = nullptr;
    ID3D11PixelShader* prepareShader_ = nullptr;
    ID3D11PixelShader* evaluateShader_ = nullptr;
    ID3D11PixelShader* denoiseShader_ = nullptr;
    ID3D11Buffer* configurationBuffer_ = nullptr;
    ID3D11SamplerState* pointSampler_ = nullptr;
    ID3D11SamplerState* linearSampler_ = nullptr;
    ID3D11Query* disjointQuery_ = nullptr;
    std::array<EyeResources, kDepthTextureCount> eyes_ = {};
    std::array<std::vector<double>, kDepthTextureCount> gpuMilliseconds_ = {};
    std::vector<double> stereoGpuMilliseconds_;
    std::array<bool, kDepthTextureCount> frameEyesBuilt_ = {};
    SharedTextureLogCallback logCallback_ = nullptr;
    void* logContext_ = nullptr;
    bool frameTimingActive_ = false;
    bool timingsReported_ = false;
};
} // namespace bfvr::shared
