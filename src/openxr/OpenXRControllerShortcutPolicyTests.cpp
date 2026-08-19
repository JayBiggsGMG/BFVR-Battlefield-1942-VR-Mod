#include "openxr/OpenXRControllerShortcutPolicy.h"

#include <cstdio>

namespace
{
bool Expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(
            stderr,
            "OpenXR controller shortcut policy test failed: %s\n",
            message);
    }
    return condition;
}
} // namespace

int main()
{
    using namespace bfvr;
    bool passed = true;
    OpenXRControllerShortcutState state = {};

    passed &= Expect(
        IsOpenXRMapActionPressed(true, false) &&
            IsOpenXRMapActionPressed(false, true) &&
            !IsOpenXRMapActionPressed(false, false),
        "Map must accept its OpenXR action from either hand");

    auto update = [&](std::int64_t time, bool focused, bool menu, bool b) {
        return UpdateOpenXRControllerShortcuts(
            state,
            {time, focused, menu, b});
    };

    passed &= Expect(
        !update(1, true, false, false).mapToggleRequested,
        "the first focused sample must only establish a baseline");
    passed &= Expect(
        update(2, true, true, false).mapToggleRequested,
        "the map action must toggle the map on its press edge");
    passed &= Expect(
        !update(3, true, true, false).mapToggleRequested,
        "holding the map action must not repeat the map toggle");
    passed &= Expect(
        !update(4, true, false, false).mapToggleRequested &&
            update(5, true, true, false).mapToggleRequested,
        "the map action must re-arm after release");

    constexpr std::int64_t start = 10'000'000'000LL;
    passed &= Expect(
        !update(start, true, false, true).recenterRequested,
        "the B press edge must preserve reload without immediate recenter");
    passed &= Expect(
        !update(
             start + kControllerRecenterHoldNanoseconds - 1,
             true,
             false,
             true).recenterRequested,
        "B must not recenter before 2 seconds");
    passed &= Expect(
        update(
            start + kControllerRecenterHoldNanoseconds,
            true,
            false,
            true).recenterRequested,
        "B must recenter at the 2-second threshold");
    passed &= Expect(
        !update(
             start + kControllerRecenterHoldNanoseconds + 1,
             true,
             false,
             true).recenterRequested,
        "one B hold must produce only one recenter");
    (void)update(start + 3'000'000'000LL, true, false, false);
    (void)update(start + 4'000'000'000LL, true, false, true);
    passed &= Expect(
        update(start + 6'500'000'000LL, true, false, true).
            recenterRequested,
        "B must re-arm after release");

    (void)update(start + 7'000'000'000LL, false, false, false);
    passed &= Expect(
        !update(start + 8'000'000'000LL, true, true, true).
             mapToggleRequested &&
            !update(start + 11'000'000'000LL, true, true, true).
             recenterRequested,
        "buttons held while focus is acquired must not fire shortcuts");
    (void)update(start + 12'000'000'000LL, true, false, false);
    passed &= Expect(
        update(start + 13'000'000'000LL, true, true, false).
            mapToggleRequested,
        "focus-baselined shortcuts must arm after release");

    if (!passed)
    {
        return 1;
    }
    std::puts("OpenXR controller shortcut policy tests passed.");
    return 0;
}
