#include "stereo/D3D8WeaponDrawPolicy.h"

#include <cmath>

namespace
{

using bfvr::stereo::Matrix4;
using bfvr::stereo::Vec3;

bool IsFinite(float value) noexcept
{
    return std::isfinite(value);
}

bool IsFinite(const Matrix4& matrix) noexcept
{
    for (const auto& row : matrix.values)
    {
        for (float element : row)
        {
            if (!IsFinite(element))
            {
                return false;
            }
        }
    }
    return true;
}

bool IsRigidD3D8View(const Matrix4& view) noexcept
{
    if (!IsFinite(view) || std::fabs(view.values[0][3]) > 0.001F ||
        std::fabs(view.values[1][3]) > 0.001F ||
        std::fabs(view.values[2][3]) > 0.001F ||
        std::fabs(view.values[3][3] - 1.0F) > 0.001F)
    {
        return false;
    }
    constexpr float kLengthTolerance = 0.01F;
    constexpr float kDotTolerance = 0.01F;
    for (std::size_t row = 0; row < 3; ++row)
    {
        float lengthSquared = 0.0F;
        for (std::size_t column = 0; column < 3; ++column)
        {
            lengthSquared += view.values[row][column] * view.values[row][column];
        }
        if (std::fabs(lengthSquared - 1.0F) > kLengthTolerance)
        {
            return false;
        }
        for (std::size_t other = row + 1; other < 3; ++other)
        {
            float dot = 0.0F;
            for (std::size_t column = 0; column < 3; ++column)
            {
                dot += view.values[row][column] * view.values[other][column];
            }
            if (std::fabs(dot) > kDotTolerance)
            {
                return false;
            }
        }
    }
    return true;
}

Vec3 CameraWorldPositionFromRigidView(const Matrix4& view) noexcept
{
    const Vec3 translation = {
        view.values[3][0], view.values[3][1], view.values[3][2]};
    return {
        -(translation.x * view.values[0][0] +
          translation.y * view.values[0][1] +
          translation.z * view.values[0][2]),
        -(translation.x * view.values[1][0] +
          translation.y * view.values[1][1] +
          translation.z * view.values[1][2]),
        -(translation.x * view.values[2][0] +
          translation.y * view.values[2][1] +
          translation.z * view.values[2][2])};
}

} // namespace

namespace bfvr::stereo
{

WeaponDrawDisposition ClassifyWeaponDraw(
    const WeaponDrawPolicyInput& input,
    float maximumCameraDistance) noexcept
{
    if (!input.indexedDraw ||
        input.rendererRoute != WeaponRendererRoute::GenericMesh ||
        input.vertexShaderOrFvf != 0x112U || input.alphaBlendEnabled ||
        !input.zEnabled || !input.firstPersonProjection ||
        !input.worldKnown || !input.viewKnown ||
        !IsFinite(maximumCameraDistance) || maximumCameraDistance <= 0.0F ||
        !IsFinite(input.world) || !IsRigidD3D8View(input.view))
    {
        return WeaponDrawDisposition::Unclassified;
    }

    const Vec3 camera = CameraWorldPositionFromRigidView(input.view);
    const float dx = input.world.values[3][0] - camera.x;
    const float dy = input.world.values[3][1] - camera.y;
    const float dz = input.world.values[3][2] - camera.z;
    const float distanceSquared = dx * dx + dy * dy + dz * dz;
    if (!IsFinite(distanceSquared) ||
        distanceSquared > maximumCameraDistance * maximumCameraDistance)
    {
        return WeaponDrawDisposition::Unclassified;
    }

    const Vec3 cameraLocal = {
        input.world.values[3][0] * input.view.values[0][0] +
            input.world.values[3][1] * input.view.values[1][0] +
            input.world.values[3][2] * input.view.values[2][0] +
            input.view.values[3][0],
        input.world.values[3][0] * input.view.values[0][1] +
            input.world.values[3][1] * input.view.values[1][1] +
            input.world.values[3][2] * input.view.values[2][1] +
            input.view.values[3][1],
        input.world.values[3][0] * input.view.values[0][2] +
            input.world.values[3][1] * input.view.values[1][2] +
            input.world.values[3][2] * input.view.values[2][2] +
            input.view.values[3][2]};
    constexpr float kMaximumHeldObjectLateralDistance = 0.75F;
    if (!IsFinite(cameraLocal.x) ||
        !IsFinite(cameraLocal.y) ||
        !IsFinite(cameraLocal.z) ||
        std::fabs(cameraLocal.x) >
            kMaximumHeldObjectLateralDistance ||
        std::fabs(cameraLocal.y) >
            kMaximumHeldObjectLateralDistance ||
        std::fabs(cameraLocal.z) > maximumCameraDistance)
    {
        return WeaponDrawDisposition::Unclassified;
    }
    return WeaponDrawDisposition::SharedFixedFunctionWeaponCandidate;
}

} // namespace bfvr::stereo
