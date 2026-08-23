#pragma once

#include <cstdint>

namespace bfvr::stereo
{

struct UiPointerPoint
{
    float x = 0.0F;
    float y = 0.0F;
};

// Stateful low-pass filter for normalized controller-ray UI coordinates.
// A small output-space deadzone suppresses hand tremor while an adaptive
// cutoff lets deliberate pointer movement catch up quickly.
class UiPointerSmoother
{
public:
    [[nodiscard]] UiPointerPoint Update(
        float rawX,
        float rawY,
        std::int64_t timestampNanoseconds,
        bool enabled) noexcept;
    void Reset() noexcept;

private:
    UiPointerPoint output_ = {};
    std::int64_t lastTimestampNanoseconds_ = 0;
    bool initialized_ = false;
};

} // namespace bfvr::stereo
