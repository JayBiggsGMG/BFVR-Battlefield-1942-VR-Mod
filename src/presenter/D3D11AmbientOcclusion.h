#pragma once

#include "presenter/SharedPresentationProtocol.h"

#include <d3d11.h>

#include <array>
#include <cstddef>
#include <vector>

namespace bfvr::shared
{
class D3D11AmbientOcclusion
{
public:
    D3D11AmbientOcclusion() = default;
    ~D3D11AmbientOcclusion();

    D3D11AmbientOcclusion(const D3D11AmbientOcclusion&) = delete;
    D3D11AmbientOcclusion& operator=(const D3D11AmbientOcclusion&) = delete;

    bool Initialize(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        SharedTextureLogCallback logCallback,
        void* logContext);
    bool BeginFrame();
    bool BuildEye(
        std::size_t eye,
        ID3D11ShaderResourceView* packedDepth,
        UINT depthWidth,
        UINT depthHeight,
        const float projection[16]);
    void BeginApplicationTiming();
    void EndApplicationTiming();
    void EndFrame();
    void CollectFrameTimings();
    [[nodiscard]] ID3D11ShaderResourceView* GetEyeView(
        std::size_t eye) const noexcept;
    void Shutdown();

private:
    struct EyeResources
    {
        ID3D11Texture2D* textures[2] = {};
        ID3D11ShaderResourceView* views[2] = {};
        ID3D11RenderTargetView* targets[2] = {};
        ID3D11Query* timestampStart = nullptr;
        ID3D11Query* timestampEnd = nullptr;
        UINT width = 0;
        UINT height = 0;
    };

    bool EnsureEyeResources(
        EyeResources& resources,
        UINT depthWidth,
        UINT depthHeight);
    bool UpdateConfiguration(
        UINT depthWidth,
        UINT depthHeight,
        const float projection[16]);
    void ReleaseEyeResources(EyeResources& resources);
    void ReportTimings();
    void WriteLog(const wchar_t* format, ...) const;

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    ID3D11VertexShader* vertexShader_ = nullptr;
    ID3D11PixelShader* evaluateShader_ = nullptr;
    ID3D11PixelShader* denoiseShader_ = nullptr;
    ID3D11Buffer* configurationBuffer_ = nullptr;
    ID3D11SamplerState* pointSampler_ = nullptr;
    ID3D11Query* disjointQuery_ = nullptr;
    ID3D11Query* applicationTimestampStart_ = nullptr;
    ID3D11Query* applicationTimestampEnd_ = nullptr;
    std::array<EyeResources, kDepthTextureCount> eyes_ = {};
    std::array<std::vector<double>, kDepthTextureCount> gpuMilliseconds_ = {};
    std::vector<double> stereoGpuMilliseconds_;
    std::vector<double> applicationGpuMilliseconds_;
    std::vector<double> completeGpuMilliseconds_;
    std::array<bool, kDepthTextureCount> frameEyesBuilt_ = {};
    SharedTextureLogCallback logCallback_ = nullptr;
    void* logContext_ = nullptr;
    bool frameTimingActive_ = false;
    bool frameApplicationTimingActive_ = false;
    bool frameApplicationTimed_ = false;
    bool timingsReported_ = false;
};
} // namespace bfvr::shared
