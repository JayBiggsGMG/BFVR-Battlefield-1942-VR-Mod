#include "client/D3D8TreeSpriteShaderTransform.h"

#include "stereo/D3D8ShaderTransform.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{

bfvr::stereo::Matrix4 ToMatrix4(
    const bfvr::d3d8probe::D3DMatrix& matrix) noexcept
{
    bfvr::stereo::Matrix4 result = {};
    std::memcpy(&result, &matrix, sizeof(result));
    return result;
}

bfvr::d3d8probe::D3DMatrix ToD3DMatrix(
    const bfvr::stereo::Matrix4& matrix) noexcept
{
    bfvr::d3d8probe::D3DMatrix result = {};
    std::memcpy(&result, &matrix, sizeof(result));
    return result;
}

bool NearlyEqual(float lhs, float rhs) noexcept
{
    constexpr float absoluteTolerance = 0.0005F;
    constexpr float relativeTolerance = 0.002F;
    return std::isfinite(lhs) &&
        std::isfinite(rhs) &&
        std::fabs(lhs - rhs) <=
            absoluteTolerance +
                relativeTolerance * std::max(std::fabs(lhs), std::fabs(rhs));
}

bool NearlyEqual(
    const float* lhs,
    const float* rhs,
    std::size_t count) noexcept
{
    for (std::size_t index = 0; index < count; ++index)
    {
        if (!NearlyEqual(lhs[index], rhs[index]))
        {
            return false;
        }
    }
    return true;
}

bfvr::d3d8probe::D3D8TreeSpriteShaderEyeConstants ToEyeConstants(
    const bfvr::stereo::D3D8TreeSpriteShaderConstants& constants) noexcept
{
    return {
        ToD3DMatrix(constants.worldView),
        ToD3DMatrix(constants.projection)};
}

} // namespace

namespace bfvr::d3d8probe
{

TreeSpriteShaderPrepareResult PrepareD3D8TreeSpriteShaderTransforms(
    const D3D8VertexShaderConstantApi& api,
    void* device,
    const D3DMatrix& world,
    const D3DMatrix& sourceView,
    const D3DMatrix& sourceProjection,
    const D3DMatrix& leftView,
    const D3DMatrix& leftProjection,
    const D3DMatrix& rightView,
    const D3DMatrix& rightProjection,
    D3D8TreeSpriteShaderTransformState& state) noexcept
{
    state = {};
    if (device == nullptr ||
        api.setConstants == nullptr ||
        api.getConstants == nullptr)
    {
        return TreeSpriteShaderPrepareResult::InvalidApi;
    }
    if (FAILED(api.getConstants(
            device,
            0,
            &state.originalRegisters,
            kD3D8TreeSpriteShaderRegisterCount)))
    {
        return TreeSpriteShaderPrepareResult::StateReadFailed;
    }

    const auto sourceConstants =
        bfvr::stereo::MakeD3D8TreeSpriteShaderConstants(
            ToMatrix4(world),
            ToMatrix4(sourceView),
            ToMatrix4(sourceProjection));
    const auto leftConstants =
        bfvr::stereo::MakeD3D8TreeSpriteShaderConstants(
            ToMatrix4(world),
            ToMatrix4(leftView),
            ToMatrix4(leftProjection));
    const auto rightConstants =
        bfvr::stereo::MakeD3D8TreeSpriteShaderConstants(
            ToMatrix4(world),
            ToMatrix4(rightView),
            ToMatrix4(rightProjection));
    if (!sourceConstants.has_value() ||
        !leftConstants.has_value() ||
        !rightConstants.has_value())
    {
        return TreeSpriteShaderPrepareResult::InvalidTransform;
    }

    const auto source = ToEyeConstants(*sourceConstants);
    if (!NearlyEqual(
            &state.originalRegisters.values[0][0],
            &source.worldView.values[0][0],
            16) ||
        !NearlyEqual(
            &state.originalRegisters.values[4][0],
            &source.projection.values[0][0],
            16))
    {
        return TreeSpriteShaderPrepareResult::SourceConstantsMismatch;
    }

    state.eyeConstants[0] = ToEyeConstants(*leftConstants);
    state.eyeConstants[1] = ToEyeConstants(*rightConstants);
    state.prepared = true;
    return TreeSpriteShaderPrepareResult::Prepared;
}

HRESULT ApplyD3D8TreeSpriteShaderEye(
    const D3D8VertexShaderConstantApi& api,
    void* device,
    const D3D8TreeSpriteShaderTransformState& state,
    std::size_t eye) noexcept
{
    if (!state.prepared ||
        eye >= state.eyeConstants.size() ||
        device == nullptr ||
        api.setConstants == nullptr)
    {
        return E_INVALIDARG;
    }

    return api.setConstants(
        device,
        0,
        &state.eyeConstants[eye],
        kD3D8TreeSpriteShaderRegisterCount);
}

bool RestoreAndVerifyD3D8TreeSpriteShaderConstants(
    const D3D8VertexShaderConstantApi& api,
    void* device,
    const D3D8TreeSpriteShaderTransformState& state) noexcept
{
    if (!state.prepared)
    {
        return true;
    }
    if (device == nullptr ||
        api.setConstants == nullptr ||
        api.getConstants == nullptr ||
        FAILED(api.setConstants(
            device,
            0,
            &state.originalRegisters,
            kD3D8TreeSpriteShaderRegisterCount)))
    {
        return false;
    }

    D3D8TreeSpriteShaderRegisterBlock actual = {};
    return SUCCEEDED(api.getConstants(
               device,
               0,
               &actual,
               kD3D8TreeSpriteShaderRegisterCount)) &&
        std::memcmp(
            &actual,
            &state.originalRegisters,
            sizeof(actual)) == 0;
}

} // namespace bfvr::d3d8probe
