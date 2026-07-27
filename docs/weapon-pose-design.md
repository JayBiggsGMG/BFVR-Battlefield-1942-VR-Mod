# 6DOF weapon-presentation foundation

## Scope

This document starts BFVR's controller-held weapon work.  “Motion control” in
this slice means that a supported first-person weapon will visibly follow the
right tracked hand's translation and orientation in 3D.  It does **not** mean
that BFVR manufactures a projectile, changes a hit ray, or moves a player in
room scale.  BF1942 remains the sole owner of firing, recoil, reload, spread,
projectile spawning, hit detection, and network state.

The first active implementation will target the shared ordinary on-foot
infantry presentation family. One default weapon supplies the initial evidence
and a contrasting ordinary weapon validates the same generic route; BFVR will
not carry bespoke pose code or calibration per item. States with a genuinely
different presentation mode (vehicle, mounted weapon, death, spectator,
map/menu, scope, stale input, or lost tracking) must keep the original BF1942
view model until their own generic policy is proven.

## Evidence and external research

The version-5 x64-to-x86 presentation protocol carries both hands' `aim` and
`grip` position/orientation samples, controller values, separate pose-valid and
pose-tracked flags, focused-session state, the render-request sequence, and its
predicted display time. It also carries the runtime's `VIEW`-space head pose
located relative to `LOCAL` at that exact display time. The x86 bridge accepts
the hand data only when the request/focus/time checks match; weapon motion
requires active, valid, **and tracked** head and grip components. Thus a 6DOF
presentation layer never silently uses an inferred/last-known pose as precise
weapon tracking, while ordinary controller buttons and sticks remain available.

The OpenXR specification defines the distinction BFVR needs: a `grip` pose is
for reliably rendering an object held in the hand, while an `aim` pose is the
pointing reference. BFVR therefore uses the right **grip** pose for the visible
weapon and reserves **aim** for pointing/raycast tasks; neither pose is allowed
to become implicit native camera look. This makes a visible barrel follow the
held controller without silently substituting a different gameplay ray. See
the [OpenXR grip/aim definitions](https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html)
and [Microsoft's controller-pose guidance](https://learn.microsoft.com/en-us/windows/mixed-reality/design/motion-controllers).

The normal held-object hierarchy is also explicit in mainstream engines: the
rendered object is attached under the tracked motion-controller/grip transform,
so the attachment point is its rotation pivot. Unreal's
[motion-controller setup](https://dev.epicgames.com/documentation/en-us/unreal-engine/motion-controller-component-setup-in-unreal-engine)
and Unity's
[XR Grab Interactable attach transform](https://docs.unity3d.com/ja/Packages/com.unity.xr.interaction.toolkit%402.0/api/UnityEngine.XR.Interaction.Toolkit.XRGrabInteractable.html)
describe that same parent/attachment contract. BFVR implements its equivalent
with matrices because it is replaying a legacy D3D8 viewmodel rather than
parenting a native scene object.

This split also matches established open-source FPS VR practice.  QuestZDoom
assigns weapon orientation to the dominant controller and reserves the
off-hand grip for optional two-handed weapon stabilization; its documented
layout is a useful staged precedent, not a dependency for BFVR.  The GTFO VR
plugin independently exposes single- and double-handed aiming plus gun-stock
support, reinforcing that off-hand support belongs after one-hand calibration,
not before it.  Sources: [QuestZDoom](https://github.com/Team-Beef-Studios/QuestZDoom)
and [GTFO VR](https://github.com/DSprtn/GTFO_VR_Plugin).

For Battlefield 1942, the original Mod Development Toolkit distinguishes
carried `HandFireArms`, weapon configuration in `Weapons.con`, and vehicle or
stationary-gun `PlayerControlObject` hierarchies.  That confirms why the first
slice must exclude mounted weapons and vehicles instead of treating every
weapon draw as an infantry view model.  Sources: [MDT CON overview](https://www.realtimerendering.com/erich/bf1942/mdt/MDTDOC/Confiles/Intro.htm)
and [MDT hand-weapon tutorial](https://bfmods.com/mdt/Tutorials/How%20to%20Create%20New%20Weapons/How%20to%20Create%20New%20Weapons.html).

Static BF1942 evidence adds two hard constraints.  The semantic Mac corpus
shows `WeaponFireArm::getProjectileOrigin` reaches a `FireArms`-associated
position, while WinPC `FUN_0053CDB0` is the accepted weapon-fire core.  Their
exact local-object ownership and ray contract have not yet been recovered, so
the first view-model transform must never write them.  Also,
`ObjectTemplate.center1pHands` is a template placement value (the first-person
hand/weapon centre relative to `SoldierCameraPosition`), not a proven
per-frame/draw transform; BFVR must not use it as a shortcut.

## Implemented math boundary

`stereo/WeaponPoseMath` supplies the unit-tested math used by the opt-in live
weapon-motion path. It takes a reference HMD/grip pair and a current HMD/grip pair
located at their matching predicted display times, then:

1. Converts each grip into HMD-local coordinates, so shared physical HMD and
   controller movement does not move the view model.
2. Builds full reference and current grip transforms and derives the exact
   row-vector attachment delta `inverse(referenceGrip) * currentGrip`. The full
   matrix is essential: rotating a controller in place creates compensating
   translation that keeps the grip pivot fixed. A rotation-only delta would
   rotate the already-positioned weapon around the HMD/view origin.
3. Validates finite tracking and a bounded per-sample translation, and converts
   OpenXR metres/right-handed coordinates to BF1942/D3D8 units/left-handed
   coordinates.
4. Inserts the centre-HMD/source-view delta before the residual per-eye view:
   `World * sourceView * gripDelta * residualEye * eyeProjection`. The temporary
   adjusted World is consequently common to both eyes; applying the delta after
    the eye offset creates an eye-dependent pivot.

For that HMD reference, the runtime locates `XR_REFERENCE_SPACE_TYPE_VIEW`
relative to `LOCAL` at the same predicted display time as eyes and controller.
OpenXR defines that reference space as the viewer origin (the centroid of the
view origins for stereo), avoiding a guessed "left-eye orientation plus midpoint
position" head proxy. The D3D8 fallback uses a normalized shortest-path midpoint
of the two eye orientations only if the exact VIEW pose is unavailable. OpenXR
can mark a pose valid while tracking has been lost and it is inferred from
last-known data; BFVR therefore accepts `VALID` for rendering but requires both
`VALID` and `TRACKED` components for the precise head/grip motion path. See the
[OpenXR reference-space definition](https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html)
and [space-location flags](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrSpaceLocation.html).

The design deliberately preserves BF1942's original weapon anchor at
calibration: weapon-specific offset, handedness, and sight alignment are a
measured correction layered over the original view model, rather than a
guessed absolute location.  A recenter, tracking loss, focus loss, weapon
swap, or state change discards the reference and falls back to the untouched
original draw.

## Prepared draw-evidence capture

`BFVRLoader --weapon-viewmodel-probe` is now the isolated first capture step
for a controlled ordinary-infantry comparison. It waits for the already
verified direct system-D3D8 lifecycle and sustained local-infantry alive gate,
then gives an eight-second preparation window before forwarding every game
D3D8 call unchanged while recording 240 Present-to-Present frames.
For each stable draw signature it retains the original draw family, caller
return, first game-code return stack, topology/counts, shader/FVF,
stream/index/texture identities, selected render state, fixed-function
World/View/Projection submissions, and only the game-supplied vertex-constant
registers `c0..c3` when present. It logs raw World/View row 3 and projection
diagonal values solely to distinguish transform ownership in the next
comparison. If a busy scene reaches the bounded catalogue size, the probe
retains first-person-projection candidates preferentially by inspecting their
raw game-code return stack: the known AnimatedMesh boundary receives highest
priority and the generic fixed-function mesh boundary receives the next
priority. Every such replacement is counted in the completion report, so a
missing low-priority record is never silently treated as absent from the frame.

The `c0to3` values are candidates, not an assertion that they are a WVP or
weapon transform.  The probe neither reads D3D state/resources nor creates a
resource; it does not write a game object, controller state, camera,
input/action, D3D state, or rendering output.  It is intentionally exclusive
with every other graphics/input/OpenXR diagnostic.

`stereo/D3D8WeaponDrawPolicy` now expresses the current static-weapon
candidate as one shared, runtime-disabled rule: indexed generic-mesh route,
fixed-function FVF `0x112`, depth enabled, alpha disabled, a first-person FOV
projection, and a World origin within a conservative distance of the camera
derived from a rigid View matrix. It rejects animated, blended, distant,
non-FOV, missing-state, and non-rigid-view near misses. This classification is
still evidence plumbing only; it is neither a draw hook nor a transform
override.

In the direct-D3D8 validation, this rule selected exactly the five labelled
Colt bundle signatures (`52/36/436/20/321`) and rejected the other 379 retained
groups. A catalogue overflow in that busy scene means it is not a proof that
every unrecorded draw is unrelated, but it is positive evidence for the
classifier's narrow intended target.

## Bounded source-transform ownership test

`BFVRLoader --weapon-transform-ownership-probe` is a separate active
diagnostic, not the runtime motion-control path. It uses the same direct-D3D8
lifecycle, local-infantry gate, and eight-second preparation interval as the
read-only capture. For 180 Present-to-Present frames, it applies a fixed
`+0.25` rightward view-space delta through `ApplyViewSpaceWeaponDeltaToD3D8World`
only when the shared fixed-function classifier accepts the current indexed
draw. It calls the original D3D8 `SetTransform(World, adjusted)` immediately
before that draw and `SetTransform(World, original)` before the draw callback
returns.

It neither reads nor writes a BF1942 object, changes input/camera/gameplay,
creates a resource, or affects projectile/muzzle/network behavior. The log
counts accepted candidates, offset draws, failed offset sets, failed restores,
and rejected transform math. A clean run proves only the bounded transform
transaction; a human must confirm that the visible first-person weapon—not
unrelated scene content—moved. No controller pose is used in this test.

Run the test only after building the client:

```text
BFVRLoader --game-root <BF1942 root> --client <BFVRClient.dll> \
  --weapon-transform-ownership-probe --diagnostic-timeout-ms 160000
```

Spawn on foot and watch the weapon after the preparation interval. The offset
is intentionally temporary and should end automatically after roughly three
seconds at 60 Hz.

## Opt-in right-hand 6DOF presentation slice

The first live motion path is deliberately gated behind the continuous
translated OpenXR presentation run:

```text
BFVRLoader --game-root <BF1942 root> --client <BFVRClient.dll> \
  --d3d8to9-observer-probe --d3d8-openxr-presentation-probe \
  --weapon-motion-probe --run-until-stopped
```

`--weapon-motion-probe` causes the launcher to enable
`BFVR_ENABLE_WEAPON_MOTION=1` only for the BF1942 child process. The overlay
then receives the right controller's OpenXR **grip pose** and the exact
runtime `VIEW` head pose derived from the same accepted render request and
predicted display time. It does not inspect the controller squeeze or require
the user to hold a grip button.

On the first fresh focused grip sample, `WeaponMotionTracker` calibrates the
game's original weapon position in place. Later samples produce an HMD-relative
6DOF view-space delta; shared head-and-controller movement therefore does not
move the weapon. Missing focus, stale transport, a head/grip pose that is not
active-valid-tracked, predicted-time reversal, or a discontinuity larger than
0.50 metres between accepted samples resets the calibration and forwards the
original draw untouched. Continuous motion has no old 0.75 m reference-radius
wall. For an accepted delta,
the same proven generic fixed-function weapon rule is applied, BFVR replaces
only `World` for the original indexed draw, and restores the source `World`
before that callback returns.

The current `1.0` world-units-per-metre value follows the existing runtime
camera bridge; it is a provisional technical scale, not a final comfort
calibration. The live validation must still establish scale, handedness,
controller-to-grip/weapon offset, and barrel alignment. Normal weapon switches,
recoil, reloads, firing, projectiles, and native switching remain BF1942-owned.

After compiling BFVR, run it only with the intended direct-D3D8 control:

```text
BFVRLoader --game-root <BF1942 root> --client <BFVRClient.dll> \
  --weapon-viewmodel-probe --diagnostic-timeout-ms 160000
```

Load a local map, spawn on foot, and use the preparation window to select a
contrasting ordinary infantry weapon and begin a fire or reload transition.
Completion logs identify candidates; they do not authorize a transform
override. The next evidence pass compares stable candidates across baseline,
idle, fire, reload, and weapon-switch states to decide whether the shared
ordinary-infantry path is fixed-function, shader-constant, or
skeleton-controlled.

## Required next evidence before activation

1. Capture one default and one contrasting ordinary infantry weapon to identify
   their shared first-person draw, and determine whether its original transform
   is fixed-function, shader WVP, or a skeleton/bone constant. The current
   narrow `AnimatedMeshSkinning` correction is proven for arms/soldiers, not as
   a generic weapon classifier.
2. Capture the shared source transform, recoil/reload/weapon-change behavior,
   and the local controller/HMD reference at the same draw boundary. Match all
   identities before applying `WeaponPoseMath`.
3. Apply the 6DOF delta only to that shared presentation path, restore each D3D
   state byte-for-byte, and use the original draw whenever any condition is
   absent.
4. Compare the rendered barrel and controller direction against the normal
   BF1942 projectile/hit path at fixed ranges.  A visual muzzle-offset setting
   may correct presentation after measurement; it must not change the engine
   projectile origin.
5. Add the off hand only as a constrained support/stabilization pose after the
   one-hand path is stable.  Full-body IK, grabbing, and physical reloads are
   outside this slice.
