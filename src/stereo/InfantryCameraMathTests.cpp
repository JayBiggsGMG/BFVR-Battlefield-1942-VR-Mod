#include "stereo/InfantryCameraMath.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
using bfvr::stereo::Matrix4;

constexpr float kTolerance = 0.0001F;

Matrix4 Identity() noexcept
{
    Matrix4 result = {};
    for (std::size_t index = 0; index < 4; ++index)
    {
        result.values[index][index] = 1.0F;
    }
    return result;
}

Matrix4 Yaw(float radians) noexcept
{
    Matrix4 result = Identity();
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    result.values[0][0] = cosine;
    result.values[0][2] = -sine;
    result.values[2][0] = sine;
    result.values[2][2] = cosine;
    return result;
}

bool Near(float actual, float expected) noexcept
{
    return std::fabs(actual - expected) <= kTolerance;
}

bool TestSourceRecoilIsReplacedByBodyFacing() noexcept
{
    Matrix4 source = Yaw(0.23F);
    const float pitch = 0.18F;
    source.values[1][1] = std::cos(pitch);
    source.values[1][2] = std::sin(pitch);
    source.values[2][1] = -std::sin(pitch);
    source.values[2][2] *= std::cos(pitch);
    source.values[3][0] = 12.0F;
    source.values[3][1] = 3.5F;
    source.values[3][2] = -7.0F;

    const Matrix4 body = Yaw(-0.41F);
    const auto result = bfvr::stereo::MakeD3D8InfantryComfortCamera(
        source,
        body);
    const Matrix4 expected = [&]() noexcept {
        Matrix4 value = body;
        value.values[3][0] = 12.0F;
        value.values[3][1] = 3.5F;
        value.values[3][2] = -7.0F;
        return value;
    }();
    if (!result.has_value())
    {
        return false;
    }
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            if (!Near(result->values[row][column], expected.values[row][column]))
            {
                return false;
            }
        }
    }
    return true;
}

bool TestTiltedBodyStillSuppliesOnlyHorizontalFacing() noexcept
{
    Matrix4 body = Yaw(0.72F);
    body.values[2][1] = 0.35F;
    const auto result = bfvr::stereo::MakeD3D8InfantryComfortCamera(
        Identity(),
        body);
    return result.has_value() &&
        Near(result->values[0][1], 0.0F) &&
        Near(result->values[1][0], 0.0F) &&
        Near(result->values[1][1], 1.0F) &&
        Near(result->values[1][2], 0.0F) &&
        Near(result->values[2][1], 0.0F) &&
        Near(std::hypot(
            result->values[2][0],
            result->values[2][2]), 1.0F);
}

bool TestInvalidInputsFailClosed() noexcept
{
    Matrix4 invalidSource = Identity();
    invalidSource.values[0][0] = std::numeric_limits<float>::quiet_NaN();
    Matrix4 verticalBody = Identity();
    verticalBody.values[2][0] = 0.0F;
    verticalBody.values[2][1] = 1.0F;
    verticalBody.values[2][2] = 0.0F;
    return !bfvr::stereo::MakeD3D8InfantryComfortCamera(
                invalidSource,
                Identity()).has_value() &&
        !bfvr::stereo::MakeD3D8InfantryComfortCamera(
                 Identity(),
                 verticalBody).has_value();
}

float ForwardYaw(const Matrix4& matrix) noexcept
{
    return std::atan2(matrix.values[2][0], matrix.values[2][2]);
}

bool TestSuppressedYawIsConsumedWithoutDelayedSnap() noexcept
{
    bfvr::stereo::InfantryCameraHeadingState state = {};
    auto camera = bfvr::stereo::MakeD3D8FilteredInfantryComfortCamera(
        Identity(),
        Yaw(0.20F),
        false,
        state);
    if (!camera.has_value() || !Near(ForwardYaw(*camera), 0.20F))
    {
        return false;
    }

    camera = bfvr::stereo::MakeD3D8FilteredInfantryComfortCamera(
        Identity(),
        Yaw(0.31F),
        true,
        state);
    if (!camera.has_value() || !Near(ForwardYaw(*camera), 0.20F))
    {
        return false;
    }

    // Ending suppression at the same observed body yaw must not replay the
    // ignored correction. A later ordinary turn still advances relatively.
    camera = bfvr::stereo::MakeD3D8FilteredInfantryComfortCamera(
        Identity(),
        Yaw(0.31F),
        false,
        state);
    if (!camera.has_value() || !Near(ForwardYaw(*camera), 0.20F))
    {
        return false;
    }
    camera = bfvr::stereo::MakeD3D8FilteredInfantryComfortCamera(
        Identity(),
        Yaw(0.46F),
        false,
        state);
    return camera.has_value() && Near(ForwardYaw(*camera), 0.35F);
}

bool TestUnsuppressedIntentionalTurnPassesThrough() noexcept
{
    bfvr::stereo::InfantryCameraHeadingState state = {};
    const auto first = bfvr::stereo::MakeD3D8FilteredInfantryComfortCamera(
        Identity(),
        Yaw(-0.30F),
        false,
        state);
    const auto turned = bfvr::stereo::MakeD3D8FilteredInfantryComfortCamera(
        Identity(),
        Yaw(0.50F),
        false,
        state);
    return first.has_value() && turned.has_value() &&
        Near(ForwardYaw(*turned), 0.50F);
}

} // namespace

int main()
{
    if (!TestSourceRecoilIsReplacedByBodyFacing() ||
        !TestTiltedBodyStillSuppliesOnlyHorizontalFacing() ||
        !TestInvalidInputsFailClosed() ||
        !TestSuppressedYawIsConsumedWithoutDelayedSnap() ||
        !TestUnsuppressedIntentionalTurnPassesThrough())
    {
        std::fprintf(stderr, "Infantry-camera math tests failed.\n");
        return 1;
    }
    std::printf("Infantry-camera math tests passed.\n");
    return 0;
}
