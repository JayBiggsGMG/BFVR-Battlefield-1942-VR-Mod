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

bool TestSourceRecoilIsReplacedByPresentationYaw() noexcept
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

    const auto result = bfvr::stereo::MakeD3D8InfantryPresentationCamera(
        source,
        -0.41F);
    const Matrix4 expected = [&]() noexcept {
        Matrix4 value = Yaw(-0.41F);
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

bool TestPresentationYawIsExplicitAndHorizontal() noexcept
{
    const auto result = bfvr::stereo::MakeD3D8InfantryPresentationCamera(
        Identity(),
        0.72F);
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
    return !bfvr::stereo::MakeD3D8InfantryPresentationCamera(
                invalidSource,
                0.0F).has_value() &&
        !bfvr::stereo::MakeD3D8InfantryPresentationCamera(
                 Identity(),
                 std::numeric_limits<float>::quiet_NaN()).has_value();
}

float ForwardYaw(const Matrix4& matrix) noexcept
{
    return std::atan2(matrix.values[2][0], matrix.values[2][2]);
}

bool TestAuthoritativeBodyChangesCannotEnterPresentation() noexcept
{
    const auto initial = bfvr::stereo::MakeD3D8InfantryPresentationCamera(
        Identity(),
        0.20F);
    // Arbitrary authoritative soldier yaw is intentionally not an input.
    // Reusing the same presentation yaw must remain bit-for-bit invariant.
    const auto afterNativeAim =
        bfvr::stereo::MakeD3D8InfantryPresentationCamera(
            Yaw(-1.35F),
            0.20F);
    const auto explicitTurn =
        bfvr::stereo::MakeD3D8InfantryPresentationCamera(
        Identity(),
        0.31F);
    const auto afterContextChange =
        bfvr::stereo::MakeD3D8InfantryPresentationCamera(
            Identity(),
            -1.20F);
    return initial.has_value() && afterNativeAim.has_value() &&
        explicitTurn.has_value() &&
        afterContextChange.has_value() &&
        Near(ForwardYaw(*initial), 0.20F) &&
        Near(ForwardYaw(*afterNativeAim), 0.20F) &&
        Near(ForwardYaw(*explicitTurn), 0.31F) &&
        Near(ForwardYaw(*afterContextChange), -1.20F);
}

} // namespace

int main()
{
    if (!TestSourceRecoilIsReplacedByPresentationYaw() ||
        !TestPresentationYawIsExplicitAndHorizontal() ||
        !TestInvalidInputsFailClosed() ||
        !TestAuthoritativeBodyChangesCannotEnterPresentation())
    {
        std::fprintf(stderr, "Infantry-camera math tests failed.\n");
        return 1;
    }
    std::printf("Infantry-camera math tests passed.\n");
    return 0;
}
