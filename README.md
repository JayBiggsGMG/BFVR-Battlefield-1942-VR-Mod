# BFVR distribution root

`BFVR` is the one and only parent folder required to distribute and remove the
Battlefield 1942 VR mod.

The eventual release ZIP will extract this directory beside `BF1942.exe`:

```text
Battlefield 1942\
  BF1942.exe
  BFVR\
    BFVRLauncher.exe
    BFVRClient.dll
    bfvr.toml
    runtime\
    profiles\
    logs\
    licenses\
    docs\
```

No release component may replace `BF1942.exe`, install a proxy DLL in the game
root, modify game data, or require files in multiple game folders.
`BFVRLauncher` will locate the game relative to this directory and load the
client module from within `BFVR`. The portable ZIP is the primary release
artifact; an installer, if added, will only create this same directory and
optional shortcuts.

## Current preflight

Before a launcher exists, the included validator can verify the checked-in
build profile without changing the game:

```powershell
.\BFVR\tools\Test-BFVRCompatibility.ps1
```

It verifies the executable and profiled root-module hashes, reports unprofiled
DLLs, and never launches, attaches to, patches, replaces, or moves game files.
See `docs\safety-and-compatibility.md` for the coexistence policy.

## Player-input boundary trace

`Trace-BFVR-PlayerInput.bat` in the game root starts a separate local/offline
two-minute diagnostic. It does not create an OpenXR session or use the graphics
path. It prefix-verifies the profiled 55-slot `PlayerInputMap` setter and
normalizer, forwards both original calls unchanged, and writes logical input
transitions, input-thread button-mask edges, and normalized-frame snapshots to
`build\bfvr-cmake-validate\logs\observer.log`. It aggregates active logical
slots from the normalizer's frames, retains press/release edges for the
non-axis slots without half-second sampling loss, and logs only setter calls made outside that
normalizer, rather than flooding the log with its high-rate internal traffic.
It never synthesizes input or writes BF1942 state, and the loader rejects any
combination with graphics or OpenXR probes. This is evidence collection only;
it is not a controller-input implementation and must not be run on public
servers.

## Initial VR controller slice

`Launch-BFVR-VR.bat` now includes a first local-player controller path; do
not run `Trace-BFVR-PlayerInput.bat` for this test. It activates only when the
OpenXR session is focused, the x86 client has received a fresh matched
controller sample, and the current input frame belongs to the alive local
player. The cache expires quickly; death/spawn states do not receive controller
input. Keyboard and mouse remain live throughout. Do not test vehicle behavior
yet: BFVR has not recovered a safe WinPC on-foot/vehicle classifier.

The first live controller runs proved the OpenXR transport, local-frame gate,
right-trigger fire, and controller look route. The current build packages the
following one-pass infantry layout for a normal local/offline play test:

- Left stick: move (strafe / forward-back).
- Right stick: analogue smooth turn left-right; push up once to jump/action,
  down once to go prone.
- Right trigger / grip: fire / alt-fire.
- Right A / B: jump-action / reload.
- Left X / Y / grip: use (hold) / crouch (hold) / prone (press once).
- Both sticks: a dead-zone-remapped analogue response curve; small deflections
  move/turn gently and full deflection reaches full native axis input.

Weapon cycling, map/menu, and the requested left-stick-click spawn-menu action
are deliberately not claimed in this build. The current local player-frame
route cannot dispatch those high/global actions; BFVR is recovering their
separate native GameInput boundary rather than faking them with Windows input.

The right controller's aim orientation remains ordinary game mouse-look; it
does not use controller or headset translation to move the player. Keyboard
and mouse remain live. Use a local/offline infantry map and simply play for a
few minutes. Report any wrong direction, missing action, stuck input, menu or
focus-loss issue, or discomfort. Do not test vehicle behavior yet: BFVR has
not recovered a safe WinPC on-foot/vehicle classifier.

## OpenXR presentation smoke test

The original standalone probe is x86 because it shares the client build. The
currently installed Oculus x86 runtime faults inside `xrCreateSession`, while
the corresponding x64 runtime has been independently proven healthy. BFVR
therefore keeps the game client x86 and builds `BFVRPresenter` as an x64-only
companion around the same `OpenXRPresentation` module.

Configure the companion build separately:

```powershell
cmake -S .\BFVR\src -B .\build\bfvr-presenter-x64 `
  -G "Visual Studio 17 2022" -A x64 -DBFVR_PRESENTER_ONLY=ON
cmake --build .\build\bfvr-presenter-x64 --config RelWithDebInfo
```

The x86 producer and x64 consumer probes first validate transport without a
headset or OpenXR. They exchange three named keyed-mutex D3D11 textures on the
presenter-selected adapter and verify exact copied pixels:

```powershell
.\build\bfvr-cmake-validate\BFVRSharedTextureProducerProbe.exe `
  --presenter .\build\bfvr-presenter-x64\RelWithDebInfo\BFVRSharedTextureConsumerProbe.exe `
  --presenter-log .\build\diagnostics\bfvr-x64-transport.log `
  --duration-ms 5000 --transport-only
```

For the headset smoke test, substitute `BFVRPresenter.exe` and omit
`--transport-only`. `--ui-cylinder` selects the cylinder layer; the default is
the quad layer.

The translated D3D8 path has a second transport control for D3D9Ex legacy
shared handles. Build both architectures, place the x64 consumer/presenter
beside the x86 client, and run:

```powershell
.\build\bfvr-cmake-validate\BFVRD3D8To9SharedSurfaceProbe.exe `
  --consumer .\build\bfvr-cmake-validate\BFVRSharedTextureConsumerProbe.exe `
  --consumer-log .\build\diagnostics\bfvr-d3d9ex-x64.log
```

This creates two R10 world targets and one R16-float UI target through
translated D3D8/D3D9Ex, opens them in x64 D3D11, converts them to BGRA, waits
for both APIs' GPU work before reuse, and verifies exact pixels without CPU
pixel transport. The standalone helper-allocation regression can be selected
for one process with `BFVR_D3D8TO9_FORCE_SHARED_HELPER=1`.

The live no-headset control combines the normal observer with the shared
transport request:

```powershell
.\build\bfvr-cmake-validate\BFVRLoader.exe `
  --game-root (Resolve-Path .) `
  --client (Resolve-Path .\build\bfvr-cmake-validate\BFVRClient.dll) `
  --d3d8to9-observer-probe --d3d8-stereo-frame-probe `
  --diagnostic-timeout-ms 120000
```

After launch, manually select and load an offline map before judging a run.
In this installation, command-line map arguments can start BF1942 but have
never reliably loaded a map; automated map loading is not test evidence. The
capture runs for 60 seconds and reports direct or helper target creation,
frame/draw/restoration counts, and per-stage timing.
The corresponding headset request replaces `--d3d8-stereo-frame-probe` with
`--d3d8-openxr-presentation-probe` and launches the x64 `BFVRPresenter.exe`.
These are opt-in diagnostics; ordinary BF1942 launches remain unchanged.
GPU-resident requests default world eyes to 100% of the runtime-recommended
dimensions; set `BFVR_OPENXR_WORLD_RENDER_SCALE=0.50..1.25` before launch for
an explicit diagnostic override.

For an owner-observed session without a fixed duration, omit
`--diagnostic-timeout-ms` and add `--run-until-stopped` to the combined
`--d3d8to9-observer-probe --d3d8-openxr-presentation-probe` command. It runs
until BF1942 exits or `Local\BFVRPresentationStop-<game-pid>` is signaled,
then stops at a frame boundary and shuts down the x64 presenter. Bounded mode
remains the default. A renderer/probe completion is retained as diagnostic
information only in this mode; it never closes the directly launched BF1942
process. If OpenXR temporarily has no renderable frame, BFVR retains one
pending request and keeps the game/presenter alive until the runtime resumes
or the owner exits, rather than converting the short bounded diagnostic wait
into a session-ending failure.

The 2026-07-25 GPU-resident headset control completed 2,999/2,999
request/render/consume/present cycles with zero failed frames and zero D3D8
readback. The x64 Oculus presenter opened R10/R10/R16 legacy allocations,
reached FOCUSED, sampled distinct live eye pixels, and shut down cleanly
through STOPPING with `healthy=1`. User-visible quality is tracked separately
from this mechanical transport/lifecycle result.

The translator bridge also preserves the original D3D8 programmable-shader
identity instead of relying on its pointer-derived public handle. BFVR records
the original bytecode's FNV-1a hash, size, and creation ordinal and uses the
exact observed SkinningShader2Bones identity to retain the native
soldier/first-person-arm correction through d3d8to9. A native-resolution flat
verification classified both observed animated-mesh topologies, reported zero
shader-transform/restoration failures, and transported 1,288/1,288 frames.

The same fail-closed mechanism now covers the exact Refractor 2 translucent
sprite draw site used by smoke, impacts, and explosion effects. BFVR validates
the engine's sprite c0-c7/c9 View, Projection, and camera contract before
installing per-eye constants, then restores c0-c9 exactly. A no-headset
Aberdeen run classified 32 such draws with no source mismatch, apply failure,
frame failure, or restoration failure. Visual confirmation remains pending.

The programmable TreeMesh sprite branch covers close foliage shared by trees
and shrubs. BFVR validates and supplies only the per-eye c0-c7
World*View/Projection constants while preserving c8-c15 tree-light/color/fade
/fog values. The classifier identifies an exact shader/call-site/state
contract and deliberately treats creation ordinal as telemetry because resource
load order changes it. A fresh no-HMD Aberdeen run classified 776 of these
draws with no tree-sprite preparation, source-match, or application failure
and 78,686/78,686 exact restorations. The owner has since confirmed that this
corrected close foliage renders properly in-headset.

The x64 conversion stage follows OpenXR's linear-composition contract. It
prefers a BGRA8 sRGB swapchain and converts BF1942's legacy encoded R10/R16 RGB
to linear before output; BGRA8 UNORM remains a linear-data fallback. Its
world-eye path applies FXAA while the Ref2 UI remains unfiltered. Bloom is
fully disabled: BFVR does not compile bloom shaders, allocate bloom targets,
issue bloom passes, or bind a bloom texture, and environment variables cannot
re-enable it.

The 2026-07-24 Oculus Link validation passed both synthetic modes on the exact
runtime-selected adapter. The user saw the deliberately different per-eye
world colors and the blue UI panel, and confirmed that the cylinder version
looked curved. A final quad rerun also followed the requested-exit lifecycle
through STOPPING with clean zero exits and complete resource release.

The first real BF1942 handoff then passed through the opt-in
`--d3d8-openxr-presentation-probe`: the user saw recognizable game geometry
in stereo and the extracted Ref2 HUD on its separate layer. That bounded run
held one submitted frame, so head motion exposed its edges and game input did
not update the visible image; first-person hands and weapon geometry also
need a separate policy. The current probe advances this to a 60-second
runtime-paced sequence. It composes centre-head motion at the renderer-camera
boundary and leaves only residual eye poses/asymmetric FOV for D3D8 replay,
retains the owned eye/UI targets across frames with Reset-safe cleanup, and
reports per-stage timing. It remains diagnostic, not a release runtime.
