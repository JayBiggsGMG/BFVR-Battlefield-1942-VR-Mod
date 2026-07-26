#include "stereo/D3D8ShaderTransform.h"

#include <cmath>

namespace
{

bool IsFinite(const bfvr::stereo::Matrix4& matrix) noexcept
{
    for (const auto& row : matrix.values)
    {
        for (const float element : row)
        {
            if (!std::isfinite(element))
            {
                return false;
            }
        }
    }
    return true;
}

bool IsFinite(const bfvr::stereo::Vec4& vector) noexcept
{
    return std::isfinite(vector.x) &&
        std::isfinite(vector.y) &&
        std::isfinite(vector.z) &&
        std::isfinite(vector.w);
}

bfvr::stereo::Matrix4 Multiply(
    const bfvr::stereo::Matrix4& lhs,
    const bfvr::stereo::Matrix4& rhs) noexcept
{
    bfvr::stereo::Matrix4 result = {};
    for (std::size_t row = 0; row < result.values.size(); ++row)
    {
        for (std::size_t column = 0;
             column < result.values[row].size();
             ++column)
        {
            for (std::size_t inner = 0; inner < result.values.size(); ++inner)
            {
                result.values[row][column] +=
                    lhs.values[row][inner] * rhs.values[inner][column];
            }
        }
    }
    return result;
}

bfvr::stereo::Matrix4 Transpose(
    const bfvr::stereo::Matrix4& matrix) noexcept
{
    bfvr::stereo::Matrix4 result = {};
    for (std::size_t row = 0; row < result.values.size(); ++row)
    {
        for (std::size_t column = 0;
             column < result.values[row].size();
             ++column)
        {
            result.values[row][column] = matrix.values[column][row];
        }
    }
    return result;
}

} // namespace

namespace bfvr::stereo
{

std::optional<Matrix4> MakeD3D8SkinningShaderConstants(
    const Matrix4& world,
    const Matrix4& view,
    const Matrix4& projection) noexcept
{
    if (!IsFinite(world) || !IsFinite(view) || !IsFinite(projection))
    {
        return std::nullopt;
    }
    const Matrix4 worldViewProjection =
        Multiply(Multiply(world, view), projection);
    const Matrix4 constants = Transpose(worldViewProjection);
    return IsFinite(constants)
        ? std::optional<Matrix4>(constants)
        : std::nullopt;
}

std::optional<D3D8SpriteShaderConstants>
MakeD3D8SpriteShaderConstants(
    const Matrix4& view,
    const Matrix4& projection,
    float cameraPositionW) noexcept
{
    if (!IsFinite(view) ||
        !IsFinite(projection) ||
        !std::isfinite(cameraPositionW))
    {
        return std::nullopt;
    }

    // D3D row-vector affine view: worldPosition * rotation + translation.
    // For an orthonormal camera rotation, cameraPosition =
    // -translation * transpose(rotation).
    const Vec4 cameraPosition = {
        -(view.values[3][0] * view.values[0][0] +
          view.values[3][1] * view.values[0][1] +
          view.values[3][2] * view.values[0][2]),
        -(view.values[3][0] * view.values[1][0] +
          view.values[3][1] * view.values[1][1] +
          view.values[3][2] * view.values[1][2]),
        -(view.values[3][0] * view.values[2][0] +
          view.values[3][1] * view.values[2][1] +
          view.values[3][2] * view.values[2][2]),
        cameraPositionW};

    Matrix4 rotationOnlyView = view;
    rotationOnlyView.values[3][0] = 0.0F;
    rotationOnlyView.values[3][1] = 0.0F;
    rotationOnlyView.values[3][2] = 0.0F;

    const D3D8SpriteShaderConstants constants = {
        Transpose(rotationOnlyView),
        Transpose(projection),
        cameraPosition};
    if (!IsFinite(constants.rotationOnlyView) ||
        !IsFinite(constants.projection) ||
        !IsFinite(constants.cameraPosition))
    {
        return std::nullopt;
    }
    return constants;
}

std::optional<D3D8TreeSpriteShaderConstants>
MakeD3D8TreeSpriteShaderConstants(
    const Matrix4& world,
    const Matrix4& view,
    const Matrix4& projection) noexcept
{
    if (!IsFinite(world) || !IsFinite(view) || !IsFinite(projection))
    {
        return std::nullopt;
    }

    const D3D8TreeSpriteShaderConstants constants = {
        Transpose(Multiply(world, view)),
        Transpose(projection)};
    if (!IsFinite(constants.worldView) ||
        !IsFinite(constants.projection))
    {
        return std::nullopt;
    }
    return constants;
}

} // namespace bfvr::stereo
