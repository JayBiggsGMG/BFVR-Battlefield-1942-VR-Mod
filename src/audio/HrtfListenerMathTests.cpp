#include "audio/HrtfListenerMath.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
bool Near(float actual, float expected, float tolerance = 1.0e-4F)
{
    return std::fabs(actual - expected) <= tolerance;
}

bool Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "HRTF listener math test failed: %s\n", message);
    }
    return condition;
}
} // namespace

int main()
{
    bool passed = true;
    const bfvr::audio::ListenerTransform native = {
        {10.0F, 20.0F, 30.0F},
        {0.0F, 0.0F, 1.0F},
        {0.0F, 1.0F, 0.0F}};

    const auto identity = bfvr::audio::ComposeHrtfListener(
        native,
        {},
        1.0F);
    passed &= Check(identity.has_value(), "identity pose was rejected");
    if (identity)
    {
        passed &= Check(
            Near(identity->position.x, 10.0F) &&
                Near(identity->position.y, 20.0F) &&
                Near(identity->position.z, 30.0F),
            "identity pose changed position");
        passed &= Check(
            Near(identity->front.z, 1.0F) &&
                Near(identity->top.y, 1.0F),
            "identity pose changed orientation");
    }

    bfvr::stereo::Pose translated = {};
    translated.position = {1.0F, 2.0F, -3.0F};
    const auto moved = bfvr::audio::ComposeHrtfListener(
        native,
        translated,
        2.0F);
    passed &= Check(moved.has_value(), "translated pose was rejected");
    if (moved)
    {
        passed &= Check(
            Near(moved->position.x, 12.0F) &&
                Near(moved->position.y, 24.0F) &&
                Near(moved->position.z, 36.0F),
            "OpenXR-to-D3D8 translation was not composed");
    }

    bfvr::stereo::Pose yaw = {};
    constexpr float kSqrtHalf = 0.707106781F;
    yaw.orientation = {0.0F, kSqrtHalf, 0.0F, kSqrtHalf};
    const auto rotated = bfvr::audio::ComposeHrtfListener(
        native,
        yaw,
        1.0F);
    passed &= Check(rotated.has_value(), "yaw pose was rejected");
    if (rotated)
    {
        passed &= Check(
            Near(std::fabs(rotated->front.x), 1.0F) &&
                Near(rotated->front.y, 0.0F) &&
                Near(rotated->front.z, 0.0F),
            "yaw did not rotate the listener front");
    }

    bfvr::stereo::Pose invalid = {};
    invalid.position.x = std::numeric_limits<float>::quiet_NaN();
    passed &= Check(
        !bfvr::audio::ComposeHrtfListener(native, invalid, 1.0F),
        "non-finite pose did not fail closed");

    const bfvr::audio::ListenerTransform degenerate = {
        {},
        {0.0F, 1.0F, 0.0F},
        {0.0F, 1.0F, 0.0F}};
    passed &= Check(
        !bfvr::audio::ComposeHrtfListener(degenerate, {}, 1.0F),
        "parallel listener axes did not fail closed");

    return passed ? 0 : 1;
}
