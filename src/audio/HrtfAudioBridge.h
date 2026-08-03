#pragma once

#include "stereo/StereoMath.h"

#include <windows.h>

namespace bfvr::audio
{
using HrtfAudioLogCallback = void (*)(const wchar_t* message);

[[nodiscard]] bool IsHrtfAudioRequested() noexcept;

// Loads and probes BFVR's private audio runtime, then reroutes only the main
// executable's DirectSoundCreate8 import. HRTF is enabled by default for the
// normal VR request; BFVR_HRTF=0 or failed initialization leaves BF1942's
// original DirectSound path untouched.
[[nodiscard]] bool InitializeHrtfAudio(
    HMODULE bfvrClient,
    HrtfAudioLogCallback logCallback) noexcept;

// Publishes an OpenXR LOCAL centre-head pose for the DirectSound listener.
// Stale or untracked poses fail closed to the game's native listener values.
void PublishHrtfHeadPose(
    const stereo::Pose& headPose,
    bool tracked) noexcept;
} // namespace bfvr::audio
