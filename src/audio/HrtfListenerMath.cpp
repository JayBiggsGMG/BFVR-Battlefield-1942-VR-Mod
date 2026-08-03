#include "audio/HrtfListenerMath.h"

#include <cmath>

namespace bfvr::audio
{
namespace
{
bool IsFinite(const stereo::Vec3& value) noexcept
{
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

stereo::Vec3 Cross(
    const stereo::Vec3& left,
    const stereo::Vec3& right) noexcept
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x};
}

std::optional<stereo::Vec3> Normalize(stereo::Vec3 value) noexcept
{
    const float squared =
        value.x * value.x + value.y * value.y + value.z * value.z;
    if (!std::isfinite(squared) || squared < 1.0e-8F)
    {
        return std::nullopt;
    }
    const float inverseLength = 1.0F / std::sqrt(squared);
    value.x *= inverseLength;
    value.y *= inverseLength;
    value.z *= inverseLength;
    return value;
}
} // namespace

std::optional<ListenerTransform> ComposeHrtfListener(
    const ListenerTransform& nativeListener,
    const stereo::Pose& currentHead,
    float worldUnitsPerMeter) noexcept
{
    if (!IsFinite(nativeListener.position) ||
        !std::isfinite(worldUnitsPerMeter) ||
        worldUnitsPerMeter <= 0.0F)
    {
        return std::nullopt;
    }

    const auto front = Normalize(nativeListener.front);
    const auto initialTop = Normalize(nativeListener.top);
    if (!front || !initialTop)
    {
        return std::nullopt;
    }
    const auto right = Normalize(Cross(*initialTop, *front));
    if (!right)
    {
        return std::nullopt;
    }
    const auto top = Normalize(Cross(*front, *right));
    if (!top)
    {
        return std::nullopt;
    }

    stereo::Matrix4 source = {};
    source.values[0] = {right->x, right->y, right->z, 0.0F};
    source.values[1] = {top->x, top->y, top->z, 0.0F};
    source.values[2] = {front->x, front->y, front->z, 0.0F};
    source.values[3] = {
        nativeListener.position.x,
        nativeListener.position.y,
        nativeListener.position.z,
        1.0F};

    const stereo::Pose localOrigin = {};
    const auto composed = stereo::ComposeRuntimeHeadWithD3D8Camera(
        source,
        localOrigin,
        currentHead,
        worldUnitsPerMeter);
    if (!composed)
    {
        return std::nullopt;
    }

    ListenerTransform result = {};
    result.position = {
        composed->values[3][0],
        composed->values[3][1],
        composed->values[3][2]};
    const auto composedTop = Normalize({
        composed->values[1][0],
        composed->values[1][1],
        composed->values[1][2]});
    const auto composedFront = Normalize({
        composed->values[2][0],
        composed->values[2][1],
        composed->values[2][2]});
    if (!IsFinite(result.position) || !composedTop || !composedFront)
    {
        return std::nullopt;
    }
    result.top = *composedTop;
    result.front = *composedFront;
    return result;
}
} // namespace bfvr::audio
