#pragma once

#include "presenter/SharedPresentationProtocol.h"

#include <d3d11.h>

namespace bfvr::shared
{

// Composites the BFVR Back to Game raster onto the presenter's local Ref2
// texture after the x86-owned menu texture has been copied. This keeps the
// button in the exact texture used by both the OpenXR layer and desktop mirror.
class D3D11MainMenuOverlay
{
public:
    D3D11MainMenuOverlay() = default;
    ~D3D11MainMenuOverlay();

    D3D11MainMenuOverlay(const D3D11MainMenuOverlay&) = delete;
    D3D11MainMenuOverlay& operator=(const D3D11MainMenuOverlay&) = delete;

    bool Initialize(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        SharedTextureLogCallback logCallback,
        void* logContext);
    bool Composite(
        ID3D11RenderTargetView* destination,
        UINT sourceUiWidth,
        UINT sourceUiHeight,
        UINT destinationWidth,
        UINT destinationHeight,
        bool visible,
        bool hovered);
    [[nodiscard]] bool IsReady() const noexcept;
    void Shutdown();

private:
    bool CreateButtonView(bool hovered, ID3D11ShaderResourceView** view);
    void WriteLog(const wchar_t* format, ...) const;

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    ID3D11VertexShader* vertexShader_ = nullptr;
    ID3D11PixelShader* pixelShader_ = nullptr;
    ID3D11SamplerState* sampler_ = nullptr;
    ID3D11BlendState* blendState_ = nullptr;
    ID3D11ShaderResourceView* buttonViews_[2] = {};
    SharedTextureLogCallback logCallback_ = nullptr;
    void* logContext_ = nullptr;
    bool firstCompositeLogged_ = false;
};

} // namespace bfvr::shared
