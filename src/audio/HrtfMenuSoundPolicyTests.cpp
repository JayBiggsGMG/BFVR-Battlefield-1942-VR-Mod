#include "audio/HrtfMenuSoundPolicy.h"

#include <cstdio>
#include <cstring>

namespace
{
bool Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "HRTF menu-sound policy test failed: %s\n", message);
    }
    return condition;
}
} // namespace

int main()
{
    using namespace bfvr::audio;
    bool passed = true;

    constexpr char kHello[] = "hello";
    passed &= Check(
        ContinueHrtfPcmHash(
            kHrtfFnv1aOffset,
            kHello,
            std::strlen(kHello)) == 0xA430D84680AABD0BULL,
        "FNV-1a implementation changed");

    const HrtfPcmSignature menuChange = {
        0x0CB704B72940DE18ULL,
        10128,
        44100,
        1,
        16};
    passed &= Check(
        ClassifyHrtfMenuSound(menuChange) ==
            HrtfMenuSoundKind::dedicated,
        "stock 44-kHz menuchange was not recognized");

    const HrtfPcmSignature lowMenuBack = {
        0x5FD63CECA2B10EE7ULL,
        9712,
        11025,
        1,
        16};
    passed &= Check(
        ClassifyHrtfMenuSound(lowMenuBack) ==
            HrtfMenuSoundKind::dedicated,
        "stock 11-kHz menuback was not recognized");

    const HrtfPcmSignature sharedWeaponLayer = {
        0x8D6C21C24BF10B6EULL,
        19936,
        44100,
        1,
        16};
    passed &= Check(
        ClassifyHrtfMenuSound(sharedWeaponLayer) ==
            HrtfMenuSoundKind::sharedWeaponLayer,
        "stock shared weapon layer was not recognized");

    HrtfPcmSignature wrongFormat = menuChange;
    wrongFormat.channels = 2;
    passed &= Check(
        ClassifyHrtfMenuSound(wrongFormat) == HrtfMenuSoundKind::none,
        "format mismatch was accepted");

    HrtfPcmSignature wrongHash = menuChange;
    ++wrongHash.fnv1a;
    passed &= Check(
        ClassifyHrtfMenuSound(wrongHash) == HrtfMenuSoundKind::none,
        "hash mismatch was accepted");

    passed &= Check(
        IsHrtfMenuLayerWindowActive(1500, 1000, 750),
        "shared layer was rejected inside the menu window");
    passed &= Check(
        !IsHrtfMenuLayerWindowActive(1800, 1000, 750),
        "shared layer was accepted after the menu window");
    passed &= Check(
        !IsHrtfMenuLayerWindowActive(999, 1000, 750),
        "time before the trigger was accepted");
    passed &= Check(
        ShouldDisableHrtfMenuSpatialization(
            HrtfMenuSoundKind::menuContext,
            true,
            2000,
            0,
            750),
        "mod-compatible visible-menu buffer was not disabled");
    passed &= Check(
        !ShouldDisableHrtfMenuSpatialization(
            HrtfMenuSoundKind::menuContext,
            false,
            2000,
            0,
            750),
        "contextual menu buffer remained disabled outside the menu");
    passed &= Check(
        ShouldDisableHrtfMenuSpatialization(
            HrtfMenuSoundKind::dedicated,
            false,
            2000,
            0,
            750),
        "dedicated stock menu fallback was not disabled");
    passed &= Check(
        ShouldDisableHrtfMenuSpatialization(
            HrtfMenuSoundKind::sharedWeaponLayer,
            false,
            1500,
            1000,
            750),
        "shared stock layer was not disabled inside its trigger window");
    passed &= Check(
        !ShouldDisableHrtfMenuSpatialization(
            HrtfMenuSoundKind::sharedWeaponLayer,
            false,
            1800,
            1000,
            750),
        "shared stock layer remained disabled outside its trigger window");

    return passed ? 0 : 1;
}
