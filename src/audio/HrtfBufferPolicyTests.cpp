#include "audio/HrtfBufferPolicy.h"

#include <cstdio>

namespace
{
bool Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "HRTF buffer policy test failed: %s\n", message);
    }
    return condition;
}
} // namespace

int main()
{
    using bfvr::audio::HrtfBufferPolicyInput;
    using bfvr::audio::ShouldCenterHrtfBuffer;

    bool passed = true;
    const HrtfBufferPolicyInput leftMono = {
        false,
        false,
        true,
        1,
        -10000};
    passed &= Check(
        ShouldCenterHrtfBuffer(leftMono),
        "hard-left mono interface buffer was not centred");

    HrtfBufferPolicyInput rightMono = leftMono;
    rightMono.pan = 10000;
    passed &= Check(
        ShouldCenterHrtfBuffer(rightMono),
        "hard-right mono interface buffer was not centred");

    HrtfBufferPolicyInput centredMono = leftMono;
    centredMono.pan = 0;
    passed &= Check(
        !ShouldCenterHrtfBuffer(centredMono),
        "already-centred mono buffer was changed");

    HrtfBufferPolicyInput stereo = leftMono;
    stereo.channels = 2;
    passed &= Check(
        !ShouldCenterHrtfBuffer(stereo),
        "stereo buffer was changed");

    HrtfBufferPolicyInput positional = leftMono;
    positional.threeDimensional = true;
    passed &= Check(
        !ShouldCenterHrtfBuffer(positional),
        "3D buffer was changed");

    HrtfBufferPolicyInput primary = leftMono;
    primary.primary = true;
    passed &= Check(
        !ShouldCenterHrtfBuffer(primary),
        "primary buffer was changed");

    HrtfBufferPolicyInput noPanControl = leftMono;
    noPanControl.panControl = false;
    passed &= Check(
        !ShouldCenterHrtfBuffer(noPanControl),
        "buffer without DirectSound pan control was changed");

    HrtfBufferPolicyInput unknownFormat = leftMono;
    unknownFormat.channels = 0;
    passed &= Check(
        !ShouldCenterHrtfBuffer(unknownFormat),
        "buffer with unknown format was changed");

    return passed ? 0 : 1;
}
