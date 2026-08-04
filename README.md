# BFVR distribution root

> **KNOWN-BEST FIREARM BASELINE:** The commit/tag
> `bfvr-known-best-firearm-alignment-2026-07-29` is the owner-validated best
> rifle/pistol hand, gun, aim, firing, respawn, crouch, and prone alignment
> system. Preserve it as the fallback for future IK/recoil work. See
> [KNOWN_BEST_FIREARM_ALIGNMENT.md](KNOWN_BEST_FIREARM_ALIGNMENT.md).

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

## Experimental HRTF audio

BFVR now enables its folder-local DSOAL/OpenAL Soft headphone-HRTF prototype by
default for the normal VR launcher. Set `BFVR_HRTF=0` before launch for an
immediate original-audio opt-out. It leaves the existing game-root
`dsound.dll`, `Sound.con`, and user OpenAL configuration untouched; startup
accepts the alternate path only after a real DirectSound3D listener reports
HRTF enabled, otherwise it preserves the original audio route. Tracked HMD
motion is composed with BF1942's native listener and expires back to the native
values after 250 ms without tracking.

This has passed build, listener-math, mono-buffer-policy, ordinal-IAT, and
standalone audio-backend tests. The first owner headset run confirmed a subtle
audible effect and exposed head-relative menu-click/weapon-switch-style
feedback. BFVR now centres mono 3D sounds only while its engine-observed native
frontend menu is visible, so replacement sounds in mods are covered without
flattening gameplay audio. Exact stock-sample fingerprints provide an
early-start fallback and a bounded window for the stock click's shared firearm
layers. That correction still needs a headset re-test. Use ordinary stereo
headphones with any other virtual-surround stage disabled. See
[docs/hrtf-audio.md](docs/hrtf-audio.md) for architecture, pinned runtime
versions, validation evidence, and opt-out controls.

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
through the old input trace launcher; the normal VR launcher now owns the
validated infantry/vehicle controller switch described below.

OpenXR now starts before BF1942 creates its delayed gameplay D3D8 device. A
temporary CPU startup bridge captures the native game window onto a
world-locked menu panel, then hands off to the GPU-resident stereo path when
CreateDevice/Present become available. Native interactive menus open from the
actual native-menu edge at a yaw-only OpenXR `LOCAL` pose in front of the user.
They remain world-stable while within 35 degrees of view, then gently yaw-follow
at up to 90 degrees per second when looked well away; the controller ray uses
the same anchor. Only the gameplay HUD follows the head in `VIEW`, at the
unchanged 1.5 m distance and size. In native mouse-enabled menus, point the
right controller at the panel and use right trigger as the normal BF1942
select/click action; mouse and keyboard remain available. This interaction is
currently quad-only.

The first live controller runs proved the OpenXR transport, local-frame gate,
right-trigger fire, and controller look route. The current build packages the
following one-pass infantry layout for a normal local/offline play test:

- Left stick: move (strafe / forward-back); click for the native contextual
  vehicle hatch/dive-down action.
- Right stick: analogue smooth turn left-right; click for the native contextual
  vehicle hatch/dive-up action; flick up once to submit jump and parachute
  together; flick down once to toggle crouch on or off.
- Right trigger: fire. Right grip supplies BF1942's native alt-fire/ADS action.
  BFVR adds no second latch; infantry, vehicle, and mounted semantics remain
  owned by the game and unchanged by the scope-view feature.
- Left trigger: use (hold).
- Right A: dedicated Quick Menu hold; it never submits jump/action. Hold A to
  show the stabilized 0.266 m right-hand panel, point with the right aim ray,
  and release A to choose Escape, Enter, 1-6, or F9-F12. Releasing off-panel
  or losing focus/tracking cancels. Its authored stack is background, selected
  hover, text, then the frame as the final/top layer.
- Right B: reload.
- Left X / Y / grip: prone / scoreboard (hold through BF1942's paired native
  player/global HUD semantics) / proximity-gated weapon support.
  Prone and jump clear a controller-toggled crouch.
- Both sticks: a dead-zone-remapped analogue response curve; small deflections
  move/turn gently and full deflection reaches full native axis input.

When the local player controls a non-default vehicle or mounted
`PlayerControlObject`, BFVR switches only the stick axes while retaining the
buttons above:

- Land, sea, AA, and mounted guns: left Y is proportional gas/throttle or
  reverse, left X is proportional steering, and the right stick continuously
  aims turret/station traverse and elevation. Tracked right-controller movement
  adds fine relative aim on the same axes: hand-left moves the barrel right and
  hand-down moves it up, as though the gun pivot were between the controller
  and barrel. Holding the hand still holds aim still; the stick remains the
  unrestricted control for continuous/360-degree traverse. These directions
  and the current 48.0-native-input-units-per-metre sensitivity are fixed; the
  latter halves physical travel relative to the initial headset build. A future
  in-game VR settings menu is intended to expose horizontal/vertical inversion
  and sensitivity.
- Aircraft: left Y is proportional throttle, left X is roll, right X is yaw,
  and right Y is pitch. Stick up uses BF1942's positive pitch/dive direction;
  stick down climbs.

The switch uses BF1942's current-versus-default control-object relation, not a
null pointer or configurable key binding. A soldier is the default
`PlayerControlObject`; vehicles and stationary weapons are non-default control
objects. The controlled template's native `VCAir` category selects the aircraft
axes, while land/sea and unreadable mod categories never acquire aircraft-only
roll/yaw mapping. Entering or leaving a vehicle clears crouch/directional stick
state and captures a fresh zero-input motion reference so neither infantry state
nor a stale hand position can leak across the transition.

While the Quick Menu is open, controller fire, alt-fire, infantry stick-up/down
actions, and face-button gameplay actions are suppressed; stick locomotion and
vehicle control remain live. Surface-weapon motion aim pauses and rebaselines on
release so pointing at the panel cannot move a turret. The x64 presenter emits
one scan-code down/up pair only on A release with a valid hover and only while
BF1942 is the foreground process.
The number keys retain BF1942's native context-sensitive weapon/vehicle-seat
behavior, while Enter opens deployment, F9-F12 select cameras, and Escape opens
the native main menu. Mouse and keyboard remain available.

Gameplay controller inputs are submitted as BF1942 logical actions, not as
keyboard keys. Scoreboard Y submits the exact player-side source-0 state used
by `HudManager::scoreBoard`, then overrides only logical action 35 at the native
global branch so BF1942 completes its ordinary paired dispatch. Changing
keyboard or mouse bindings in the native profile menu therefore does not change
or break this controller layout. The jump/parachute pair likewise bypasses the
profile editor's duplicate-binding restriction by submitting both native
actions in the same temporary input frame.

Controller pose does not write infantry look or aircraft flight axes; deliberate
right-stick turn and ordinary keyboard/mouse look remain live. Only surface,
sea, and mounted control objects translate newly timestamped right-grip position
deltas into bounded native turret/station `mouseLookX/Y` input. The opt-in weapon
path separately uses the tracked right grip for the classified first-person
viewmodel and feeds the same fresh grip-driven rendered orientation into ordinary
alive-local-infantry fire. This follows the virtual weapon geometry instead of
maintaining a parallel controller `aim` ray. The firing transform preserves BF1942's
per-weapon/per-barrel muzzle offsets, spread, cadence, projectile creation,
and networking. Native-arm mode may move the native origin with the solved
hand only when the fresh fire matrix belongs to the same soldier lifetime and
is within 1.25 m of that hand; solved-hand displacement must also remain within
1.5 m. A distant match-start/death/cinematic camera origin, unsupported
caller, owner, matrix, focus, or tracking state forwards the original shot
unchanged and records a bounded reason. The BAR and Colt have produced live
adjusted directions; a fixed-range guarded muzzle/impact measurement remains
pending.

For an exact local handweapon whose native template enables `useScope`,
BF1942's native zoom state keeps the HMD-positioned stereo view aimed along the
tracked gun basis, restores that weapon's configured relative FOV, and presents
the native scope raster as an eye-filling head-locked overlay. BFVR calibrates
the accepted right-controller aim against the exact native-arm gun basis at
entry, then continues reading that tracked aim while BF1942 hides its normal
first-person arm update during native scope view. A transient tracking miss
retains the last valid gun direction rather than dropping magnification or the
overlay. When primary two-hand support was already established at entry, scoped
camera direction also reuses that exact authored support relation and bounded
fixed-pivot steering with fresh left-grip motion. Releasing or losing the left
grip returns view direction to right aim; this does not create a second grip or
write native grip state. The exact alive-local weapon and soldier lifetime also
share this visible scoped gun basis with `WeaponFire_Core` whenever BF1942's
hidden native-arm publication is stale. That fallback replaces shot direction
but preserves BF1942's native projectile origin; native weapon/barrel offsets,
spread, cadence, projectile creation, and networking remain authoritative.
Zoom-only weapons without
`useScope` receive no BFVR camera, FOV, or overlay policy. Scope exit immediately
returns camera direction and firing to their normal behavior; grips, arms,
elbows, IK, and recoil are not filtered by this view.

The VR replay applies only a rigid controller attachment to the classified
weapon family: no viewmodel scale or perspective morph and no artificial
controller-reach clamp. The exact first-person animated-arm family is replayed
in VR globally across assets/factions, while remote soldiers retain their
corrected stereo draw and BF1942's original flat render remains untouched. It
replays only the exact game-selected skinned first-person arms through the
existing stereo shader path; no bespoke assets, controller IK, manual reloads,
or gameplay-state changes are involved. Set `BFVR_NATIVE_1P_ARMS=0` before
launch to opt out and restore the prior arm suppression.
The exact native CrossHair visibility setter is forced off without disabling
the rest of the HUD. BFVR replaces it with the supplied 64x64 world crosshair
only for the gadget slots: grenades/TNT (4),
mines/binoculars/medpack (5), and wrench (6). Knives, pistols, ordinary guns,
launchers, and the detonator remain excluded. Unknown modded items which reuse
slots 4/5/6 remain enabled by policy because those slots are conventionally
gadget families; all other infantry slots remain excluded. A non-default
vehicle or mounted control object additionally requires BF1942 itself to
request its crosshair and expose a readable native weapon, so unarmed or
unresolved stations fail closed. The gadget endpoint uses the exact direct
controller-gun basis shared with local fire. Mounted endpoints query the
current non-default control object's native PlayerControlObject weapon vector
and use weapon zero's `FireArms::getFireArmsTransformation`, including the
engine's special parent-firearm route. Both are independently projected into
the two world-eye targets at a finite default 50 m. Because no continuous native surface query is
yet proven, this is explicitly a no-hit range endpoint rather than a claimed
collision point. `BFVR_CROSSHAIR_MAX_DISTANCE_METRES` adjusts it from 2..500 m,
and `BFVR_CROSSHAIR_ANGULAR_DIAMETER_DEGREES` adjusts apparent size from
0.25..8 degrees (default 2).

The aligned `HitMarker.png` layer appears only while BF1942's own local network
hit-indication timer is positive. BFVR reads that timer without changing or
extending it; it does not infer hits from health, depth, or collision.

### Native arm ownership trace

Controller-driven native arms need one separate ownership trace; merely seeing
the restored arms is not that evidence. Double-click
[`Trace-BFVR-FirstPerson-Arms.bat`](../Trace-BFVR-FirstPerson-Arms.bat) in the
game folder. It records the game-selected local first-person soldier's
template, animated/collision Skeleton pointers, right-hand bone index, and
active-item index, plus native child-to-bone binding records, one final
right-hand matrix after the original native animation update returns, and its
existing in-update `AnimatedBundle` attachment-query candidates. It also reads
the game-owned current-item lookup object's vtable target and a short code
prefix so BFVR can identify the live dispatch without calling it. It makes no
game-method calls, game-data writes, or debug-register access. It waits up to
two minutes for a local first-person traversal, then records the exact
narrow-projection AnimatedMesh instances drawn during a brief full
post-capture window. Spawn or re-enter first person once; it then removes its
hooks, signals completion, and writes the result to
`build\bfvr-cmake-validate\logs\observer.log`. At timeout, the loader closes
only the BF1942 process it directly launched, so use an unsaved local/offline
session. It deliberately does not claim a direct arm-mesh owner; that requires
a separate renderer-correlated observation, which this revision performs.

This trace is for the next motion-control feasibility gate only. It neither
changes the currently enabled native-arm rendering nor implements IK.

The older classified-D3D8 weapon path still has a two-stage Left Touch Menu
development calibration, but native-arm mode does not rebase from that button
or from death/respawn. It captures the right-hand orientation relation once
for the injected/OpenXR process and retains it while soldier/Skeleton
lifetimes change. Its current position uses the provisional PID 33220
tracking-to-Skeleton translation; installed items still have different
authored camera positions, skeleton parts, and mesh pivots. Automatic
native-metadata normalization is in development. Use only a local/offline
infantry map and report wrong shot direction, guarded-fire log messages,
focus-loss behavior, or discomfort. Vehicle behavior is not yet supported by
this gate.

## Latest headset acceptance

The current regression-correction build has passed an owner headset check for
the main infantry/UI path: VR enters from the launcher before spawn, controller
translation has no artificial reach limit, weapon scale is correct, the native
crosshair is hidden, and the menus/HUD are acceptable for the current stage.
The startup panel is world-static and the gameplay HUD remains head-following.

This is not yet vehicle or full-reset certification. The ADS/Willys case,
device-reset recovery, full weapon/item grip normalization, and fixed-range
muzzle/impact alignment remain deliberate follow-up tests.

## OpenXR presentation smoke test

### Normal and deep diagnostics

BFVR has one source tree and one normal launcher build; there are not separate
copies of the code to keep in sync. `Launch-BFVR-VR.bat` sets
`BFVR_DIAGNOSTICS=normal`. Normal mode keeps every visual feature, required
graphics-state restoration, failure check, useful event log, and 30-second
performance summary. It skips only two expensive per-draw proof tools:
reading restored graphics state back from Direct3D to prove it byte-for-byte,
and grouping every draw by its diagnostic provenance.

For a troubleshooting run, change the launcher's one setting to:

```bat
set "BFVR_DIAGNOSTICS=deep"
```

Deep mode turns those proof tools back on. Change it back to `normal` for
ordinary play and performance testing. Bounded no-headset proof probes always
use deep diagnostics. A compiler Debug build is also generated from the same
source only when diagnosing a rare memory/calling-convention fault; it is not a
second version that receives separate edits.

The normal log now reports the rate and pacing of newly completed BFVR stereo
frames, detailed x86 replay stages, original Direct3D `Present`, transport
waits, the OpenXR runtime's predicted refresh period, and how often the headset
had to receive the last complete image because a new one was not ready. These
“new BFVR frames” are not vanilla BF1942's frame rate.

The x86 game bridge and x64 presenter use two named auto-reset events as
cross-process doorbells instead of repeatedly sleeping and checking for frame
changes. Shared sequence numbers remain authoritative, and BFVR automatically
falls back to bounded polling if those events are unavailable. This first
transport optimization does not change resolution, FXAA, rendered features, or
the one-set image path. Headset timing should compare `consumeWait`,
`nextRequestWait`, and their sum before adding a second native-resolution
texture set: Oculus publishes the next pose only after the current source has
been consumed/submitted, so those two waits are not fully independent.

The original standalone probe is x86 because it shares the client build. The
currently installed Oculus x86 runtime faults inside `xrCreateSession`, while
the corresponding x64 runtime has been independently proven healthy. BFVR
therefore keeps the game client x86 and builds `BFVRPresenter` as an x64-only
companion around the same `OpenXRPresentation` module.

Configure the companion build separately:

```powershell
cmake -S .\BFVR\src -B .\build\bfvr-presenter-x64 `
  -G "Visual Studio 17 2022" -A x64 -DBFVR_PRESENTER_ONLY=ON
cmake --build .\build\bfvr-presenter-x64 --config Release
```

The x86 producer and x64 consumer probes first validate transport without a
headset or OpenXR. They exchange three named keyed-mutex D3D11 textures on the
presenter-selected adapter and verify exact copied pixels:

```powershell
.\build\bfvr-cmake-validate\BFVRSharedTextureProducerProbe.exe `
  --presenter .\build\bfvr-presenter-x64\Release\BFVRSharedTextureConsumerProbe.exe `
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
  --consumer .\build\bfvr-presenter-x64\Release\BFVRSharedTextureConsumerProbe.exe `
  --consumer-log .\build\diagnostics\bfvr-d3d9ex-x64.log
```

This creates two R10 world targets and one R16-float UI target through
translated D3D8/D3D9Ex, opens them in x64 D3D11, converts them to BGRA, waits
for both APIs' GPU work before reuse, and verifies exact pixels without CPU
pixel transport. The standalone helper-allocation regression can be selected
for one process with `BFVR_D3D8TO9_FORCE_SHARED_HELPER=1`.

The no-game ambient-occlusion depth prerequisite has its own x86 hardware
probe. It validates texture-backed `INTZ` depth/stencil rendering, packed and
float D3D9-to-D3D11 export, exact D3D8-visible state restoration, and GPU
timestamps at the current native eye size:

```powershell
.\build\bfvr-cmake-validate\BFVRAmbientOcclusionDepthProbe.exe `
  --width 1872 --height 2016 --iterations 64
```

The matching x64 control executes the native-resolution spatial AO and bilateral
denoise shaders, verifies non-trivial output, and compares the existing stereo
world conversion with and without the AO sample:

```powershell
.\build\bfvr-presenter-x64\Release\BFVRAmbientOcclusionGpuProbe.exe `
  --width 1872 --height 2016 --iterations 64
```

The SSGI hardware control renders a synthetic red-source/neutral-source corner,
requires measurable red-only radiance across at least 5% of receiver pixels,
and reports per-eye/stereo GPU time for native preparation, gather, and denoise:

```powershell
.\build\bfvr-presenter-x64\Release\BFVRScreenSpaceGlobalIlluminationGpuProbe.exe `
  --width 1872 --height 2016 --iterations 64
```

Brightness bloom has a separate x64 hardware control. It renders a small
white source over a dark world, proves that the halo changes pixels outside
the source, and reports the GPU time for quarter-resolution extraction plus
the horizontal/vertical blur:

```powershell
.\build\bfvr-presenter-x64\Release\BFVRBloomGpuProbe.exe `
  --width 1872 --height 2016 --iterations 64
```

Add `--ambient-occlusion` to `BFVRD3D8To9SharedSurfaceProbe` for the complete
no-game INTZ -> packed D3D9 shared depth -> x64 AO -> world-composite control.
The ordinary invocation above remains the exact no-AO regression.

Add `--ssgi` for the complete no-game INTZ -> packed D3D9 shared depth -> x64
native-resolution SSGI -> world-composite control. Its
uniform-depth scene must preserve all transported world/UI pixels exactly.

Add `--water-reflections` instead for the complete water-SSR transport control.
It clears the packed target's alpha mask, resolves depth into RGB without
changing alpha, initializes the x64 water pass, and verifies that an empty mask
leaves all three transported images exact. The separate positive shader control
uses a masked synthetic water plane reflecting a dry scene wall:

```powershell
.\build\bfvr-presenter-x64\Release\BFVRWaterReflectionGpuProbe.exe
```

Live AO is experimental and default-off. Set `BFVR_OPENXR_AO=1` in the launch
environment to request it. Unsupported adapters, depth allocation/resolve
failures, invalid frame projections, or x64 AO setup failures retain the
ordinary D24S8/color/UI path; the established three shared color textures stay
mandatory. Ref2 UI and bloom are not processed by AO. Use the same build with
the variable unset for the required headset A/B.
The current owner-comparison build uses native-eye-resolution R16_FLOAT AO
intermediates, world-composite intensity 1.0, and a per-pixel-rotated
eight-sample 0.60 m view-space disk. Projected radii remain exact through 24
pixels and use a C1-continuous transition over 24..72 pixels into a 48-pixel
cap. The 3x3 bilateral denoiser uses its original absolute-plus-relative
view-depth tolerance. The arbitrary view-distance fade is absent; AO fades only
when its projected footprint becomes subpixel. Native resolution avoids
stretching a half-resolution result through the ordinary color sampler. The AO
pass explicitly installs its complete viewport/scissor/blend/depth/raster
state, and packed-depth transport is unchanged.

Live screen-space global illumination is a rejected experiment and is disabled
in the current launcher with `BFVR_OPENXR_SSGI=0` and intensity `0.0`. The x86
producer accepts only the exact value `1`; every other value omits the SSGI
protocol flag. Consequently the x64 consumer does not initialize SSGI shaders,
allocate its native-resolution intermediates, select an SSGI composite shader,
or enter its per-frame gather/denoise path. AO or water SSR can still request the
shared packed-depth targets independently.

`BFVR_OPENXR_SSGI_DEBUG=1` replaces each world eye with a 32x-exposed SSGI
radiance diagnostic: valid pixels with zero bounce are black, while pixels
whose packed-depth/reconstructed-normal guide did not survive preparation and
denoising are magenta. Value `2` shows guide coverage directly (white valid,
black invalid). Both diagnostics leave Ref2 UI unchanged; use `0` for the
ordinary composite.

SSGI reconstructs per-eye view positions and normals from the same packed depth
used by AO, prepares a native-resolution guide, and gathers four per-pixel-
rotated spatial samples from the matching final world-colour texture inside a
4.0 m view-space radius. Directional two-surface bounce is supplemented by a
bounded finite-patch term that spreads only brighter-neighbour colour contrast;
a flat uniformly coloured surface therefore remains unchanged. Projected radii
enter a C1-continuous 320-pixel safety limit, and a 3x3 relative-depth/normal
denoiser runs three ping-pong passes at native resolution. Each eye is processed
independently; Ref2 UI, menus, water-mask alpha, and the other eye are never
sampled. There are no
motion vectors, frame-varying noise, or temporal history. Allocation, shader,
depth, or projection failure falls back to the normal world image for that frame.

The native additive-water repair keeps complete stereo View/Projection
geometry and uses `D3DRS_LOCALVIEWER=FALSE` only for the exact reflection pass.
Its current texture-basis experiment is default-on: the original stage-0
texture matrix is rotation-only rebased from each replay eye into BF1942's
logical camera basis, then restored after the draw. Set
`BFVR_WATER_REFLECTION_TEXTURE_BASIS=0` for a matched comparison that keeps the
accepted no-band repair but disables only the new basis correction. The older
`BFVR_STEREO_WATER_REFLECTION=1` comparison disables the complete native-water
repair and can therefore restore the rejected eye-relative bands.

Water-only screen-space reflections are experimental and default-off in code.
Set `BFVR_OPENXR_WATER_SSR=1` to request them; the comparison launcher currently
does this explicitly. `BFVR_OPENXR_WATER_SSR_INTENSITY` defaults to `1.0` and
accepts `0.0..2.0`. Set only `BFVR_OPENXR_WATER_SSR=0` for a matched native-water
A/B.

The x86 renderer derives a dedicated per-eye mask only from BF1942's exact
depth-writing additive/specular `WaterSurface` replay. It writes that material's
own alpha to the alpha channel of the packed-depth texture while the ordinary
depth resolve writes RGB only. The x64 pass reconstructs view space from each
eye's projection, estimates a water normal from nearby masked depths, rejects
water self-hits, uses a conservative 24-step spatial ray march with six-step
depth-crossing refinement, fixed roughness, and Schlick Fresnel, and blends the
result only into the corresponding world eye.
There is no temporal history or motion-vector dependency, and Ref2 UI is never
sampled or modified. Missing masks/depth/projections, unsupported resources, or
shader failures retain the native water image for that frame.

This exact-pass mask also addresses Galactic Conquest's invisible space-map
water without a mod-name special case. Local archive inspection found
`water.specularEnable 0` in `GC_DeathStar`, `GC_Judicator`,
`GC_Judicator_Push`, and `GC_Taskforce`; those maps do not submit the mask-authority
specular pass, so their base water cannot receive SSR. Live validation is still
required for other maps and mods. As with all screen-space reflection methods,
off-screen or occluded source detail cannot be reflected and shallow-angle
disocclusions can create misses.

The focused GPU control also enforces intersection density. Its synthetic
masked water plane must reflect a dry wall across at least 75% of the pixels in
its active rows, with no empty row inside the reflection band. This prevents a
coarse one-sample thickness test from regressing into horizontal stipple bands.

Live bloom is a new default-off, world-only experiment. Set
`BFVR_OPENXR_BLOOM=1` to request it; the comparison launcher currently does
this explicitly. `BFVR_OPENXR_BLOOM_THRESHOLD` defaults to `0.55` in linear
color and accepts `0.0..1.0`; `BFVR_OPENXR_BLOOM_INTENSITY` defaults to `0.35`
and accepts `0.0..2.0`. Set only `BFVR_OPENXR_BLOOM=0` for a matched A/B.
The implementation extracts each source sample before quarter-resolution
downsampling, applies a fixed soft knee and separable blur, then actually binds
and adds the result during the linear world composite. Ref2 UI never enters
the extraction or composite. AO uses an independent texture input, and FXAA's
accepted shader route and controls are unchanged. Enabled runs report isolated
per-eye and stereo extract-plus-blur GPU timing summaries during shutdown.

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

The x64 presenter covers the translated BF1942 client with a BFVR-owned
desktop preview: a centre-cropped right-eye image plus BFVR's UI texture,
rather than the underlying flat game HUD/arms. Its swapchain matches the
current BF1942 client resolution and is recreated if that resolution changes;
there is no 1280x720 intermediate upscale. It presents each accepted BFVR
source frame with a non-blocking desktop present. This is a small D3D11
composite pass over the same accepted eye/UI textures, not an OpenXR runtime
compositor mirror or an extra game/stereo render. Set `BFVR_DESKTOP_MIRROR=0`
before launch to opt out.
Because OpenXR does not provide a portable compositor-mirror contract, BFVR
also reproduces its separately submitted Quick Menu and cursor layers in this
preview. Their LOCAL-space poses are projected through the current right-eye
pose/FOV and the same centre crop, using the exact hover-specific art textures.
The preview remains source-driven normally; it refreshes each OpenXR frame only
while the Quick Menu is visible and once after release to clear it.
World FXAA remains enabled by default, as in the owner-accepted visual path;
set `BFVR_OPENXR_FXAA=0` only for a deliberate measured A/B.

For an owner-observed session without a fixed duration, omit
`--diagnostic-timeout-ms` and add `--run-until-stopped` to the combined
`--d3d8to9-observer-probe --d3d8-openxr-presentation-probe` command. It runs
until the directly launched BF1942 process actually exits. There is no
independent named renderer-stop event: the earlier event route could cleanly
tear down only the injected VR renderer while the game remained alive.
The x64 presenter separately monitors producer-process lifetime so it cannot
be orphaned. Bounded mode remains the default. A renderer/probe completion is
diagnostic information only and never closes the directly launched BF1942
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
default-off. When explicitly enabled, only the world eyes compile and execute
the independent quarter-resolution brightness extraction/blur and bind its
HDR result at the final linear composite; the default path still compiles no
bloom shaders and allocates no bloom resources.

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
