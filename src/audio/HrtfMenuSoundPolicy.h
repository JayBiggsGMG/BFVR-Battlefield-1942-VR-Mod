#pragma once

#include <cstddef>
#include <cstdint>

namespace bfvr::audio
{
enum class HrtfMenuSoundKind
{
    none,
    menuContext,
    dedicated,
    sharedWeaponLayer,
};

struct HrtfPcmSignature
{
    std::uint64_t fnv1a = 0;
    std::uint32_t bytes = 0;
    std::uint32_t samplesPerSecond = 0;
    std::uint16_t channels = 0;
    std::uint16_t bitsPerSample = 0;
};

constexpr std::uint64_t kHrtfFnv1aOffset = 14695981039346656037ULL;

[[nodiscard]] std::uint64_t ContinueHrtfPcmHash(
    std::uint64_t hash,
    const void* bytes,
    std::size_t byteCount) noexcept;

// Identifies only PCM payloads named by the installed stock MenuSound.ssc.
// Dedicated menu samples are safe to de-spatialize on every play. The four
// firearm-manipulation layers are shared with weapon patches and require a
// preceding dedicated menu trigger at runtime.
[[nodiscard]] HrtfMenuSoundKind ClassifyHrtfMenuSound(
    const HrtfPcmSignature& signature) noexcept;

[[nodiscard]] bool IsHrtfMenuLayerWindowActive(
    std::uint64_t nowMilliseconds,
    std::uint64_t menuTriggerMilliseconds,
    std::uint64_t windowMilliseconds) noexcept;

[[nodiscard]] bool ShouldDisableHrtfMenuSpatialization(
    HrtfMenuSoundKind kind,
    bool visibleMenu,
    std::uint64_t nowMilliseconds,
    std::uint64_t menuTriggerMilliseconds,
    std::uint64_t windowMilliseconds) noexcept;
} // namespace bfvr::audio
