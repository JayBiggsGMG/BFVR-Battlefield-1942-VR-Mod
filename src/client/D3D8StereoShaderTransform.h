#pragma once

#include "client/D3D8StereoProbeRecords.h"

#include <windows.h>

#include <array>

namespace bfvr::d3d8probe
{

using SetVertexShaderConstantFn =
    HRESULT(WINAPI*)(void* device, DWORD firstRegister, const void* data, DWORD registerCount);
using GetVertexShaderConstantFn =
    HRESULT(WINAPI*)(void* device, DWORD firstRegister, void* data, DWORD registerCount);

struct D3D8VertexShaderConstantApi
{
    SetVertexShaderConstantFn setConstants = nullptr;
    GetVertexShaderConstantFn getConstants = nullptr;
};

enum class SkinningShaderPrepareResult
{
    Prepared,
    InvalidApi,
    StateReadFailed,
    InvalidTransform,
    SourceConstantsMismatch
};

struct D3D8SkinningShaderTransformState
{
    D3DMatrix originalConstants = {};
    std::array<D3DMatrix, 2> eyeConstants = {};
    bool prepared = false;
};

[[nodiscard]] SkinningShaderPrepareResult PrepareD3D8SkinningShaderTransforms(
    const D3D8VertexShaderConstantApi& api,
    void* device,
    const D3DMatrix& world,
    const D3DMatrix& sourceView,
    const D3DMatrix& sourceProjection,
    const D3DMatrix& leftView,
    const D3DMatrix& leftProjection,
    const D3DMatrix& rightView,
    const D3DMatrix& rightProjection,
    D3D8SkinningShaderTransformState& state) noexcept;

[[nodiscard]] HRESULT ApplyD3D8SkinningShaderEye(
    const D3D8VertexShaderConstantApi& api,
    void* device,
    const D3D8SkinningShaderTransformState& state,
    std::size_t eye) noexcept;

// Restores c0-c3. Registers c4 onward are never touched by BFVR, preserving
// the engine's fog, lighting, and bones. Deep diagnostics may verify the
// restored registers through the separate read-only function.
[[nodiscard]] bool RestoreD3D8SkinningShaderConstants(
    const D3D8VertexShaderConstantApi& api,
    void* device,
    const D3D8SkinningShaderTransformState& state) noexcept;

[[nodiscard]] bool VerifyD3D8SkinningShaderConstants(
    const D3D8VertexShaderConstantApi& api,
    void* device,
    const D3D8SkinningShaderTransformState& state) noexcept;

} // namespace bfvr::d3d8probe
