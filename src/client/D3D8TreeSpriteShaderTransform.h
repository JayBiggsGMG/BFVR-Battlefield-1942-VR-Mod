#pragma once

#include "client/D3D8StereoShaderTransform.h"

#include <array>

namespace bfvr::d3d8probe
{

constexpr std::size_t kD3D8TreeSpriteShaderRegisterCount = 8;

enum class TreeSpriteShaderPrepareResult
{
    Prepared,
    InvalidApi,
    StateReadFailed,
    InvalidTransform,
    SourceConstantsMismatch
};

struct D3D8TreeSpriteShaderRegisterBlock
{
    float values[kD3D8TreeSpriteShaderRegisterCount][4] = {};
};

struct D3D8TreeSpriteShaderEyeConstants
{
    D3DMatrix worldView = {};
    D3DMatrix projection = {};
};

struct D3D8TreeSpriteShaderTransformState
{
    D3D8TreeSpriteShaderRegisterBlock originalRegisters = {};
    std::array<D3D8TreeSpriteShaderEyeConstants, 2> eyeConstants = {};
    bool prepared = false;
};

[[nodiscard]] TreeSpriteShaderPrepareResult
PrepareD3D8TreeSpriteShaderTransforms(
    const D3D8VertexShaderConstantApi& api,
    void* device,
    const D3DMatrix& world,
    const D3DMatrix& sourceView,
    const D3DMatrix& sourceProjection,
    const D3DMatrix& leftView,
    const D3DMatrix& leftProjection,
    const D3DMatrix& rightView,
    const D3DMatrix& rightProjection,
    D3D8TreeSpriteShaderTransformState& state) noexcept;

[[nodiscard]] HRESULT ApplyD3D8TreeSpriteShaderEye(
    const D3D8VertexShaderConstantApi& api,
    void* device,
    const D3D8TreeSpriteShaderTransformState& state,
    std::size_t eye) noexcept;

// Restores and byte-verifies c0-c7. Tree/light/fade constants c8 and later
// are never modified by BFVR.
[[nodiscard]] bool RestoreAndVerifyD3D8TreeSpriteShaderConstants(
    const D3D8VertexShaderConstantApi& api,
    void* device,
    const D3D8TreeSpriteShaderTransformState& state) noexcept;

} // namespace bfvr::d3d8probe
