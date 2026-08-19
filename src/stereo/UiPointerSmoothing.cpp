#include "stereo/UiPointerSmoothing.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr float kPointerDeadzone = 0.0015F;
constexpr float kMinimumCutoffHertz = 4.0F;
constexpr float kMaximumCutoffHertz = 30.0F;
constexpr float kSpeedCutoffScale = 18.0F;
constexpr float kMaximumContinuousSeconds = 0.100F;
constexpr float kTwoPi = 6.283185307F;

bool IsFinite(float value) noexcept
{
    return std::isfinite(value);
}
} // namespace

namespace bfvr::stereo
{

UiPointerPoint UiPointerSmoother::Update(
    float rawX,
    float rawY,
    std::int64_t timestampNanoseconds,
    bool enabled) noexcept
{
    if (!IsFinite(rawX) || !IsFinite(rawY))
    {
        Reset();
        return {};
    }
    const UiPointerPoint raw = {
        std::clamp(rawX, 0.0F, 1.0F),
        std::clamp(rawY, 0.0F, 1.0F)};
    if (!enabled)
    {
        output_ = raw;
        lastTimestampNanoseconds_ = timestampNanoseconds;
        initialized_ = true;
        return raw;
    }
    if (initialized_ && timestampNanoseconds == lastTimestampNanoseconds_)
    {
        return output_;
    }

    const float elapsedSeconds = initialized_ &&
            timestampNanoseconds > lastTimestampNanoseconds_
        ? static_cast<float>(
              timestampNanoseconds - lastTimestampNanoseconds_) *
              0.000000001F
        : 0.0F;
    if (!initialized_ || timestampNanoseconds <= 0 ||
        timestampNanoseconds < lastTimestampNanoseconds_ ||
        elapsedSeconds > kMaximumContinuousSeconds)
    {
        output_ = raw;
        lastTimestampNanoseconds_ = timestampNanoseconds;
        initialized_ = true;
        return output_;
    }

    const float deltaX = raw.x - output_.x;
    const float deltaY = raw.y - output_.y;
    const float distance = std::sqrt(deltaX * deltaX + deltaY * deltaY);
    if (distance > kPointerDeadzone)
    {
        const float targetScale = 1.0F - kPointerDeadzone / distance;
        const float speed = distance / elapsedSeconds;
        const float cutoff = std::clamp(
            kMinimumCutoffHertz + speed * kSpeedCutoffScale,
            kMinimumCutoffHertz,
            kMaximumCutoffHertz);
        const float follow = 1.0F - std::exp(
            -kTwoPi * cutoff * elapsedSeconds);
        output_.x += deltaX * targetScale * follow;
        output_.y += deltaY * targetScale * follow;
        output_.x = std::clamp(output_.x, 0.0F, 1.0F);
        output_.y = std::clamp(output_.y, 0.0F, 1.0F);
    }
    lastTimestampNanoseconds_ = timestampNanoseconds;
    return output_;
}

void UiPointerSmoother::Reset() noexcept
{
    output_ = {};
    lastTimestampNanoseconds_ = 0;
    initialized_ = false;
}

} // namespace bfvr::stereo
