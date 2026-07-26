#pragma once

#include "client/D3D8StereoShaderTransform.h"

#include <array>

namespace bfvr::d3d8probe
{

constexpr std::size_t kD3D8SpriteShaderRegisterCount = 10;

enum class SpriteShaderPrepareResult
{
    Prepared,
    InvalidApi,
    StateReadFailed,
    InvalidTransform,
    SourceConstantsMismatch
};

struct D3D8SpriteShaderRegisterBlock
{
    float values[kD3D8SpriteShaderRegisterCount][4] = {};
};

struct D3D8SpriteShaderEyeConstants
{
    D3DMatrix rotationOnlyView = {};
    D3DMatrix projection = {};
    float cameraPosition[4] = {};
};

struct D3D8SpriteShaderTransformState
{
    D3D8SpriteShaderRegisterBlock originalRegisters = {};
    std::array<D3D8SpriteShaderEyeConstants, 2> eyeConstants = {};
    bool prepared = false;
};

[[nodiscard]] SpriteShaderPrepareResult PrepareD3D8SpriteShaderTransforms(
    const D3D8VertexShaderConstantApi& api,
    void* device,
    const D3DMatrix& sourceView,
    const D3DMatrix& sourceProjection,
    const D3DMatrix& leftView,
    const D3DMatrix& leftProjection,
    const D3DMatrix& rightView,
    const D3DMatrix& rightProjection,
    D3D8SpriteShaderTransformState& state) noexcept;

[[nodiscard]] HRESULT ApplyD3D8SpriteShaderEye(
    const D3D8VertexShaderConstantApi& api,
    void* device,
    const D3D8SpriteShaderTransformState& state,
    std::size_t eye) noexcept;

// Restores and verifies c0-c9. c10 and all later engine constants are never
// modified by BFVR.
[[nodiscard]] bool RestoreAndVerifyD3D8SpriteShaderConstants(
    const D3D8VertexShaderConstantApi& api,
    void* device,
    const D3D8SpriteShaderTransformState& state) noexcept;

} // namespace bfvr::d3d8probe
