# Experimental HRTF audio

BFVR contains a folder-local DirectSound-to-OpenAL Soft path for ordinary stereo
headphones. It is enabled by default for the continuous translated OpenXR
request. Set `BFVR_HRTF=0` before launch to keep BF1942's original audio path.

The implementation does not replace the existing `dsound.dll` beside
`BF1942.exe`. That installation DLL performs a separate process-attach
compatibility patch before BFVR is injected, so BFVR leaves it loaded and
untouched. After injection, BFVR changes only BF1942.exe's DSOUND ordinal-11
(`DirectSoundCreate8`) IAT entry. Any initialization or backend-probe failure
leaves that entry on its original route. A later DSOAL creation failure calls
the saved original route.

## Default and opt-out controls

The normal `Launch-BFVR-VR.bat` path needs no audio environment variable. To
disable HRTF for one launch, use the same command prompt as the launcher:

```bat
set "BFVR_HRTF=0"
Launch-BFVR-VR.bat
```

Close that command prompt or run `set "BFVR_HRTF="` to restore the default. A
separate compatibility switch, `BFVR_HRTF_CENTER_MONO=0`, disables only BFVR's
mono non-3D pan correction while retaining HRTF.
`BFVR_HRTF_CENTER_MENU=0` disables only the visible-menu correction described
below. BFVR does not change
`Sound.con`, the Windows audio configuration, the game-root DLL, or an OpenAL
configuration under AppData.

Use ordinary two-channel headphones and disable Windows Sonic, Dolby Atmos,
headset virtual surround, or another binaural stage for this test. Applying
two spatializers usually weakens direction cues and colours the sound.

## Private runtime

The release layout remains under `BFVR`:

```text
BFVR\
  BFVRClient.dll
  runtime\audio\win32\
    BFVRDSoal.dll
    BFVROpenALSoft.dll
    dsoal-aldrv.dll
```

- `BFVRDSoal.dll` is the official x86 DSOAL r694 binary from commit
  `5c65e5fea13474a8bf346627e2944d28fe0c9cb5`, renamed to avoid the occupied
  game-root filename. Its SHA-256 is
  `3E49A9AB1B4454BED65A2ED0D9391CDEA6611FBDAB4CFE466E313BCE9EAC3303`.
- `BFVROpenALSoft.dll` is the official x86 OpenAL Soft r10594 binary from
  commit `9531d76f0617cf84609c5d9c17c6d134f92e5cc3`, renamed for private routing.
  Its SHA-256 is
  `A064DF5DC7653510988773F0E2AC512F5A5E6976F97696D2C5F0E4371F0010AF`.
- BFVR builds `dsoal-aldrv.dll` as a thin OpenAL export router. It preserves
  all 176 OpenAL Soft exports and intercepts only `alcCreateContext`, adding
  `ALC_HRTF_SOFT=true`. This makes HRTF explicit without putting `alsoft.ini`
  beside the game or in a user profile.
- The corresponding DSOAL and OpenAL Soft licenses are in `BFVR\licenses`.

At initialization BFVR creates and releases a private DirectSound8 primary 3D
listener, then reads the router's versioned diagnostics. Routing is accepted
only when a context was created with the forced attribute and OpenAL Soft
reports `ALC_HRTF_STATUS_SOFT == ALC_HRTF_ENABLED_SOFT`.

## HMD listener bridge

DSOAL continues to own the game-created DirectSound objects and BF1942
continues to own every native source and listener base transform. BFVR observes
DSOAL primary-buffer listener acquisition and wraps these listener methods:

- `GetAllParameters`, `GetOrientation`, and `GetPosition`
- `SetAllParameters`, `SetOrientation`, and `SetPosition`

The getters return BF1942's native values. The setters preserve the native
listener position, front/top orientation, velocity, distance factor, rolloff,
Doppler, and immediate/deferred flag, then compose only the current OpenXR
`LOCAL` centre-head transform before forwarding to DSOAL. The composition uses
the same row-vector OpenXR-to-D3D8 basis as the renderer-camera path. A missing,
invalid, or older-than-250-ms tracked pose forwards the native listener exactly.

This prototype does not synthesize source positions, occlusion, reverberation,
or elevation data. Stereo music, radio, UI, and other non-3D game buffers remain
non-positional by design. After the first headset run exposed hard-left menu
clicks and weapon-switch feedback, BFVR added a bounded compatibility rule:
only a buffer that is non-primary, lacks `DSBCAPS_CTRL3D`, has
`DSBCAPS_CTRLPAN`, is mono, and has a non-zero pan is centred at `SetPan` or
`Play`. Authored stereo and all 3D buffers retain their original behavior.

## Native-menu sound correction

The first follow-up established that the affected sounds change sides as the
head turns, so they are entering a spatial path rather than carrying a fixed
left pan. BFVR now uses its existing native BfMenu state observation as the
primary ownership gate: while the stock or modded frontend menu is visibly
active, any mono `DSBCAPS_CTRL3D` buffer played there is temporarily changed to
`DS3DMODE_DISABLE`. DirectSound defines that mode as non-spatial and centred in
the listener's head. If a mod reuses the same buffer later outside the menu,
BFVR restores its original 3D mode before playback.

For the short interval before menu visibility is first published, BFVR also
recognizes exact PCM payloads named by the installed stock `MenuSound.ssc` at
all three BF1942 quality rates. The stock select-item sound layers four shared
firearm-manipulation samples over `menuchange.wav`; those shared samples are
made non-spatial only while the menu is visible or for 750 ms after a dedicated
stock menu trigger. Their ordinary weapon use remains spatial. Unknown mod
sounds are covered by the menu-state gate rather than by stock filenames.

## Validation status

The deterministic x86 test verifies BF1942's ordinal-form IAT routing. The
listener-math suite covers identity, translation, yaw, non-finite input, and
degenerate axes. The buffer-policy suite proves that only panned mono non-3D
buffers are eligible for centring. The menu-sound suite additionally covers
stock fingerprints, the mod-compatible visibility gate, shared-layer timing,
and restoration outside the menu. All 15 registered suites pass. The
standalone backend probe has created a real primary DirectSound3D listener on
the development machine and reported:

```text
HRESULT=0x00000000 contexts=1 forced=1 successful=1 hrtfStatus=1 malformed=0 menuMode=2 menuModeHRESULT=0x00000000
```

The first owner headset run heard a subtle positional effect and its log proved
the enabled HRTF context, BF1942-to-DSOAL route, primary CTRL3D listener, and
tracked listener writes. It also revealed that BFVR's first observer supported
only one DSOAL buffer vtable and forwarded one malformed pre-tracking listener
position. The current build registers up to eight distinct DSOAL buffer
vtables, retains each class's original QueryInterface/Play/SetPan methods, and
reuses the last valid native listener transform when a setter supplies a NaN or
degenerate basis.

The native-menu correction and broader lifecycle matrix still need a BF1942
headset A/B. Confirm that menu clicks and weapon-switch-style layers remain
centred while turning the head, then
test a stable world sound while turning the HMD. Maps, respawn, vehicles, focus
loss, and device changes remain open regression gates, so HRTF is default-on
for this development build but remains experimental rather than release-ready.
