#include "client/D3D8SpriteShaderTransform.h"

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

bfvr::d3d8probe::D3D8SpriteShaderEyeConstants ToEyeConstants(
    const bfvr::stereo::D3D8SpriteShaderConstants& constants) noexcept
{
    bfvr::d3d8probe::D3D8SpriteShaderEyeConstants result = {};
    result.rotationOnlyView = ToD3DMatrix(constants.rotationOnlyView);
    result.projection = ToD3DMatrix(constants.projection);
    result.cameraPosition[0] = constants.cameraPosition.x;
    result.cameraPosition[1] = constants.cameraPosition.y;
    result.cameraPosition[2] = constants.cameraPosition.z;
    result.cameraPosition[3] = constants.cameraPosition.w;
    return result;
}

} // namespace

namespace bfvr::d3d8probe
{

SpriteShaderPrepareResult PrepareD3D8SpriteShaderTransforms(
    const D3D8VertexShaderConstantApi& api,
    void* device,
    const D3DMatrix& sourceView,
    const D3DMatrix& sourceProjection,
    const D3DMatrix& leftView,
    const D3DMatrix& leftProjection,
    const D3DMatrix& rightView,
    const D3DMatrix& rightProjection,
    D3D8SpriteShaderTransformState& state) noexcept
{
    state = {};
    if (device == nullptr ||
        api.setConstants == nullptr ||
        api.getConstants == nullptr)
    {
        return SpriteShaderPrepareResult::InvalidApi;
    }
    if (FAILED(api.getConstants(
            device,
            0,
            &state.originalRegisters,
            kD3D8SpriteShaderRegisterCount)))
    {
        return SpriteShaderPrepareResult::StateReadFailed;
    }

    const float cameraPositionW = state.originalRegisters.values[9][3];
    const auto sourceConstants = bfvr::stereo::MakeD3D8SpriteShaderConstants(
        ToMatrix4(sourceView),
        ToMatrix4(sourceProjection),
        cameraPositionW);
    const auto leftConstants = bfvr::stereo::MakeD3D8SpriteShaderConstants(
        ToMatrix4(leftView),
        ToMatrix4(leftProjection),
        cameraPositionW);
    const auto rightConstants = bfvr::stereo::MakeD3D8SpriteShaderConstants(
        ToMatrix4(rightView),
        ToMatrix4(rightProjection),
        cameraPositionW);
    if (!sourceConstants.has_value() ||
        !leftConstants.has_value() ||
        !rightConstants.has_value())
    {
        return SpriteShaderPrepareResult::InvalidTransform;
    }

    const auto source = ToEyeConstants(*sourceConstants);
    if (!NearlyEqual(
            &state.originalRegisters.values[0][0],
            &source.rotationOnlyView.values[0][0],
            16) ||
        !NearlyEqual(
            &state.originalRegisters.values[4][0],
            &source.projection.values[0][0],
            16) ||
        !NearlyEqual(
            &state.originalRegisters.values[9][0],
            source.cameraPosition,
            3))
    {
        return SpriteShaderPrepareResult::SourceConstantsMismatch;
    }

    state.eyeConstants[0] = ToEyeConstants(*leftConstants);
    state.eyeConstants[1] = ToEyeConstants(*rightConstants);
    state.prepared = true;
    return SpriteShaderPrepareResult::Prepared;
}

HRESULT ApplyD3D8SpriteShaderEye(
    const D3D8VertexShaderConstantApi& api,
    void* device,
    const D3D8SpriteShaderTransformState& state,
    std::size_t eye) noexcept
{
    if (!state.prepared ||
        eye >= state.eyeConstants.size() ||
        device == nullptr ||
        api.setConstants == nullptr)
    {
        return E_INVALIDARG;
    }

    const auto& constants = state.eyeConstants[eye];
    HRESULT result = api.setConstants(
        device,
        0,
        &constants.rotationOnlyView,
        4);
    if (SUCCEEDED(result))
    {
        result = api.setConstants(
            device,
            4,
            &constants.projection,
            4);
    }
    if (SUCCEEDED(result))
    {
        result = api.setConstants(
            device,
            9,
            constants.cameraPosition,
            1);
    }
    return result;
}

bool RestoreAndVerifyD3D8SpriteShaderConstants(
    const D3D8VertexShaderConstantApi& api,
    void* device,
    const D3D8SpriteShaderTransformState& state) noexcept
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
            kD3D8SpriteShaderRegisterCount)))
    {
        return false;
    }

    D3D8SpriteShaderRegisterBlock actual = {};
    return SUCCEEDED(api.getConstants(
               device,
               0,
               &actual,
               kD3D8SpriteShaderRegisterCount)) &&
        std::memcmp(
            &actual,
            &state.originalRegisters,
            sizeof(actual)) == 0;
}

} // namespace bfvr::d3d8probe
