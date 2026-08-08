#pragma once

#include "presenter/SharedPresentationProtocol.h"

#include <d3d11.h>

#include <array>
#include <cstddef>

namespace bfvr::shared
{
class D3D11WaterReflection
{
public:
    D3D11WaterReflection() = default;
    ~D3D11WaterReflection();

    D3D11WaterReflection(const D3D11WaterReflection&) = delete;
    D3D11WaterReflection& operator=(const D3D11WaterReflection&) = delete;

    bool Initialize(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        SharedTextureLogCallback logCallback,
        void* logContext);
    bool BuildEye(
        std::size_t eye,
        ID3D11ShaderResourceView* worldColor,
        bool worldColorAlreadyLinear,
        ID3D11ShaderResourceView* packedDepthAndMask,
        UINT width,
        UINT height,
        const float projection[16]);
    [[nodiscard]] ID3D11ShaderResourceView* GetEyeView(
        std::size_t eye) const noexcept;
    void Shutdown();

private:
    struct EyeResources
    {
        ID3D11Texture2D* texture = nullptr;
        ID3D11ShaderResourceView* view = nullptr;
        ID3D11RenderTargetView* target = nullptr;
        UINT width = 0;
        UINT height = 0;
    };

    bool EnsureEyeResources(EyeResources& resources, UINT width, UINT height);
    bool UpdateConfiguration(
        UINT width,
        UINT height,
        const float projection[16],
        bool worldColorAlreadyLinear);
    void ReleaseEyeResources(EyeResources& resources);
    void WriteLog(const wchar_t* format, ...) const;

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    ID3D11VertexShader* vertexShader_ = nullptr;
    ID3D11PixelShader* reflectionShader_ = nullptr;
    ID3D11Buffer* configurationBuffer_ = nullptr;
    ID3D11SamplerState* linearSampler_ = nullptr;
    ID3D11SamplerState* pointSampler_ = nullptr;
    std::array<EyeResources, kDepthTextureCount> eyes_ = {};
    SharedTextureLogCallback logCallback_ = nullptr;
    void* logContext_ = nullptr;
};
} // namespace bfvr::shared
