#include "client/D3D8StereoShaderTransform.h"

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

bool NearlyEqual(
    const bfvr::d3d8probe::D3DMatrix& lhs,
    const bfvr::d3d8probe::D3DMatrix& rhs) noexcept
{
    constexpr float absoluteTolerance = 0.0005F;
    constexpr float relativeTolerance = 0.002F;
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            const float left = lhs.values[row][column];
            const float right = rhs.values[row][column];
            const float tolerance = absoluteTolerance +
                relativeTolerance * std::max(std::fabs(left), std::fabs(right));
            if (!std::isfinite(left) ||
                !std::isfinite(right) ||
                std::fabs(left - right) > tolerance)
            {
                return false;
            }
        }
    }
    return true;
}

} // namespace

namespace bfvr::d3d8probe
{

SkinningShaderPrepareResult PrepareD3D8SkinningShaderTransforms(
    const D3D8VertexShaderConstantApi& api,
    void* device,
    const D3DMatrix& world,
    const D3DMatrix& sourceView,
    const D3DMatrix& sourceProjection,
    const D3DMatrix& leftView,
    const D3DMatrix& leftProjection,
    const D3DMatrix& rightView,
    const D3DMatrix& rightProjection,
    D3D8SkinningShaderTransformState& state) noexcept
{
    state = {};
    if (device == nullptr ||
        api.setConstants == nullptr ||
        api.getConstants == nullptr)
    {
        return SkinningShaderPrepareResult::InvalidApi;
    }
    if (FAILED(api.getConstants(device, 0, &state.originalConstants, 4)))
    {
        return SkinningShaderPrepareResult::StateReadFailed;
    }

    const auto sourceConstants = bfvr::stereo::MakeD3D8SkinningShaderConstants(
        ToMatrix4(world),
        ToMatrix4(sourceView),
        ToMatrix4(sourceProjection));
    const auto leftConstants = bfvr::stereo::MakeD3D8SkinningShaderConstants(
        ToMatrix4(world),
        ToMatrix4(leftView),
        ToMatrix4(leftProjection));
    const auto rightConstants = bfvr::stereo::MakeD3D8SkinningShaderConstants(
        ToMatrix4(world),
        ToMatrix4(rightView),
        ToMatrix4(rightProjection));
    if (!sourceConstants.has_value() ||
        !leftConstants.has_value() ||
        !rightConstants.has_value())
    {
        return SkinningShaderPrepareResult::InvalidTransform;
    }

    if (!NearlyEqual(
            state.originalConstants,
            ToD3DMatrix(*sourceConstants)))
    {
        return SkinningShaderPrepareResult::SourceConstantsMismatch;
    }
    state.eyeConstants[0] = ToD3DMatrix(*leftConstants);
    state.eyeConstants[1] = ToD3DMatrix(*rightConstants);
    state.prepared = true;
    return SkinningShaderPrepareResult::Prepared;
}

HRESULT ApplyD3D8SkinningShaderEye(
    const D3D8VertexShaderConstantApi& api,
    void* device,
    const D3D8SkinningShaderTransformState& state,
    std::size_t eye) noexcept
{
    if (!state.prepared ||
        eye >= state.eyeConstants.size() ||
        device == nullptr ||
        api.setConstants == nullptr)
    {
        return E_INVALIDARG;
    }
    return api.setConstants(device, 0, &state.eyeConstants[eye], 4);
}

bool RestoreAndVerifyD3D8SkinningShaderConstants(
    const D3D8VertexShaderConstantApi& api,
    void* device,
    const D3D8SkinningShaderTransformState& state) noexcept
{
    if (!state.prepared)
    {
        return true;
    }
    if (device == nullptr ||
        api.setConstants == nullptr ||
        api.getConstants == nullptr ||
        FAILED(api.setConstants(device, 0, &state.originalConstants, 4)))
    {
        return false;
    }
    D3DMatrix actual = {};
    return SUCCEEDED(api.getConstants(device, 0, &actual, 4)) &&
        std::memcmp(&actual, &state.originalConstants, sizeof(actual)) == 0;
}

} // namespace bfvr::d3d8probe
