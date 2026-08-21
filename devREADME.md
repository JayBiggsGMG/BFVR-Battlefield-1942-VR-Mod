# BFVR distribution root

`BFVR` is the one and only parent folder required to distribute and remove the
Battlefield 1942 VR mod.

The eventual release ZIP will extract this directory beside `BF1942.exe`:

```text
Battlefield 1942\
  BF1942.exe
  BFVR\
    BFVR.exe
    BFVRClient.dll
    runtime\
    licenses\
```

No release component may replace `BF1942.exe`, install a proxy DLL in the game
root, modify game data, or require files in multiple game folders.
`BFVR.exe` locates the game relative to this directory and loads the client
module from within `BFVR`. The installer creates this directory and optional
shortcuts; the player does not need the development batch file.

## Audio compatibility

BFVR does not replace, reroute, or otherwise modify Battlefield 1942's audio
backend. Audio remains owned by the installed game and any user-selected audio
patch, including fan builds that restore hardware audio or provide HRTF.

## Multiplayer compatibility and throwable items

BFVR supports multiplayer motion-controller aiming for firearms and throwable
items such as grenades, TNT, mines, and similar gadgets. It remains compatible
with ordinary, unmodified servers and does not require special BFVR server
software.

For alive infantry, the VR camera excludes BF1942's legacy recoil/shake view
rotation. Snap and Smooth turns immediately rotate one shared local tracking
anchor for the HMD, controllers, arms, and weapon while the same native input
continues turning BF1942's authoritative soldier body. As that body catches up,
the temporary local lead is consumed without producing a second visible turn.
Multiplayer still uses the stock PlayerAction route; snap turns are divided
into network-valid axis samples instead of being clipped by the compact packet
encoder. BFVR does not write the replicated body transform or change weapon
state, firing, packets, projectile direction, models, or IK.

BF1942's paired native recoil pattern is transferred from the legacy camera to
the physical held gun at `1.5x` pitch/yaw with established two-hand support and
`3.0x` pitch/yaw one-handed. This changes amplitude only: native sample order,
randomization, weapon timing, and the `0.10`-second residual-return half-life
remain unchanged.

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
- Right B: reload on press. Hold continuously for 2 seconds to recenter the
  current infantry or vehicle view using the same action as VR Settings >
  Recenter Forward; one hold produces one recenter and reload remains intact.
- Left X / Y / grip: prone / scoreboard (hold through BF1942's paired native
  player/global HUD semantics) / proximity-gated weapon support.
  Prone and jump clear a controller-toggled crouch.
- Map action: toggle BF1942's expanded map with one native `M` press per button
  press. Left Menu is the default Touch and Vive binding. Index has no default
  because it lacks an application Menu component, but SteamVR may bind the
  named Map action to a control on either hand. The keyboard `M` key remains
  available.
- Both sticks: a dead-zone-remapped analogue response curve; small deflections
  move/turn gently and full deflection reaches full native axis input.

### Controller profiles and SteamVR remapping

BFVR publishes the named OpenXR action set `BFVR controller input`. SteamVR
users can select the running `Battlefield 1942 VR` application in SteamVR's
controller-binding interface, inspect the functional action names, change the
physical inputs assigned to them, and save a personal binding. BFVR consumes
OpenXR actions only; it does not read Index or Vive hardware directly, so a
SteamVR remap remains authoritative.

The default profile layouts are:

- Meta/Oculus Touch remains unchanged: left X/Y are primary/secondary and
  right A/B are primary/secondary. All poses, triggers, squeezes, sticks,
  clicks, haptics, and left Menu retain their established BFVR actions.
- Valve Index/Knuckles maps physical A/B to primary/secondary on each hand.
  Consequently left A acts like Touch X (Prone), left B like Touch Y
  (Scoreboard), right A like Touch A (Quick Menu), and right B like Touch B
  (Reload; hold 2 seconds to recenter). Triggers, squeezes, sticks, stick
  clicks, aim/grip poses, and haptics remain on the corresponding hand. Index
  exposes no application Menu component, so Map has no default Index binding;
  it can be assigned to either controller through SteamVR.
- HTC Vive Wand uses each trackpad as the corresponding movement/turn stick
  and its trackpad click as the corresponding stick click. Triggers, binary
  squeeze buttons, aim/grip poses, and haptics retain their per-hand roles.
  Left Menu toggles the map and right Menu opens the Quick Menu. The Wand has
  no remaining default inputs for Prone, Scoreboard, or Reload; users who need
  those actions can assign them through SteamVR's controller-binding UI.

These mappings use the interaction-profile paths standardized by OpenXR. In
particular, each runtime owns its controller-specific aim pose, so BFVR does
not apply a Quest-derived angular correction to Index or Vive pointer rays.

VR Settings page 2 groups `Comfort Vignette`, `Death Camera Comfort`, `Show`,
and the default-on `Menu Pointer Smoothing` toggle. `Show` selects Arms & Hands, Hands Only, or No
Hands/Arms. While the local soldier or occupied vehicle
translates, BFVR smoothly closes to a stable, feathered black peripheral
aperture and restores the full view after movement stops. A filtered
movement-state trigger prevents quantized game transforms from making the
aperture pulse between simulation updates. Page 3 groups manual height,
standing calibration, and forward recentering. Head movement, artificial
turning, turret aiming, and a
vehicle rotating in place do not trigger it. The effect is procedural—there is
no vignette image asset—and OpenXR composites it over the world but under the
native HUD, scope texture, Quick Menu, Settings, and other overlays.

`Menu Pointer Smoothing` uses the same normalized, frame-time-aware adaptive
filter for Battlefield's native menu injection, the Quick Menu, and VR
Settings. A small output deadzone absorbs controller tremor, while the cutoff
rises with pointer speed so deliberate movement remains responsive. Cursor
rendering and hit testing consume the same filtered point. Tracking loss,
leaving a panel, switching Quick Menu surfaces, or disabling the toggle clears
history; Controls remain clickable at their displayed cursor position. Save
applies the setting live without changing any startup or OpenXR path.

The potential 1.0.2 VR Settings presentation adds a green
`BFVR v1.0.2 - JayBiggsGaming` credit as a separate transparent OpenXR quad
below the menu. It deliberately leaves the authored 1024-square menu texture,
panel pose, and ray-to-pointer mapping unchanged; the desktop mirror composes
the same extra quad. `BFVRVersion.inc` is the canonical release version shared
by this C++ text, loader console text, the Windows version resource, and the
Inno Setup metadata/output name.

When the local player controls a non-default vehicle or mounted
`PlayerControlObject`, BFVR switches only the stick axes while retaining the
buttons above:

- Land, sea, AA, and mounted guns: left Y is proportional gas/throttle or
  reverse, left X is proportional steering, and the right stick continuously
  supplies native camera or turret/station traverse and elevation. For an
  occupied PCO with a valid non-empty native weapon vector, tracked
  right-controller movement adds fine relative aim on the same axes: hand-left
  moves the barrel right and hand-down moves it up, as though the gun pivot
  were between the controller and barrel. Empty or unreadable weapon vectors
  disable only that physical-motion contribution, so unarmed driver/passenger
  freelook remains head- and stick-controlled. Holding the hand still holds aim
  still; the stick remains the unrestricted control for continuous/360-degree
  traverse. Controls now
  exposes a saved **Turret Motion Sensitivity** slider from 50% to 300% in 10%
  steps. The 200% default produces 96.0 native input units per metre and halves
  the initial headset build's physical travel; 100% preserves that original
  response. The slider scales only controller-motion aim, while the existing
  pitch/yaw inversion options continue to affect both motion and stick aim.
- Aircraft defaults to left Y throttle, left X roll, right X yaw, and right Y
  pitch. The saved Controls option `Aircraft Pitch + Roll on Same Stick` pairs
  roll with pitch and moves yaw to the other stick. `Swap Aircraft Sticks`
  exchanges the complete pitch-stick and throttle-stick roles between the left and right
  controllers. Together these two independent options allow pitch/yaw or
  pitch/roll on either physical stick. `Flight Pitch (Invert Vertical Stick)`
  only reverses the chosen pitch stick's vertical direction. With inversion
  off, stick up uses BF1942's positive pitch/dive direction and stick down
  climbs.

The switch uses BF1942's current-versus-default control-object relation, not a
null pointer or configurable key binding. A soldier is the default
`PlayerControlObject`; vehicles and stationary weapons are non-default control
objects. The controlled template's native `VCAir` category selects the aircraft
axes, while land/sea and unreadable mod categories never acquire aircraft-only
roll/yaw mapping. Entering, leaving, or changing the exact occupied control
object clears crouch/directional stick state and captures a fresh zero-input
motion reference so neither infantry state nor a stale hand position can leak
across a seat transition.

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
sea, and mounted control objects with a proven non-empty native weapon vector
translate newly timestamped right-grip position deltas into bounded native
turret/station `mouseLookX/Y` input. Unarmed and unresolved seats fail closed
without affecting stick look. The opt-in weapon
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
BF1942's native zoom state keeps the HMD-positioned stereo view aimed along its
native pre-VR source-camera direction, restores that weapon's configured
relative FOV, and presents the native scope raster as an eye-filling head-locked
overlay. BFVR calibrates the accepted right-controller aim against the exact
native-arm gun basis at entry, then continues reading that tracked aim while
BF1942 hides its normal first-person arm update during native scope view. A
transient tracking miss retains the last valid target rather than dropping
magnification or the overlay. When primary two-hand support was already
established at entry, the controller target also reuses that exact authored
support relation and bounded fixed-pivot steering with fresh left-grip motion.
Releasing or losing the left grip returns the target to right aim; this does not
create a second grip or write native grip state. The smoothed controller basis
drives both the visible magnified direction and BF1942's native authoritative
aim target. This removes visible native-camera catch-up while preserving
BF1942's projectile origin, weapon/barrel offsets, spread, cadence, projectile
creation, and stock-server authority. Because the stock normalized look route
still converges in the background, an immediate shot after an unusually large
hand jump may briefly differ from the displayed ray.
Off-hand acquisition uses an 18 cm radius around a long gun's unchanged
animation-authored support point. Close sidearms retain their 12 cm captured-cup
radius. Both gates remain acquisition-only: neither changes a hand target or
weapon anchor, and an established grab has no distance-based auto-release.
The WorkInProgress launcher keeps both the temporary recorder and its audit
sink disabled with `BFVR_OFFHAND_CALIBRATION=0` and an empty
`BFVR_OFFHAND_CALIBRATION_LOG`. Set the recorder to `1` and restore a dedicated
audit path only for deliberate calibration.
With a primary slot-3 weapon equipped,
release left squeeze, place and rotate the free visual left hand at the desired
socket, and click the left stick. BFVR records and immediately flushes the
complete native and calibrated left-wrist-from-right-wrist relations directly
to the configured dedicated
`build/bfvr-release-win32/RelWithDebInfo/logs/offhand-calibrations.log` and
hot-applies the
calibrated relation only to that exact live soldier/Skeleton/item identity. A
held stick cannot recapture every frame; release and click again to replace the
session value. Capture/rejection records use the narrow
`OFFHAND_CALIBRATION_` audit prefix. The dedicated capture file does not depend
on the ordinary observer callback or diagnostics level; an observer copy is
also permitted when available, without enabling any other diagnostic family.
Sidearms, gadgets, untracked hands, and an already-held
support are rejected. The experiment writes no persistent calibration data
and is disabled by setting the environment value to `0`.
The accepted 2026-08-15 corrections are separate from that recorder and remain
active with it off. They identify DP, MP18, Type5, AK47, and Saiga12k through
the exact native `HandFireArmsTemplate` zoom-FOV and soldier-camera-position
properties already read from the active item, then require primary slot 3 and
a close match to the captured native wrist relation. The duplicate MP18
captures select the nearest recorded native relation. Unknown template
properties, another item slot, or a materially different native pose remain
unchanged. This secondary pose fingerprint prevents a mod that reuses a stock
weapon template but authors a different left-hand placement from inheriting
the correction.
The runtime `soldierCameraPosition` vector begins at template `+0x3F0`; a
2026-08-15 live DP probe rejected the earlier `+0x3EC` assumption by exposing
the preceding zero float followed by only the first two configured components.
The subsequent owner headset pass accepted all six stored corrections: Russian
assault and medic, Japanese medic and engineer, and Chinese medic and engineer.
The later No4Sniper Oculus/SteamVR A/B exposed a separate native-capture
boundary: the current generic arm path waits only two untouched attachment
callbacks and freezes the next left-from-right relation. The two runtimes
captured No4 relations 6.37 cm and 1.26 degrees apart even though neither
matched an override. This is a general first-sample timing risk; it must be
corrected with automatic native-relation convergence, not manual per-weapon
measurement or a broad vanilla weapon table. The six saved overrides also keep
their secondary relation gate because it is what prevents a mod with different
authored hand placement from inheriting a correction.

The default-on `Sniper Aim Smoothing` checkbox on the Controls page affects
only scoped micro-motion. It applies a frame-time-aware spherical orientation
interpolation while total stabilized-to-raw error remains below `1.5` degrees;
as error grows, raw input receives more weight, and at the boundary the aim
catches up to raw immediately. Current weapon translation is always preserved.
The same aim ray drives scoped presentation and native authority convergence,
so this is not view-only smoothing. Controls > Save applies the toggle live and
clears stabilization history immediately. Scope, tracking, weapon, soldier, or
sample-time discontinuities also restart from raw. An accepted
local scoped shot releases BFVR's exact zoom
override ownership, allowing BF1942's native post-fire `setZoom` and soldier
zoom-bit decisions to pass through unchanged. Stock bolt-action rifles can
therefore exit before chamber animation as in the flat game, while a modded
weapon keeps whatever native behavior it authors. No weapon class, animation
time, or reload timer is assumed. Zoom-only
weapons without `useScope` receive no BFVR camera, FOV, or overlay policy.
Scope exit clears smoothing history and returns camera direction to normal;
unscoped aim, firing, grips, arms, elbows, IK, recoil, artificial turning,
vehicles, and mounted weapons are not filtered by this feature.
The temporary SteamVR smoothing audit remains available for a deliberate
future investigation, but the WorkInProgress launcher now clears
`BFVR_SCOPE_SMOOTHING_AUDIT_LOG`. With no explicit audit path the client does
not collect its outcome counters or write a scope-exit summary. If deliberately
enabled, it classifies filtered samples, duplicate generations, invalid/zero
timestamps, intervals above 50 ms, and 1.5-degree raw bypasses without
altering the filter result.

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
for controller-pointer items: knife (1), grenades/TNT (4),
mines/binoculars/medpack (5), and wrench (6). Pistols, ordinary guns, launchers,
and the detonator remain outside that pointer family and use their established
aim paths. Unknown modded items which reuse slots 1/4/5/6 remain enabled by
policy because those slots are conventionally pointer-item families; all other
infantry slots remain excluded. A non-default
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

Controls page 2 keeps all stereo 3D crosshair policy together: hand-weapon,
mounted-weapon, and knife/throwable/gadget modes, the eight-color selector,
and opacity. Opacity is persisted as 5..100% in 5% steps and scales both RGB
and alpha for BFVR's premultiplied aiming-crosshair and hit-marker blend. The
parser and renderer both clamp to a nonzero 5% floor; native scoped crosshair artwork
continues to receive only the selected RGB tint because Battlefield's recovered
scope color setter has no alpha parameter.

Exact scope is the exception: BFVR suppresses these world-overlay layers while
BF1942's native CrossHair path carries the scope raster and native hit
feedback. During active/pending exact scope only, BFVR applies the selected 3D
Crosshair Color through the prefix-verified native color setter, then restores
the captured live native RGB on exit. This does not edit profile files or
recolor scope artwork.

The potential 1.0.2 settings work keeps that architecture intact. The hand,
mounted, and controller-pointer knife/throwable/gadget sources now each use a
saved three-state display mode. `Crosshair.png` and `HitMarker.png` remain
aligned 64x64 premultiplied layers but are neutral grayscale art; one vertex
diffuse tint selects White, Green, Blue, Purple, Red, Pink, Orange, or Yellow
for both eyes and both layers. The tint draw still occurs immediately before transport into
the owned stereo-world targets, so later world color treatment intentionally
affects it with the scene.

The first-person part-name classifier remains behind the prefix-verified
`AnimatedMesh::draw` forwarding context, but it no longer scans template names
for every animated world mesh. The hook now records only the current mesh;
classification is requested lazily after the existing projection/semantic
policy proves a first-person candidate and only when Hands Only needs to
distinguish separate hands. A bounded per-thread cache stores both recognized
hands and conservative combined/unrecognized results using the template
address plus name-storage identity. Arms & Hands and No Hands/Arms require no
classification, and ordinary soldiers never enter the classifier in any mode.

The owner's first SteamVR headset A/B rejects that classifier as a material
performance explanation: facing bots felt unchanged after the lazy/cached
build, and Arms & Hands versus Hands Only produced no noticeable cadence
difference. The matching 2026-08-16 SteamVR 2.16.7 records instead show a
runtime-level cadence mismatch. Across 156.892 seconds BFVR submitted 9,900
frames and consumed 9,426 fresh game frames, while the 90-Hz compositor made
14,079 headset presents and reprojected 4,081 (29.0%) with zero dropped frames.
BFVR remained healthy and logged no session, device, or swapchain failure; the
owner's occasional brief black flashes therefore remain unassigned and may be
below BFVR's application-visible boundary. Treat the global name scan as
removed unnecessary work, not as the cause of the observed SteamVR slowdown.
VirtualDesktopXR is a separate runtime, not a SteamVR extension: its same-day
log requested 2496x2688 per eye versus SteamVR's 1872x2016, exactly 1.78 times
the pixel area, so its performance must be compared at matched source
resolution and refresh rate before attributing a shared runtime defect.

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

### Off, normal, and deep diagnostics

BFVR has one source tree; there are not separate copies of the code to keep in
sync. `Launch-BFVR-VR.bat` sets `BFVR_DIAGNOSTICS=off` for player-performance
testing. Off mode keeps every visual feature and all required graphics-state
restoration while disabling the developer log, per-draw CPU timers, firing
traces, AO/bloom GPU timers, presenter stage timers, and startup-only metadata
samplers.

Set the value to `normal` when useful event logs and 30-second performance
summaries are needed. Normal skips expensive restored-state readbacks and draw
provenance aggregation.

For a troubleshooting run, change the launcher's one setting to:

```bat
set "BFVR_DIAGNOSTICS=deep"
```

Deep mode turns all proof tools back on. Change it back to `off` for ordinary
play and release-performance testing. Bounded no-headset proof probes always
use deep diagnostics. Developer debugging and diagnostics remain part of the
same developer build.

The normal log now reports the rate and pacing of newly completed BFVR stereo
frames, detailed x86 replay stages, original Direct3D `Present`, transport
waits, the OpenXR runtime's predicted refresh period, and how often the headset
had to receive the last complete image because a new one was not ready. These
“new BFVR frames” are not vanilla BF1942's frame rate.

The ordinary launcher explicitly sets `BFVR_PERFORMANCE_SUMMARY=0` alongside
broad diagnostics-off behavior. For a deliberate low-noise performance
capture, temporarily set that explicit launcher assignment to `1`, or use a
separate diagnostic launcher that supplies the flag without overriding it.

This opt-in records in-memory aggregates and emits one summary every 30 seconds
plus a final summary. It does not enable per-frame logging, draw provenance, or
deep restored-state readbacks. The x86 summary covers total replay,
skinning-family draw counts, native Present, producer waits, and new-frame
pacing. It intentionally leaves the optional per-draw prepare/draw/restore
timers off so soldier-heavy scenes do not gain extra clock reads. The x64
summaries separate shared-texture
acquisition, SSGI/water-SSR/AO/composite/menu command enqueue, D3D11 flush,
`xrWaitFrame`, `xrBeginFrame`, each swapchain acquire/wait/copy/release, and the
actual `xrEndFrame`. Existing AO, SSGI, and bloom GPU timestamp reports are also
enabled for the explicit profiling run. CPU enqueue times are not GPU duration;
compare them with `sourceFinalize` and those GPU timestamp summaries.

The targeted flag is inherited by both BF1942 and its x64 companion when a
diagnostic launcher enables it. The ordinary `Launch-BFVR-VR.bat` explicitly
forces both `BFVR_DIAGNOSTICS=off` and `BFVR_PERFORMANCE_SUMMARY=0` so normal
headset use performs no aggregate timing capture.

The x86 game bridge and x64 presenter use two named auto-reset events as
cross-process doorbells instead of repeatedly sleeping and checking for frame
changes. Shared sequence numbers remain authoritative, and BFVR automatically
falls back to bounded polling if those events are unavailable. This first
transport optimization does not change resolution, FXAA, rendered features, or
the one-set image path. Headset timing should compare `consumeWait`,
`nextRequestWait`, and their sum before adding a second native-resolution
texture set: Oculus publishes the next pose only after the current source has
been consumed/submitted, so those two waits are not fully independent.

Two SteamVR-specific speculative future-frame pipelines were live-rejected and
are absent from the current source. The first waited up to 100 ms after
`xrEndFrame`; frames 352, 353, 354, and 2972 exhausted that interval and caused
severe stale-image stutter. A second version kept OpenXR cadence independent and
never waited for the source, but owner testing found performance every bit as
bad. Do not restore either `predictedDisplayTime + predictedDisplayPeriod`
source buffering design. The established immediate runtime-timed path remains
active for SteamVR, Oculus, and VirtualDesktopXR.

A distinct 2026-08-18 Present-boundary overlap experiment was also live-
rejected and is absent from the current source. It completed the current
source reads into x64-local textures and acknowledged x86 before `xrEndFrame`,
then allowed an ordinary native BF1942 bubble frame when the next real OpenXR
request was unavailable at Present. It did not predict a pose, but owner
testing found performance the same or probably worse and found a new severe
gun-alignment error whose magnitude depended on look direction. The bubble
frame therefore broke the established synchronization between request-bound
camera/weapon state and the submitted stereo source. The environment flag,
early acknowledgement, pose-clearing branch, policy/test files, and wrapper
launcher were fully removed. Do not restore this scheduling family.

Neither rejected result implicated startup. Preserve the OpenXR 1.0 API request
that fixed SteamVR/VDXR launch and the temporary x86 D3D11 open of the first
D3D9Ex texture before publication that fixed the Oasis driver.

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
the quad layer. Add `--runtime-timed --comfort-vignette-motion` to publish a
bounded synthetic translation through the normal protocol and verify the
procedural vignette initializes, renders, and submits without launching the
game; the presenter log records its first eased layer strength.

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

The native additive-water repair now preserves Battlefield's complete authored
material: scrolling normal map, generated light/reflection lookup, map-provided
`Water.SpecularColor`, `Water.SpecularStreakFactor`, and the original local-viewer
response. The exact specular pass uses the current head-centre View for both
eyes, while each eye's residual View is folded into its Projection. Thus
`World * View * Projection` remains mathematically identical for each eye, but
the fixed-function reflection coordinate has one world-locked viewer instead
of two eye-relative viewers. `BFVR_STEREO_WATER_REFLECTION=1` disables this
repair for a legacy comparison and can restore the rejected eye-relative bands.

Controller vibration is enabled by default and has one saved Controls-page
switch: `Controller Haptics`. It gates a light pulse when the right-hand pointer
enters a different Quick Menu or Settings target, the BFVR Back-to-Game target,
or any selectable item in Battlefield's native frontend, pause, spawn/deploy,
kit, and related menus; one pulse for every accepted local weapon shot; and one
longer pulse on both hands when the local player dies. Native-menu hover hooks
Battlefield's actual `BfMenu::playMenuHighLight`, `playLoadMenuHighLight`, and
`playHudMouseOver` UI event functions; it does not inspect or classify audio.
One-handed fire affects the right controller only. When BFVR's
actual off-hand weapon-support binding is acquired, firing affects both
controllers; raw grip-button state alone does not select both hands.

The potential 1.0.2 audio path keeps Battlefield menu audio inside the game
process. Quick Menu and Settings hover/activation events advance pointer-free
protocol-v22 counters; the x86 BfMenu input thread consumes them and calls the
prefix-verified `playLoadMenuHighLight`, `playLoadMenuOk`, or
`playLoadMenuCancel` wrapper on its live menu object. The BFVR Back-to-Game
button calls the same original wrappers directly. This preserves the active
`MenuSound.ssc`, mod replacements, native audio device, and native menu-volume
grouping without extracting stock sound files.

The separate default-on `Kill Sound` option uses two verified retail score
boundaries. Remote MP retains the confirmed forwarding hook at
`GameClient::handleGameEventManagerEvent` (`0x004933D0`): it accepts only
`GEScoreMsg` type `0x2A`, ordinary subtype 3, resolves killer/victim IDs through
native `GameClient::findPlayer` (`0x00491980`), and requires the killer to match
PlayerManager's current BFPlayer (with GameClient `+0x170` as fallback). SP and
listen-server play additionally observe `GameServer::handleScore` at
`0x004AD2D0`, whose arguments directly distinguish ordinary kill type 3 from
teamkill type 6 and identify both scoring player and victim. That path requires
the scoring player to equal PlayerManager's current player and the victim to be
a different non-null player. Both native handlers are forwarded unchanged; an
identical killer/victim pair seen at both boundaries within two seconds is
published once. Separately, the first confirmed kill opens a 300 ms multi-kill
burst; further confirmations in that window remain silent so a grenade does not
stack several voices.
The x64
presenter loads `assets/Sounds/killsound.wav` once and creates one independent
XAudio2 source voice per audible kill, retaining the PCM buffer until all
voices finish. Kills outside the burst window can overlap without restarting.
The authored WAV
follows Windows/headset output volume; no unverified Battlefield master-volume
address or profile file is consulted. Static cross-build evidence and both
architecture builds support these boundaries. Owner testing confirms correct
SP and MP playback, teamkill silence, and 300 ms multikill grouping.

The 2026-08-15 development asset audit found that the repository WAV and the
active launch-payload WAV could diverge. `BFVR/assets` remains canonical, but
the former CMake `POST_BUILD` copy ran only when a native target actually
rebuilt, and the outer development launcher mirrored only Settings-menu PNGs.
The runtime asset copy is now an always-run dependency of both `BFVRClient`
and `BFVRPresenter`, player staging copies directly from canonical `assets/`,
and the local launcher mirrors that complete tree before launch. The launcher
also rejects any BFVR checkout other than `WorkInProgress`; none of these local
staging operations commits, merges, tags, publishes, or modifies `main`.
Both the active Win32 RelWithDebInfo `BFVRClient` target and x64 Release
`BFVRPresenter` target ran the new synchronization dependency successfully.
The canonical, Win32-runtime, and x64-runtime WAVs then shared SHA-256
`B47B72E209A61B480F28AC3F929FCB4FCADAAEDE9F1B2CC89AB732D71F09371A`.
All 32 Win32 deterministic suites passed. A temporary player-payload staging
run produced 68 manifest files and matched all 55 canonical asset files with
zero missing or differing hashes; the temporary payload was then removed.

The per-eye water surface-reflection pass is controlled by the default-on
`Water SSR` checkbox on VR Settings > Graphics. Saving a changed enable state
shows `Settings saved - restart required`; the next BFVR launch uses the
persisted `water_reflections_enabled = true|false` value because packed depth,
the water mask, and reflection resources are negotiated at startup. The legacy
`BFVR_OPENXR_WATER_SSR` environment opt-in remains only as a fallback for
standalone probes that have no initialized `UserConfig.txt`.
`BFVR_OPENXR_WATER_SSR_INTENSITY` defaults to `1.0` and accepts `0.0..2.0`.

The x86 renderer derives a dedicated per-eye mask only from BF1942's exact
depth-writing additive/specular `WaterSurface` replay. It writes that material's
own alpha to the alpha channel of the packed-depth texture while the ordinary
depth resolve writes RGB only. The x64 pass reconstructs view space from each
eye's projection and estimates a water normal from nearby masked depths. Valid
SSR hits use a conservative 24-step near-field spatial ray march followed by an
eight-step expanding long-range tail, with six-step depth-crossing refinement,
fixed roughness, and physical Schlick weighting. The longer bounded tail and
matching confidence fade recover substantially taller or more distant finite
geometry without making the per-water-pixel search unbounded.
That reflection is blended only into the corresponding world eye, on top of
the native normal-mapped, map-authored sun shine.
BF1942's alpha-blended tree cards test depth but do not write it, so the water
receiver mask can remain present behind their visible pixels. The first cheap
1.0.2 trial capped final SSR replacement at 35%, but the owner saw exactly the
same artifact; the active reflection weight in that scene was evidently not
crossing the cap often enough to matter. That cap is fully removed.

The replacement trial no longer subtracts any original world color. It keeps
the complete source pixel and adds at most 20% of the reflected color into the
pixel's unused brightness, which avoids simple additive blowout. It adds no
texture, draw, target switch, shared handle, protocol field, or pass, and does
not alter tree depth/sorting or the water ray marcher. This is a low-cost visual
workaround rather than material-accurate foliage occlusion and awaits headset
review for tree visibility, water strength, and unwanted brightening.
Both exact embedded Water-SSR composite variants compile under strict `ps_4_0`
and create successfully on the installed D3D11 hardware device. The scaler
initializer also now treats any shader-compilation failure as a real
initialization failure instead of accidentally continuing with a missing
shader. The first build's new success log mistakenly said literal
`20% strength` through a printf-style variadic logger. `% s` consumed a
nonexistent string argument and crashed both the standalone consumer and the
owner's presenter after Water SSR initialization. The owner-run dump confirms
the resulting wide-string read at `0xFFFFFFFFFFFFFFFF`. The log now uses
non-format text `0.20 strength`; the soft-add shader itself is unchanged.
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

When finite-depth tracing misses, the shader can also reflect the authored
skybox already present in that eye's world-color texture. It projects the
infinite reflected direction and accepts the color only when the location is
inside the current eye image, retains clear/far depth, and has no water mask.
No separate sky render or replacement texture is used, and finite geometry
always takes precedence when the marcher hits. Sky directions outside the
current eye image remain ordinary screen-space misses.

The focused GPU control also enforces intersection density. Its synthetic
masked water plane must reflect a dry wall across at least 75% of the pixels in
its active rows, with no empty row inside the reflection band. This prevents a
coarse one-sample thickness test from regressing into horizontal stipple bands.
Separate z=20 finite-wall and clear-depth blue-sky cases require continuous
long-range coverage, finite-source color retention, and sky color only on
masked water; an empty water mask must still leave the output completely clear.

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
.\build\bfvr-cmake-validate\BFVR.exe `
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

Dynamic soldier and vehicle shadows use BF1942's separate projected-terrain
receiver path. The shared WinPC `PatchCellBlock::draw` submission returns at
`0x0069922E`. Retail contains a vector-based `PatchTerrain::drawShadowCells`
at `0x00682D70` (direct-call return `0x00682E95`) and an alternate linked-list
traversal whose direct call returns at `0x00683ADD`; the tested live path uses
`0x00682E95`. Both returns are permanent members of the static classifier, so
the tested live path no longer depends on runtime discovery finding it before
a budget expires. Generation into the auxiliary shadow texture remains outside
the full-backbuffer replay. At the hooked D3D submission, the renderer wrapper
return is stack slot 10, its three caller-owned arguments are slots 11-13,
`PatchCellBlock::draw`'s saved `ESI` is slot 14, and the outer return is slot
15. For only that three-address indexed conjunction, BFVR verifies
stage 0 uses `D3DTSS_TCI_CAMERASPACEPOSITION` and `D3DTTFF_COUNT2`, then
replaces the native texture transform with
`inverse(eyeView) * sourceView * nativeTexture` for each eye. This preserves
the game's world-space projection while terrain geometry keeps its normal
stereo View/Projection. The original texture transform is restored after the
draw, and any signature, state, matrix, or D3D-write failure falls back to the
native path. Runtime summaries expose `projectedTerrainShadows`,
`projectedShadowTextureEyes`, and `projectedShadowTextureFailures`. Deterministic
coordinate-equivalence and semantic-classification tests pass. Headset PID
`15796` proved exact native state, two successful eye writes, exact restoration,
and owner visual acceptance. Normal diagnostics-off headset runs retain only a
sparse `PROJECTED_SHADOW_AUDIT`: one startup/armed record, one first exact
`PatchCellBlock` stack-layout record, one first exact receiver-candidate
signature, one first classified draw/state record, one prepared-matrix delta
record, one result per eye, one restoration record, and one shutdown summary
when teardown runs.
The diagnostics-off filter evaluates the final formatted callback text so the
observer adapter cannot hide these records behind its outer `%s` format. This
does not enable continuous tracing or change render policy.

Earlier builds depended on a bounded runtime discovery window for the tested
`0x00682E95` route. Historical runs prove that its 65,536-draw budget could be
spent before a visible shadow appeared, leaving the correction inactive for
the rest of that process. Discovery remains as compatibility fallback for an
unfamiliar outer return, but it now reads the two stage-0 values only on the
already-profiled shared `PatchCellBlock::draw` indexed submission with the
expected perspective, triangle-list, and alpha-blend shape. It can promote
only that same terrain-cell boundary under the exact native
`CAMERASPACEPOSITION + COUNT2` pair. Observing either proven static producer
caches it and ends fallback discovery for the process. No launcher,
configuration, injection, or OpenXR lifecycle behavior depends on this
fallback.

The visual correction baseline is the exact normally launched
`BFVRClient.dll` tested in PID `15796`: 2,159,616 bytes, SHA-256
`B94A64951FCC1E8D523825E8292F5A17CC8E2895D761BC7CE862F88B1EB8DBE3`.
That local run retained the launch sequence used by the externally validated
Oasis/WMR build: inject bf42++ before BFVRClient while BF1942 is suspended,
initialize the observer, translator, and OpenXR path before resume, then enter
run-until-stopped presentation. It does not itself validate Oasis because the
local machine does not have Oasis.

The reliability correction builds in the normal Win32 RelWithDebInfo tree and
all 36 registered deterministic suites pass, including both proven static
producer returns, nearby-producer rejection, projected-texture state/matrix
coverage, and the existing startup compatibility tests. The x64 Release
presenter also builds, and the launcher's non-starting `--dry-run` resolves the
client, translator, game, and BF42++ paths. The resulting 2,212,864-byte client
SHA-256 is
`F49BC6C393A14AB78F0FF812F3BA903C10A051CD14596282A2301F5A344CCBA8`.
No loader, launcher, OpenXR bootstrap/runtime-selection, presenter, Oasis, or
Steam-launch source changed. A headset run is still required to confirm the
new client reproduces the already accepted shadow appearance every launch.

The x64 conversion stage follows OpenXR's linear-composition contract. It
prefers a BGRA8 sRGB swapchain and converts BF1942's legacy encoded R10/R16 RGB
to linear before output; BGRA8 UNORM remains a linear-data fallback. Its
world-eye path applies FXAA while the Ref2 UI remains unfiltered. The Graphics
Settings page places an `FXAA Sharpening` slider and number box directly below
the FXAA toggle. Its `0..100%` control defaults to a mild `25%` and drives a
CAS-style contrast-adaptive sharpen fused into the existing FXAA pass, reusing
the samples FXAA already fetched. It therefore adds no full-screen pass,
intermediate target, temporal history, or HUD/menu filtering. Bloom is
default-off. When explicitly enabled, only the world eyes compile and execute
the independent quarter-resolution brightness extraction/blur and bind its
HDR result at the final linear composite; the default path still compiles no
bloom shaders and allocates no bloom resources.

The potential 1.0.2 color page extends this same final world pass with
Original, Filmic, and Vibrant profiles followed by signed exposure, contrast,
and saturation adjustments. Original with zero adjustments is the identity
configuration. `LeftWorld` and `RightWorld` always use the scaler so saved
changes apply live; Ref2 receives neutral constants and separately composed
scope/menu/settings layers remain outside the pass. Death-camera comfort also
extends the established comfort-vignette layer rather than adding another:
the x86 alive-to-dead observer publishes protocol-v21 life state, and x64
blends a tighter muted dark-red profile for up to three seconds or until a
verified respawn while ordinary locomotion comfort continues updating behind
the same compositor.

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
