#pragma once

#include "stereo/StereoMath.h"

#include <cstdint>
#include <optional>

namespace bfvr::stereo
{

struct BF1942TreeAngleSliceContext
{
    Vec3 origin = {};
    float referenceAxisX = 0.0F;
    float referenceAxisZ = 0.0F;
    float angleAxisX = 0.0F;
    float angleAxisZ = 0.0F;
    std::uint32_t angleCount = 0;
    std::uint32_t centreAngleIndex = 0;
};

// Mirrors the retail TreeMesh::getAngle/getAngleIndex calculation for one
// camera position without invoking game code.
[[nodiscard]] std::optional<std::uint32_t>
SelectBF1942TreeAngleSlice(
    const BF1942TreeAngleSliceContext& context,
    const Vec3& cameraPosition) noexcept;

// TreeMesh group 0 stores each camera-angle index set consecutively. Preserve
// the block's angle-zero base and substitute only the target eye's index.
[[nodiscard]] std::optional<std::uint32_t>
RemapBF1942TreeAngleSliceStartIndex(
    std::uint32_t originalStartIndex,
    std::uint32_t primitiveCount,
    std::uint32_t centreAngleIndex,
    std::uint32_t eyeAngleIndex) noexcept;

} // namespace bfvr::stereo
