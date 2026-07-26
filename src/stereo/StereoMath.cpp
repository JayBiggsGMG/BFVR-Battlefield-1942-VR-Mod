#include "stereo/StereoMath.h"

#include <cmath>

namespace
{
bool IsFinite(float value) noexcept
{
    return std::isfinite(value);
}

bool IsFinite(const bfvr::stereo::Vec3& value) noexcept
{
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
}

bool IsFinite(const bfvr::stereo::Quaternion& value) noexcept
{
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z) && IsFinite(value.w);
}

bool IsFinite(const bfvr::stereo::Matrix4& value) noexcept
{
    for (const auto& row : value.values)
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

std::optional<bfvr::stereo::Quaternion> Normalize(const bfvr::stereo::Quaternion& value) noexcept
{
    if (!IsFinite(value))
    {
        return std::nullopt;
    }

    const float lengthSquared =
        value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
    if (!IsFinite(lengthSquared) || lengthSquared <= 0.0F)
    {
        return std::nullopt;
    }

    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    if (!IsFinite(inverseLength))
    {
        return std::nullopt;
    }

    return bfvr::stereo::Quaternion{
        value.x * inverseLength,
        value.y * inverseLength,
        value.z * inverseLength,
        value.w * inverseLength};
}

bfvr::stereo::Vec3 Add(const bfvr::stereo::Vec3& lhs, const bfvr::stereo::Vec3& rhs) noexcept
{
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

bfvr::stereo::Vec3 Subtract(
    const bfvr::stereo::Vec3& lhs,
    const bfvr::stereo::Vec3& rhs) noexcept
{
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

bfvr::stereo::Vec3 Cross(const bfvr::stereo::Vec3& lhs, const bfvr::stereo::Vec3& rhs) noexcept
{
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x};
}

bfvr::stereo::Vec3 Scale(const bfvr::stereo::Vec3& value, float scalar) noexcept
{
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

bfvr::stereo::Vec3 Rotate(const bfvr::stereo::Quaternion& orientation, const bfvr::stereo::Vec3& value) noexcept
{
    const bfvr::stereo::Vec3 axis{orientation.x, orientation.y, orientation.z};
    const bfvr::stereo::Vec3 doubledCross = Scale(Cross(axis, value), 2.0F);
    return Add(value, Add(Scale(doubledCross, orientation.w), Cross(axis, doubledCross)));
}

bfvr::stereo::Quaternion Conjugate(
    const bfvr::stereo::Quaternion& value) noexcept
{
    return {-value.x, -value.y, -value.z, value.w};
}

bfvr::stereo::Quaternion Multiply(
    const bfvr::stereo::Quaternion& lhs,
    const bfvr::stereo::Quaternion& rhs) noexcept
{
    return {
        lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.w * rhs.y - lhs.x * rhs.z + lhs.y * rhs.w + lhs.z * rhs.x,
        lhs.w * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.w,
        lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z};
}

bfvr::stereo::Matrix4 Multiply(
    const bfvr::stereo::Matrix4& lhs,
    const bfvr::stereo::Matrix4& rhs) noexcept
{
    bfvr::stereo::Matrix4 result = {};
    for (std::size_t row = 0; row < result.values.size(); ++row)
    {
        for (std::size_t column = 0; column < result.values[row].size(); ++column)
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

std::optional<bfvr::stereo::Pose> MakeRelativePose(
    const bfvr::stereo::Pose& reference,
    const bfvr::stereo::Pose& current,
    float positionScale) noexcept
{
    if (!IsFinite(reference.position) ||
        !IsFinite(current.position) ||
        !IsFinite(positionScale) ||
        positionScale <= 0.0F)
    {
        return std::nullopt;
    }
    const auto referenceOrientation = Normalize(reference.orientation);
    const auto currentOrientation = Normalize(current.orientation);
    if (!referenceOrientation.has_value() ||
        !currentOrientation.has_value())
    {
        return std::nullopt;
    }
    const bfvr::stereo::Quaternion inverseReference =
        Conjugate(*referenceOrientation);
    bfvr::stereo::Pose relative = {};
    relative.position = Scale(
        Rotate(
            inverseReference,
            Subtract(current.position, reference.position)),
        positionScale);
    relative.orientation =
        Multiply(inverseReference, *currentOrientation);
    return IsFinite(relative.position) && IsFinite(relative.orientation)
        ? std::optional<bfvr::stereo::Pose>(relative)
        : std::nullopt;
}

std::optional<bfvr::stereo::Matrix4> MakeD3D8PoseFromOpenXRPose(
    const bfvr::stereo::Pose& pose) noexcept
{
    const auto view =
        bfvr::stereo::MakeD3D8ViewFromOpenXRPose(pose);
    if (!view.has_value())
    {
        return std::nullopt;
    }
    bfvr::stereo::Matrix4 result = {};
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            result.values[row][column] =
                view->values[column][row];
        }
    }
    result.values[3][0] =
        bfvr::stereo::OpenXRToD3D8Coordinates(pose.position).x;
    result.values[3][1] =
        bfvr::stereo::OpenXRToD3D8Coordinates(pose.position).y;
    result.values[3][2] =
        bfvr::stereo::OpenXRToD3D8Coordinates(pose.position).z;
    result.values[3][3] = 1.0F;
    return result;
}
}

namespace bfvr::stereo
{
Vec3 OpenXRToD3D8Coordinates(Vec3 value) noexcept
{
    return {value.x, value.y, -value.z};
}

std::optional<EyePoses> ComputeEyePoses(const Pose& headPose, float ipdMeters) noexcept
{
    if (!IsFinite(headPose.position) || !IsFinite(ipdMeters) || ipdMeters < 0.0F)
    {
        return std::nullopt;
    }

    const std::optional<Quaternion> orientation = Normalize(headPose.orientation);
    if (!orientation.has_value())
    {
        return std::nullopt;
    }

    const float halfIpd = ipdMeters * 0.5F;
    if (!IsFinite(halfIpd))
    {
        return std::nullopt;
    }

    EyePoses eyes = {};
    eyes.left.orientation = *orientation;
    eyes.right.orientation = *orientation;
    eyes.left.position = Add(headPose.position, Rotate(*orientation, {-halfIpd, 0.0F, 0.0F}));
    eyes.right.position = Add(headPose.position, Rotate(*orientation, {halfIpd, 0.0F, 0.0F}));
    return eyes;
}

std::optional<Matrix4> MakeD3D8ViewFromOpenXRPose(const Pose& eyePose) noexcept
{
    if (!IsFinite(eyePose.position))
    {
        return std::nullopt;
    }

    const std::optional<Quaternion> orientation = Normalize(eyePose.orientation);
    if (!orientation.has_value())
    {
        return std::nullopt;
    }

    const float x = orientation->x;
    const float y = orientation->y;
    const float z = orientation->z;
    const float w = orientation->w;
    const float xx = x * x;
    const float yy = y * y;
    const float zz = z * z;
    const float xy = x * y;
    const float xz = x * z;
    const float yz = y * z;
    const float xw = x * w;
    const float yw = y * w;
    const float zw = z * w;

    // This is the OpenXR pose rotation in conventional column-vector form.
    const float openXRRotation[3][3] = {
        {1.0F - 2.0F * (yy + zz), 2.0F * (xy - zw), 2.0F * (xz + yw)},
        {2.0F * (xy + zw), 1.0F - 2.0F * (xx + zz), 2.0F * (yz - xw)},
        {2.0F * (xz - yw), 2.0F * (yz + xw), 1.0F - 2.0F * (xx + yy)}};

    // C * R_openxr * C, where C flips Z between OpenXR's right-handed world
    // and D3D8's conventional left-handed world.
    constexpr float coordinateSign[3] = {1.0F, 1.0F, -1.0F};
    float d3dRotation[3][3] = {};
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            d3dRotation[row][column] = coordinateSign[row] * openXRRotation[row][column] * coordinateSign[column];
        }
    }

    const Vec3 d3dPosition = OpenXRToD3D8Coordinates(eyePose.position);
    const float d3dPositionComponents[3] = {d3dPosition.x, d3dPosition.y, d3dPosition.z};
    Matrix4 result = {};
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            // The D3D8 row-vector representation is the transpose of the
            // usual column-vector rigid inverse, leaving R here in the upper
            // 3x3 and placing -R^T * position in the final row.
            result.values[row][column] = d3dRotation[row][column];
        }
    }

    for (int component = 0; component < 3; ++component)
    {
        float translatedComponent = 0.0F;
        for (int column = 0; column < 3; ++column)
        {
            translatedComponent -= d3dRotation[column][component] * d3dPositionComponents[column];
        }
        result.values[3][component] = translatedComponent;
    }
    result.values[3][3] = 1.0F;
    return result;
}

std::optional<Matrix4> MakeD3D8ProjectionFromFovTangents(
    const FovTangents& fov,
    float nearZ,
    float farZ) noexcept
{
    if (!IsFinite(fov.left) || !IsFinite(fov.right) || !IsFinite(fov.up) || !IsFinite(fov.down) ||
        !IsFinite(nearZ) || !IsFinite(farZ) || nearZ <= 0.0F || farZ <= nearZ ||
        fov.left >= fov.right || fov.down >= fov.up)
    {
        return std::nullopt;
    }

    const float left = nearZ * fov.left;
    const float right = nearZ * fov.right;
    const float top = nearZ * fov.up;
    const float bottom = nearZ * fov.down;
    const float horizontalWidth = right - left;
    const float verticalHeight = top - bottom;
    const float depthRange = farZ - nearZ;
    if (!IsFinite(left) || !IsFinite(right) || !IsFinite(top) || !IsFinite(bottom) ||
        !IsFinite(horizontalWidth) || !IsFinite(verticalHeight) || !IsFinite(depthRange) ||
        horizontalWidth <= 0.0F || verticalHeight <= 0.0F || depthRange <= 0.0F)
    {
        return std::nullopt;
    }

    Matrix4 result = {};
    result.values[0][0] = 2.0F * nearZ / horizontalWidth;
    result.values[1][1] = 2.0F * nearZ / verticalHeight;
    result.values[2][0] = (left + right) / (left - right);
    result.values[2][1] = (top + bottom) / (bottom - top);
    result.values[2][2] = farZ / depthRange;
    result.values[2][3] = 1.0F;
    result.values[3][2] = -(nearZ * farZ) / depthRange;
    for (const auto& row : result.values)
    {
        for (float element : row)
        {
            if (!IsFinite(element))
            {
                return std::nullopt;
            }
        }
    }
    return result;
}

std::optional<D3D8StereoTransformPair> MakeDiagnosticD3D8StereoPair(
    const Matrix4& sourceView,
    const Matrix4& sourceProjection,
    float halfEyeOffset,
    float convergenceDistance) noexcept
{
    if (!IsFinite(sourceView) ||
        !IsFinite(sourceProjection) ||
        !IsFinite(halfEyeOffset) ||
        !IsFinite(convergenceDistance) ||
        halfEyeOffset <= 0.0F ||
        convergenceDistance <= 0.0F ||
        sourceProjection.values[0][0] == 0.0F)
    {
        return std::nullopt;
    }

    const float projectionShift =
        halfEyeOffset * sourceProjection.values[0][0] / convergenceDistance;
    if (!IsFinite(projectionShift))
    {
        return std::nullopt;
    }

    D3D8StereoTransformPair result = {};
    result.leftView = sourceView;
    result.rightView = sourceView;
    result.leftProjection = sourceProjection;
    result.rightProjection = sourceProjection;

    // Moving the camera left makes world points move right in view space.
    result.leftView.values[3][0] += halfEyeOffset;
    result.rightView.values[3][0] -= halfEyeOffset;
    result.leftProjection.values[2][0] -= projectionShift;
    result.rightProjection.values[2][0] += projectionShift;
    if (!IsFinite(result.leftView) ||
        !IsFinite(result.rightView) ||
        !IsFinite(result.leftProjection) ||
        !IsFinite(result.rightProjection))
    {
        return std::nullopt;
    }
    return result;
}

std::optional<D3D8StereoTransformPair> MakeRuntimeFovD3D8StereoPair(
    const Matrix4& sourceView,
    const Matrix4& sourceProjection,
    const Pose& leftEye,
    const Pose& rightEye,
    const FovTangents& leftFov,
    const FovTangents& rightFov,
    float worldUnitsPerMeter) noexcept
{
    if (!IsFinite(sourceView) ||
        !IsFinite(sourceProjection) ||
        !IsFinite(leftEye.position) ||
        !IsFinite(rightEye.position) ||
        !IsFinite(worldUnitsPerMeter) ||
        worldUnitsPerMeter <= 0.0F)
    {
        return std::nullopt;
    }

    const float deltaX = rightEye.position.x - leftEye.position.x;
    const float deltaY = rightEye.position.y - leftEye.position.y;
    const float deltaZ = rightEye.position.z - leftEye.position.z;
    const float eyeSeparationMeters =
        std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
    const float halfEyeOffset =
        eyeSeparationMeters * worldUnitsPerMeter * 0.5F;
    const float projectionM22 = sourceProjection.values[2][2];
    const float projectionM32 = sourceProjection.values[3][2];
    if (!IsFinite(eyeSeparationMeters) ||
        !IsFinite(halfEyeOffset) ||
        halfEyeOffset <= 0.0F ||
        projectionM22 == 0.0F ||
        projectionM22 == 1.0F)
    {
        return std::nullopt;
    }

    const float nearZ = -projectionM32 / projectionM22;
    const float farZ = -projectionM32 / (projectionM22 - 1.0F);
    const auto leftProjection =
        MakeD3D8ProjectionFromFovTangents(leftFov, nearZ, farZ);
    const auto rightProjection =
        MakeD3D8ProjectionFromFovTangents(rightFov, nearZ, farZ);
    if (!leftProjection.has_value() || !rightProjection.has_value())
    {
        return std::nullopt;
    }

    D3D8StereoTransformPair result = {};
    result.leftView = sourceView;
    result.rightView = sourceView;
    result.leftView.values[3][0] += halfEyeOffset;
    result.rightView.values[3][0] -= halfEyeOffset;
    result.leftProjection = *leftProjection;
    result.rightProjection = *rightProjection;
    return result;
}

std::optional<D3D8StereoTransformPair> MakeRuntimePoseD3D8StereoPair(
    const Matrix4& sourceView,
    const Matrix4& sourceProjection,
    const Pose& referenceHead,
    const Pose& leftEye,
    const Pose& rightEye,
    const FovTangents& leftFov,
    const FovTangents& rightFov,
    float worldUnitsPerMeter) noexcept
{
    if (!IsFinite(sourceView) ||
        !IsFinite(sourceProjection) ||
        !IsFinite(referenceHead.position) ||
        !IsFinite(leftEye.position) ||
        !IsFinite(rightEye.position) ||
        !IsFinite(worldUnitsPerMeter) ||
        worldUnitsPerMeter <= 0.0F)
    {
        return std::nullopt;
    }

    const auto relativeLeft =
        MakeRelativePose(referenceHead, leftEye, worldUnitsPerMeter);
    const auto relativeRight =
        MakeRelativePose(referenceHead, rightEye, worldUnitsPerMeter);
    if (!relativeLeft.has_value() || !relativeRight.has_value())
    {
        return std::nullopt;
    }
    const auto relativeLeftView =
        MakeD3D8ViewFromOpenXRPose(*relativeLeft);
    const auto relativeRightView =
        MakeD3D8ViewFromOpenXRPose(*relativeRight);
    if (!relativeLeftView.has_value() || !relativeRightView.has_value())
    {
        return std::nullopt;
    }

    const float projectionM22 = sourceProjection.values[2][2];
    const float projectionM32 = sourceProjection.values[3][2];
    if (projectionM22 == 0.0F || projectionM22 == 1.0F)
    {
        return std::nullopt;
    }
    const float nearZ = -projectionM32 / projectionM22;
    const float farZ = -projectionM32 / (projectionM22 - 1.0F);
    const auto leftProjection =
        MakeD3D8ProjectionFromFovTangents(leftFov, nearZ, farZ);
    const auto rightProjection =
        MakeD3D8ProjectionFromFovTangents(rightFov, nearZ, farZ);
    if (!leftProjection.has_value() || !rightProjection.has_value())
    {
        return std::nullopt;
    }

    D3D8StereoTransformPair result = {};
    result.leftView = Multiply(sourceView, *relativeLeftView);
    result.rightView = Multiply(sourceView, *relativeRightView);
    result.leftProjection = *leftProjection;
    result.rightProjection = *rightProjection;
    if (!IsFinite(result.leftView) ||
        !IsFinite(result.rightView))
    {
        return std::nullopt;
    }
    return result;
}

std::optional<Matrix4> ComposeRuntimeHeadWithD3D8Camera(
    const Matrix4& sourceCamera,
    const Pose& referenceHead,
    const Pose& currentHead,
    float worldUnitsPerMeter) noexcept
{
    if (!IsFinite(sourceCamera))
    {
        return std::nullopt;
    }
    const auto relativeHead =
        MakeRelativePose(
            referenceHead,
            currentHead,
            worldUnitsPerMeter);
    if (!relativeHead.has_value())
    {
        return std::nullopt;
    }
    const auto relativeCamera =
        MakeD3D8PoseFromOpenXRPose(*relativeHead);
    if (!relativeCamera.has_value())
    {
        return std::nullopt;
    }
    const Matrix4 result = Multiply(*relativeCamera, sourceCamera);
    return IsFinite(result)
        ? std::optional<Matrix4>(result)
        : std::nullopt;
}

Vec4 TransformRowVector(const Vec4& value, const Matrix4& matrix) noexcept
{
    return {
        value.x * matrix.values[0][0] + value.y * matrix.values[1][0] + value.z * matrix.values[2][0] + value.w * matrix.values[3][0],
        value.x * matrix.values[0][1] + value.y * matrix.values[1][1] + value.z * matrix.values[2][1] + value.w * matrix.values[3][1],
        value.x * matrix.values[0][2] + value.y * matrix.values[1][2] + value.z * matrix.values[2][2] + value.w * matrix.values[3][2],
        value.x * matrix.values[0][3] + value.y * matrix.values[1][3] + value.z * matrix.values[2][3] + value.w * matrix.values[3][3]};
}
}
