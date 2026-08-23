#pragma once

#include "stereo/StereoMath.h"

#include <array>
#include <cstdint>
#include <optional>

namespace bfvr
{

// Retains a controller-local anatomical-wrist lever arm independently of the
// BF1942 item animation. Its first pose is a zero-displacement reference, so
// binding, respawn, and item switches cannot move the accepted neutral hand.
class BFSoldierWristPositionBinding final
{
public:
    [[nodiscard]] std::optional<std::array<float, 3>> Update(
        const void* soldier,
        const void* skeleton,
        std::int32_t handBone,
        const stereo::Quaternion& gripOrientation) noexcept;
    void Reset() noexcept;

private:
    const void* soldier_ = nullptr;
    const void* skeleton_ = nullptr;
    std::int32_t handBone_ = -1;
    stereo::Quaternion referenceGripOrientation_ = {};
    bool valid_ = false;
};

} // namespace bfvr
