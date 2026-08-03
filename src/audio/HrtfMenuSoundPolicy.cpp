#include "audio/HrtfMenuSoundPolicy.h"

#include <array>

namespace bfvr::audio
{
namespace
{
struct KnownSignature
{
    std::uint64_t fnv1a;
    std::uint32_t bytes;
    std::uint32_t samplesPerSecond;
};

constexpr std::array<KnownSignature, 27> kDedicatedMenuSounds = {{
    {0x45598FE27133E454ULL, 41088, 44100},
    {0x719BF0C431E27A58ULL, 38848, 44100},
    {0xE9704D6CDAD631A8ULL, 13952, 44100},
    {0x0CB704B72940DE18ULL, 10128, 44100},
    {0xE37E2DD534A3FD2BULL, 27136, 44100},
    {0x8783D861D3A84253ULL, 14400, 44100},
    {0xDBF83E7737FE1D40ULL, 9536, 44100},
    {0xCF558EECC3B37A33ULL, 62848, 44100},
    {0xA1F74CEB3D276215ULL, 19328, 44100},
    {0x715F8F4E54285F66ULL, 20540, 22050},
    {0x2466CF974D714D99ULL, 19424, 22050},
    {0xA8151FD71AAEC63BULL, 6972, 22050},
    {0x2806324821E81C2EULL, 5060, 22050},
    {0x7BF1F774B86491DCULL, 13564, 22050},
    {0xC133E6FB5D46D2E3ULL, 7196, 22050},
    {0x784F23B7820D11BFULL, 4764, 22050},
    {0x63BCBB1A517A4BC2ULL, 31420, 22050},
    {0xFE81BA29C63F051EULL, 9660, 22050},
    {0xA73F029636F6CB22ULL, 10270, 11025},
    {0x5FD63CECA2B10EE7ULL, 9712, 11025},
    {0xA75DA7C9E356DB59ULL, 3486, 11025},
    {0xBB4426455273C22CULL, 2530, 11025},
    {0xCE474158B04D6030ULL, 6782, 11025},
    {0x825C7A5310991624ULL, 3598, 11025},
    {0x38E3FADAA14D5F39ULL, 2382, 11025},
    {0x3A347FE584DE920CULL, 15710, 11025},
    {0xB25E07CE90CCE342ULL, 4830, 11025},
}};

constexpr std::array<KnownSignature, 12> kSharedWeaponLayers = {{
    {0xA8FD677BF3797A75ULL, 20008, 44100},
    {0x96B2912628FA991BULL, 26180, 44100},
    {0x8D6C21C24BF10B6EULL, 19936, 44100},
    {0xA34A1FF5C63C5643ULL, 22592, 44100},
    {0xCC25E4E3A4E47A22ULL, 10000, 22050},
    {0xFCC65966310AF732ULL, 13086, 22050},
    {0xA3A476DD56963080ULL, 9964, 22050},
    {0xE24A2842E75B3E2CULL, 11292, 22050},
    {0xF3CE0859089FD0A4ULL, 5000, 11025},
    {0x56DDC144FADB644CULL, 6544, 11025},
    {0xF37305A43BD1BE01ULL, 4982, 11025},
    {0x782E56B6B9722957ULL, 5646, 11025},
}};

bool Matches(
    const HrtfPcmSignature& signature,
    const KnownSignature& known) noexcept
{
    return signature.fnv1a == known.fnv1a &&
        signature.bytes == known.bytes &&
        signature.samplesPerSecond == known.samplesPerSecond &&
        signature.channels == 1 &&
        signature.bitsPerSample == 16;
}
} // namespace

std::uint64_t ContinueHrtfPcmHash(
    std::uint64_t hash,
    const void* bytes,
    std::size_t byteCount) noexcept
{
    if (bytes == nullptr && byteCount != 0)
    {
        return 0;
    }
    const auto* values = static_cast<const std::uint8_t*>(bytes);
    for (std::size_t index = 0; index < byteCount; ++index)
    {
        hash ^= values[index];
        hash *= 1099511628211ULL;
    }
    return hash;
}

HrtfMenuSoundKind ClassifyHrtfMenuSound(
    const HrtfPcmSignature& signature) noexcept
{
    for (const KnownSignature& known : kDedicatedMenuSounds)
    {
        if (Matches(signature, known))
        {
            return HrtfMenuSoundKind::dedicated;
        }
    }
    for (const KnownSignature& known : kSharedWeaponLayers)
    {
        if (Matches(signature, known))
        {
            return HrtfMenuSoundKind::sharedWeaponLayer;
        }
    }
    return HrtfMenuSoundKind::none;
}

bool IsHrtfMenuLayerWindowActive(
    std::uint64_t nowMilliseconds,
    std::uint64_t menuTriggerMilliseconds,
    std::uint64_t windowMilliseconds) noexcept
{
    return menuTriggerMilliseconds != 0 &&
        nowMilliseconds >= menuTriggerMilliseconds &&
        nowMilliseconds - menuTriggerMilliseconds <= windowMilliseconds;
}

bool ShouldDisableHrtfMenuSpatialization(
    HrtfMenuSoundKind kind,
    bool visibleMenu,
    std::uint64_t nowMilliseconds,
    std::uint64_t menuTriggerMilliseconds,
    std::uint64_t windowMilliseconds) noexcept
{
    switch (kind)
    {
    case HrtfMenuSoundKind::menuContext:
        return visibleMenu;
    case HrtfMenuSoundKind::dedicated:
        return true;
    case HrtfMenuSoundKind::sharedWeaponLayer:
        return visibleMenu || IsHrtfMenuLayerWindowActive(
            nowMilliseconds,
            menuTriggerMilliseconds,
            windowMilliseconds);
    default:
        return false;
    }
}
} // namespace bfvr::audio
