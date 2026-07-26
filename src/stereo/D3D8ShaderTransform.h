#pragma once

#include "stereo/StereoMath.h"

#include <optional>

namespace bfvr::stereo
{

struct D3D8SpriteShaderConstants
{
    Matrix4 rotationOnlyView = {};
    Matrix4 projection = {};
    Vec4 cameraPosition = {};
};

struct D3D8TreeSpriteShaderConstants
{
    Matrix4 worldView = {};
    Matrix4 projection = {};
};

// D3D8 vertex-shader assembly consumes m4x4 constants as four column
// dot-products. Refractor 2 therefore uploads transpose(World * View *
// Projection) to SkinningShader2Bones registers c0-c3.
[[nodiscard]] std::optional<Matrix4> MakeD3D8SkinningShaderConstants(
    const Matrix4& world,
    const Matrix4& view,
    const Matrix4& projection) noexcept;

// TranslucentBucketDB removes translation from View and uploads its transpose
// to c0-c3, then uploads transpose(Projection) to c4-c7. Sprite shaders use
// c9.xyz as the camera position, so each eye must update that value as well.
[[nodiscard]] std::optional<D3D8SpriteShaderConstants>
MakeD3D8SpriteShaderConstants(
    const Matrix4& view,
    const Matrix4& projection,
    float cameraPositionW) noexcept;

// TreeMesh::realDraw uploads transpose(World * View) to c0-c3 and
// transpose(Projection) to c4-c7 before its programmable tree-sprite branch.
[[nodiscard]] std::optional<D3D8TreeSpriteShaderConstants>
MakeD3D8TreeSpriteShaderConstants(
    const Matrix4& world,
    const Matrix4& view,
    const Matrix4& projection) noexcept;

} // namespace bfvr::stereo
