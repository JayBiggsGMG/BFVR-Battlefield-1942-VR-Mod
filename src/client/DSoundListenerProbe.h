#pragma once

namespace bfvr
{

// Makes BF1942's 3D audio follow VR head.
//
// Hooks whichever dsound.dll BF1942 loaded, reaching
// IID_IDirectSound3DListener through the 3D-capable primary buffer, and
// replaces the listener basis with the world-space camera the renderer just
// used. In menus, during loading, and on any non-VR path
// BF1942's own values pass through untouched.
//
// Controls, from UserConfig.txt, read once at startup:
//   head_tracked_audio_enabled           the correction itself (default true)
//   head_tracked_audio_position_enabled  also place the listener at the head
//                                        (default false; second-order effect)
//
// Diagnostics:
//   BFVR_DSOUND_LISTENER_PROBE=1         per-call and periodic detail
//
void StartDSoundListenerProbe(void (*appendLog)(const wchar_t* message));

} // namespace bfvr
