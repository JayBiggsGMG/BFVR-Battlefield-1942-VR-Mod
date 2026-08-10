# Infantry Authoritative Aim Refactor Plan

## Status

In progress. Phase 1 firearm authority and the Phase 2 independent presentation
boundary passed live validation. The first Phase 3 absolute controller was
live-rejected; its replacement passed the exercised one-hand, two-hand,
multiplayer-damage, and grenade-direction gates. Smooth Turn remains
live-rejected in the last headset run; its request-matched VR presentation
replacement is now staged for validation. This document defines an
infantry-only correction. It does not modify vehicle, aircraft, boat,
mounted-gun, stationary-weapon, or turret behavior.

Current proof baseline: BFVR commit
`7fc1a11207cc0555edbc5c0739145efefafa0e08` and preceding staged DLL SHA-256
`4F843A6D65DF49A79F927173D7EFBB40F0613C34D8145D928730CBB30515A737`.
The first refactor artifact is a 2,189,824-byte `BFVRClient.dll` with SHA-256
`BA73FBE62A866B956B73DB5262EACB7A169895A87CB81647D2EBFA123070E4C1`.
It builds cleanly, passes all 26 deterministic suites, and passes the loader
dry-run. The owner then confirmed an authoritative multiplayer hit marker and
kill on an unmodified server. This passes the firearm authority question, but
the artifact is not a release candidate.

Phase 2 presentation artifact: 2,190,848-byte `BFVRClient.dll`, SHA-256
`C894B3E2E1CEDAA53A0055EBC01047D8CA9420C5288500299D9548017914DC31`.
It passes all 27 deterministic suites and the loader dry-run. Headset
validation confirmed view independence, MP authoritative firing, and shared
hand framing. The owner reported substantial aim offset, which was the
temporary raw-pose proof limitation assigned to Phase 3.

A later repeat test clarified the actually exercised Phase 2 behavior: native
firing direction and scope firing were both badly offset from the visible
weapon pointer; two-hand operation worked; scope activation/tracking worked
mechanically; Smooth turn was extremely choppy and turning moved the elbows
oddly. Snap turn and throwing items were not tested in that run. The runtime
log proves that session still loaded the Phase 2 SHA-256 above, not Phase 3.

Rejected Phase 3 absolute-aim artifact: 2,174,976-byte `BFVRClient.dll`, SHA-256
`E792DB58BE2C5B56F0D02F9D54406089DD9690BF92EDCEE4A5BEFA0917804943`.
It builds cleanly, passes all 27 deterministic suites, and passes the bounded
loader validation. It replaces raw quaternion-delta steering with the final
functional weapon/scoped basis (or the established pointer basis for gadget
slots 4/5/6), plus proportional feedback from the native root, relative yaw,
and pitch. PID `25692` genuinely loaded this artifact. The owner found
intermittent large one-hand and grenade offsets, consistently large two-hand
offset, and severely choppy Smooth turn. Snap was not tested.

Deployment correction: the first requested Phase 3 headset run accidentally
loaded the old Phase 2 DLL from
`F:\Battlefield 1942\build\bfvr-cmake-validate\BFVRClient.dll`; only the BFVR
repository-root DLL had been replaced. The 18:58 runtime startup log explicitly
announced the old raw-orientation-delta proof and emitted no Phase 3 absolute
target/current diagnostics. Both launcher-facing and repository-root DLLs were
then given the Phase 3 SHA-256 above, enabling the later genuine test.

Replacement Phase 3 artifact: 2,175,488-byte `BFVRClient.dll`, SHA-256
`6875F6F1120F15E05B512F1055DB4ABD0C51C17F881FEF603CA0BE75A7EB63A3`.
Accepted-shot telemetry from PID `25692` showed the untouched
`WeaponFire_Core` forward and the pre-VR BF1942 source-camera forward agreeing
to about 0.14 degrees on the first correlated shot and remaining closely
coupled through the burst. This build therefore uses that exact, fresh,
soldier-scoped source direction as feedback instead of inferring authority from
root yaw and replicated relative-yaw/pitch fields. It also removes all direct
Smooth/Snap native-look feed-forward: artificial turn rotates presentation
once, and the absolute controller is the only writer of infantry native look.
The build passes all 27 suites and loader dry-run. Both staged DLL locations
have the hash above. PID `27192` then confirmed aligned aim in every exercised
case, multiplayer player damage, grenade throws exactly along the pointer, and
correct two-hand aim. Its first 24 accepted shots measured native-fire-to-final-
target error primarily between 0.05 and 0.64 degrees. Smooth remained visibly
stepped/choppy. Quest Link froze before Snap or scoped/sniper testing, so both
remain explicitly untested.

Request-cadence Smooth/Snap artifact: 2,177,536-byte `BFVRClient.dll`, SHA-256
`3D83E6DB4CC1648D760D1F07C9807F2F20B883052F92F4404573F7BD5A2E544D`.
The pure `InfantryPresentationTurn` module integrates the existing response
curve at 63 degrees/second for 100% speed using each new request's predicted
display time. It rejects non-positive time and gaps over 50 ms, suppresses turn
while Quick Menu owns the controller, resets across infantry lifetime/mode/
tracking changes, and emits one complete Snap edge after rearm. The lower-rate
PlayerAction overlay no longer produces either presentation turn. All 28 suites
and loader dry-run pass; both staged DLL locations have the hash above. Smooth,
Snap, scoped/sniper aim, and Link-freeze recovery need headset validation.

The owner then exercised the artifact in multiplayer followed by single-player.
Native aim/damage, two-hand aim, grenades and other throwables, Smooth camera
motion, and Snap all behaved correctly in both modes. The only reported turn
defect is now visual: the native first-person arms and attached gun are
choppy/jittery during otherwise-smooth artificial rotation. Runtime evidence
keeps this separate from authority. Presentation advances every 21--22 ms,
whereas native infantry PlayerAction/body updates remain about 30 Hz; a body
yaw can therefore change after the request thread rebases the controller but
before the skeleton callback combines it with the live soldier transform.

Same-callback arm-coherence artifact: 2,177,536-byte `BFVRClient.dll`, SHA-256
`39F2185013DE49412A3B829717EDB69609ADC2D21DE270794B4F134B1F3B6217`.
Each presentation-controller cache entry now carries the body yaw paired with
its rebase. The native arm callback reads the exact current soldier transform
and rigidly converts both hands from the cached body basis to that same-callback
basis before IK, item attachment, two-hand steering, or final gun publication.
Invalid/missing pairing retains the preceding fail-closed behavior. Camera,
PlayerAction, source-camera aim feedback, native fire, packets, and
non-infantry paths are unchanged. All 28 suites and loader dry-run pass; both
staged DLL locations have the hash above. The owner then confirmed that this
artifact removed the remaining arm/gun turn jitter: Smooth remains camera- and
viewmodel-smooth, with the previously accepted MP/SP aim, damage, two-hand,
throwable, and Snap behavior intact.

## Objective

Restore one authoritative infantry aiming and firing model for both single-
player and multiplayer while preserving BFVR's complete existing infantry
feature set.

The tracked weapon must drive Battlefield 1942's native soldier aim. Native
BF1942 firing must then create every bullet, grenade, TNT charge, mine, and
other infantry projectile or placed object. The HMD controls only the locally
rendered view and must never be rotated by tracked weapon aim.

Single-player and multiplayer must have identical BFVR behavior. There must be
no offline-only hand-fire transform, multiplayer-only origin policy, or mode-
specific controller aiming implementation.

## Correct Ownership Model

```text
HMD pose + artificial turn
        |
        v
BFVR presentation camera ----------------------> rendered eyes/monitor

Tracked gun pose + support steering + recoil
        |
        v
BF1942 native infantry look input
        |
        v
authoritative soldier aim
        |
        +--> native firearm firing --> bullet/damage/hit marker
        |
        +--> native gadget use -----> grenade/TNT/mine creation
```

The rendered gun remains the original BF1942 weapon posed by BFVR. The gameplay
gun remains the original BF1942 weapon fired through its native authority path.
They are kept aligned by making the final tracked gun direction the target of
native soldier aim, not by replacing `WeaponFire_Core`'s matrix.

## Non-Negotiable Requirements

- Infantry SP and MP use the same controller-to-native-aim implementation.
- Infantry SP and MP use native BF1942 projectile and damage authority.
- HMD position and orientation never drive firearm or gadget aim.
- Tracked weapon aim never rotate the locally presented HMD view.
- Right-stick Smooth and Snap turn remain available and visually immediate.
- Vehicles, turrets, mounted weapons, aircraft, boats, and their cameras and
  input mappings are outside this refactor and must remain byte-for-byte
  behaviorally unchanged.
- Remote soldiers may visibly follow the authoritative tracked weapon aim. No
  work is required to conceal or cosmetically alter that presentation.
- No packet fabrication, fire-time yaw spoof, server mod, protocol extension,
  or anti-cheat bypass is part of the design.
- No BFVR-added damage, hit detection, ammunition, cadence, spread, recoil,
  projectile, or gadget-placement logic is permitted.
- Existing unrelated user changes and BFVR features must be preserved.

## Historical Defect to Remove

The initial BFVR controller path sent right-hand orientation deltas through
native infantry `mouseLookX/Y`. This was authoritative but also moved the
native camera. Commit `ccd7154` removed the controller-to-native-look path and
introduced local `WeaponFire_Core` matrix replacement. That fixed the visible
camera coupling but disconnected the tracked gun from stock-server firearm
authority.

The current MP symptom is consistent with that split: local blood and hit
animation occur, but the server sends no hit marker and applies no lethal
damage. Coupled tanks and mounted weapons continue to work because their
tracked controls operate native authoritative axes.

This refactor corrects the architectural boundary: preserve native aim
authority and decouple only presentation.

## Scope

### Included

- Alive local infantry presentation camera.
- Infantry controller-to-native yaw and pitch.
- Infantry right-stick Smooth and Snap turn interaction with the presentation
  reference frame.
- Ordinary firearms, scoped firearms, automatic and semiautomatic weapons.
- Grenades, TNT, mines, and other stock infantry gadgets whose server creation
  depends on native soldier aim.
- One-hand aim, two-hand support steering, sidearms, recoil-composed weapon
  direction, weapon switching, reload, zoom, death, respawn, and transitions.
- Removal of gameplay matrix replacement from `WeaponAimOverlay` while
  retaining any independently valid observation, haptics, or diagnostics.
- Deterministic tests and live SP/MP validation.

### Excluded

- Every non-infantry control object.
- Changes to vehicle or mounted input classification.
- Changes to native projectile templates, damage, spread, cadence, ammunition,
  or network protocol.
- Server-side BFVR components.
- Remote-player cosmetic correction.
- A different SP implementation for convenience or enhanced muzzle placement.

## Feature-Parity Inventory

The refactor is incomplete until all existing infantry features remain intact:

- OpenXR stereo presentation and desktop mirror.
- HMD 6DOF view and recentering.
- Standing and seated calibration.
- Smooth turn and Snap turn.
- HMD- and controller-relative locomotion settings.
- Right-hand weapon pose and native first-person arms.
- Dynamic left/right hand IK and elbow policies.
- Two-handed primary support and close sidearm support.
- Native recoil transferred to the visible tracked weapon.
- Native spread, cadence, reload, ammunition, and weapon switching.
- Scope activation, per-eye scope view, support steering, and scope overlay.
- World crosshair policy.
- Controller haptics.
- Grenade, TNT, mine, binocular, medpack, wrench, and detonator input behavior.
- Death, respawn, deploy, focus loss, tracking loss, and map transitions.
- Infantry entry to and exit from every vehicle or mounted station.
- Quick Menu, Settings, scoreboard, map, and all controller shortcuts.

## Phase 0: Preserve and Instrument the Baseline

- [x] Record the current committed BFVR revision and staged DLL hash.
- [x] Add bounded diagnostics without changing behavior:
  - final tracked/recoil-composed gun forward;
  - native soldier yaw and pitch;
  - outgoing local `mouseLookX/Y` values;
  - fire/use button edges;
  - selected item and soldier lifetime;
  - native `WeaponFire_Core` forward;
  - authoritative hit-indicator network edges.
- [x] Identify the exact WinPC native infantry yaw and pitch state consumed by
  local prediction and mirrored in PlayerAction snapshots.
- [x] Confirm the final gameplay gun basis after one-hand aim, two-hand support,
  item alignment, and native recoil. This one basis becomes the authoritative
  target; raw HMD or raw controller aim must not bypass it.
- [x] Add explicit control-object guards proving the new path cannot execute
  for a vehicle, turret, mounted station, aircraft, or boat.

## Phase 1: Prove Native Infantry Authority

This is a diagnostic milestone, not a release candidate. It proves the server
contract before presentation-camera work begins.

- [x] Disable gameplay mutation in `WeaponAimOverlay` for both SP and MP in the
  authority-proof build. The hook retains haptics and accepted-shot recoil
  notification, then forwards every original matrix unchanged.
- [x] Restore the historical tracked right-aim orientation-delta conversion to
  native infantry `mouseLookX/Y` through one pure, bounded implementation used
  identically by the offline and multiplayer frame routes. This deliberately
  proves authority before the final absolute feedback controller exists.
- [x] Replace the proof's raw tracked-aim deltas with the final
  recoil/support-composed gameplay gun direction after presentation-camera
  separation supplies stable native-aim feedback.
- [x] Make no presentation-camera or body-catch-up change in this proof slice;
  any resulting native aim/camera coupling remains visible for diagnosis.
- [ ] Test an ordinary rifle and pistol in SP and on an unmodified public
  server. One tested multiplayer firearm has passed; the complete weapon matrix
  remains pending.
- [x] Require authoritative MP hit markers, damage, and death—not blood or
  animation alone. The owner confirmed both a native hit marker and a kill.
- [ ] Test grenade, TNT, and mine direction through their untouched native
  button/selection paths.
- [ ] Capture outgoing input, native aim, fire edge, and hit-indicator evidence.
- [ ] Stop if native hand-driven look does not restore authority; inspect the
  exact PlayerAction/fire ordering before modifying camera code.

Success gate: the same native hand-aim route works in SP and produces confirmed
server damage and native gadget direction in MP.

## Phase 2: Implement the Independent Infantry Presentation Camera

- [x] Introduce an infantry presentation-yaw state owned only by local VR
  presentation.
- [x] Compose the rendered camera from:
  - BF1942's native source position;
  - the stable BFVR presentation yaw;
  - current HMD orientation and translation;
  - standing/seated height and recenter state.
- [x] Stop using authoritative soldier/body yaw as the visible infantry camera
  heading.
- [x] Deterministically verify arbitrary native soldier yaw sequences do not
  change presentation yaw or the request-matched camera basis. Live headset
  validation remains pending.
- [x] Preserve native camera position changes required for movement, stance,
  death, spawn, ladders, parachutes, and transitions.
- [x] Keep invalid/unreadable state fail-closed without replaying stale camera
  state.
- [x] Make presentation state request-consistent across both stereo eyes.

### Retire the Current Body-Catch-Up Turn Mechanism

- [ ] Remove the controller-turn accumulator from `ControllerInputCache` once
  the replacement camera owns artificial turn directly.
- [x] Remove the bounded body-catch-up lead from `D3D8TrackingAnchor`.
  Authoritative body yaw is continuously canceled from the shared local
  HMD/controller presentation frame, not replayed into the camera.
- [x] Remove render-camera dependence on the current body heading.
- [x] Preserve the existing non-infantry tracking-anchor behavior unchanged.
- [x] Replace catch-up tests with presentation-camera invariance tests.

Success gate: moving the weapon through any yaw/pitch range cannot move the
local view, while native authoritative soldier aim follows the weapon.

Owner result: passed. Aim alignment itself remains pending Phase 3.

## Phase 3: Build the Authoritative Aim Controller

Create one infantry-only, pure-testable controller that converts an absolute
tracked gameplay gun direction into bounded native look-axis input.

### Inputs

- Final tracked/recoil-composed gun world forward.
- Current native authoritative infantry yaw and pitch derived from the exact
  fresh pre-VR source-camera forward.
- Exact local alive soldier/item lifetime.
- Frame timing and tracking/focus validity.
- Existing native logical frame values for conflict detection.

### Outputs

- Bounded native `mouseLookX` and `mouseLookY` contributions.
- Native mouse-look action enable state.
- Diagnostics only; no body/world transform writes.

### Required Behavior

- [x] Convert the final gun world forward into target native yaw and pitch.
- [x] Compute shortest signed yaw error and bounded pitch error.
- [x] Convert error into native logical look axes using one calibration for SP
  and MP.
- [x] Apply MP-safe `[-1,+1]` axis bounds in both SP and MP so behavior cannot
  diverge by route.
- [x] Use current authoritative feedback every accepted local frame; never
  integrate an unbounded shadow heading. The rejected root/relative-yaw
  reconstruction has been replaced by the source-camera direction directly
  correlated with unchanged `WeaponFire_Core` at accepted shots.
- [x] Define deterministic proportional convergence without an accumulated
  shadow heading. Live calibration still must exclude oscillation, overshoot,
  and the
  regular body-catch-up hitch pattern.
- [x] Do not add a trigger delay. Aim is synchronized continuously rather
  than snapped only when firing.
- [x] On focus/tracking loss, stop BFVR aim contribution and allow native input
  to continue without a stale correction burst.
- [x] On soldier/item/control-object change, discard the old target and
  feedback state atomically.
- [x] Prevent simultaneous artificial-turn injection and aim-controller
  injection from double-applying yaw.
- [x] Preserve keyboard/mouse fallback when controller tracking is unavailable.

Phase 3 replacement implementation gate: complete in source and deterministic
validation; the exercised ordinary firearm, two-hand, MP damage, and grenade
direction paths passed live validation. Scope/Snap and the broader matrix remain
pending. Bounded diagnostics
record the first 48 target/current/error samples and the first 24 accepted-shot
native-fire/target/source-camera angular comparisons.

## Phase 4: Rebase Smooth and Snap Turn onto Presentation

The first presentation-only artifact still generated Smooth deltas in the
roughly 30 Hz BF1942 PlayerAction route while PID `27192` presented stereo
frames every 21--24 ms. Repeated rendered frames followed by fixed 2.1-degree
jumps explain the remaining step pattern. Smooth must be integrated from the
request-matched OpenXR thumbstick and predicted display time immediately before
the tracking anchor. This changes presentation timing only; native aim/fire and
all non-infantry controls remain untouched.

This time-based boundary follows OpenXR's frame-loop contract: `xrWaitFrame`
provides a monotonically increasing predicted display time, and the
specification recommends advancing engine motion at that exact time to avoid
motion judder. See the official OpenXR 1.1 `xrWaitFrame` specification:
https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrWaitFrame.html

- [x] Smooth turn rotates the local presentation reference frame directly.
- [x] Snap turn rotates the local presentation reference frame by the complete
  configured angle on its accepted edge.
- [x] Apply the same presentation rotation to HMD-relative controllers, hands,
  arms, weapon, support relation, crosshair, and locomotion references.
- [x] Let the authoritative aim controller naturally converge soldier aim to
  the newly rotated final gun direction.
- [x] Do not separately submit the same artificial-turn delta to infantry
  native look; that would double the authoritative rotation.
- [x] Integrate Smooth velocity at each new VR presentation request with
  predicted-time scaling and bounded discontinuity handling; remove its
  PlayerAction-rate producer.
- [x] Verify Smooth camera quality and repeating Snap in both MP and SP.
- [x] Verify no camera-side periodic hitch or duplicate artificial turn.
- [x] Pair controller rebasing with the live soldier transform in the same
  skeleton callback for both hands.
- [x] Verify the arm/gun-only turn jitter is removed, then revalidate one-hand,
  two-hand, throwable, Smooth, and Snap alignment
  after that visual-coherence change.

## Phase 5: Restore Native Fire and Gadget Ownership Everywhere

- [ ] Remove all SP/MP gameplay direction and origin replacement from
  `WeaponAimOverlay`.
- [x] Forward the native `WeaponFire_Core` matrix unchanged in every mode in
  the current authority-proof build.
- [x] Retain `WeaponAimOverlay` only for passive diagnostics, haptic
  notification, or other behavior that cannot affect gameplay transforms.
- [ ] Use the same native trigger, alt-fire, reload, zoom, selection, hold, and
  release actions in SP and MP.
- [ ] Confirm the server and offline simulation both derive firearm and gadget
  direction from the same authoritative soldier aim.
- [ ] Keep tracked muzzle translation visual-only in every mode.
- [ ] Remove stale route-specific fire-origin documentation and tests.

Success gate: disabling BFVR rendering would leave the same native authoritative
aim, fire, projectile, gadget, and damage behavior running underneath.

## Phase 6: Reconcile Weapon Presentation with Authority

- [x] Keep the final rendered gun direction equal to the target sent to the
  authoritative aim controller.
- [x] Ensure native recoil is included in that target so continuous tracking
  does not silently cancel gameplay recoil.
- [ ] Preserve server/native spread independently of visual alignment.
- [x] Verify one-hand and two-hand directions use the identical authority basis
  for the exercised multiplayer firearm.
- [ ] Preserve close-sidearm support as visual-only unless it already changes
  the final gameplay gun direction by established policy.
- [ ] Keep per-weapon/barrel native muzzle offsets authoritative even when the
  visual muzzle is elsewhere.
- [ ] Verify scoped and unscoped firing use the same authority model.
- [ ] Ensure scope camera presentation follows the tracked gun while native
  zoom/state remains game-owned.

## Phase 7: Lifecycle and Transition Hardening

- [ ] Tracking/focus loss and reacquisition.
- [ ] Death, respawn, redeploy, and team/kit changes.
- [ ] Weapon selection, reload, scope entry/exit, and item destruction.
- [ ] Standing/seated changes and recenter.
- [ ] Map load/unload and D3D reset.
- [ ] Infantry to vehicle/mounted transition: remove all infantry aim and
  presentation ownership before the non-infantry controller frame executes.
- [ ] Vehicle/mounted to infantry transition: capture a new infantry camera and
  aim lifetime without changing the vehicle/mounted implementation.
- [ ] Ensure no pre-transition target, fire edge, or camera pose can replay.

## Deterministic Test Requirements

- [x] Absolute gun-forward to native yaw/pitch conversion.
- [ ] Yaw wrap at `-pi/+pi` and vertical pitch limits.
- [ ] Bounded convergence at several frame rates.
- [x] Identical SP/MP axis output for identical input state.
- [x] No mode-route branch in the authoritative aim controller.
- [ ] No output for non-infantry control objects.
- [ ] No output for remote, dead, stale, unfocused, or untracked state.
- [x] Tracking loss/reacquisition without correction bursts.
- [x] Soldier/item lifetime replacement.
- [x] HMD camera invariant under arbitrary authoritative yaw/pitch sequences.
- [x] Smooth and Snap presentation rotation without double application.
- [ ] Recoil-composed target is not automatically neutralized.
- [ ] One-hand/two-hand/scope target consistency.
- [ ] Fire transform forwarding remains byte-for-byte unchanged.
- [x] Existing full BFVR test suite remains green (28/28).

## Live Validation Matrix

### Single-Player

- [ ] Rifle, pistol, automatic weapon, scoped weapon.
- [ ] One-hand and two-hand support.
- [ ] Recoil, spread, reload, weapon switching, and zoom.
- [ ] Grenade, TNT, mine, and other stock gadgets.
- [ ] Smooth and Snap while stationary, moving, firing, and scoped.
- [ ] Death/respawn and infantry/vehicle transitions.

### Multiplayer on an Unmodified Server

- [x] Visible impact plus authoritative hit marker.
- [x] Confirmed server damage and kill with the authority-proof firearm route.
- [ ] Rifle, pistol, automatic weapon, and scoped weapon.
- [ ] Near and distant targets at several angles from HMD forward.
- [x] Grenade trajectory follows the aimed pointer through native MP authority.
  Server-created explosion/damage-position confirmation remains implicit rather
  than separately instrumented.
- [ ] TNT/mine placement and server interaction.
- [ ] Movement, Smooth, Snap, recoil, reload, weapon switching, and zoom.
- [ ] Moderate latency and packet loss.
- [ ] A second player observes authoritative body/weapon direction; visual body
  following the hand is accepted and is not a failure.

### Non-Infantry Regression

- [ ] Run existing vehicle/mounted regression tests only to prove no change.
- [ ] Confirm tank, stationary MG, AA, troop-carrier MG, aircraft, boat, and
  other station controls still behave exactly as the committed baseline.
- [ ] Any non-infantry behavior difference blocks completion.

## Implementation Boundaries

Likely code ownership changes:

- `ControllerInputOverlay`: infantry aim-controller output and input arbitration.
- `ControllerInputCache`: remove old artificial-turn/body-catch-up publication;
  retain only cache responsibilities still required by final architecture.
- `D3D8TrackingAnchor`: presentation-frame rotation without infantry body-yaw
  catch-up; preserve all non-infantry anchors.
- `D3D8RenderViewPoseHook`: presentation camera independent of soldier aim.
- `WeaponAimOverlay`: retire gameplay fire-matrix mutation.
- `BFSoldierNativeArmIk`, recoil, support, and scope caches: publish one final
  gameplay gun basis without acquiring network or camera authority themselves.
- New focused modules should be introduced for authoritative infantry aim and
  presentation-camera state rather than expanding already large files.

No created or expanded code file may exceed the repository's approximate
2,500-line modularity limit.

## Commit and Rollback Strategy

Each phase must be independently reviewable and reversible:

1. Diagnostics and authority proof.
2. Presentation-camera separation.
3. Authoritative aim controller.
4. Smooth/Snap rebasing.
5. Native fire/gadget forwarding.
6. Feature-parity integration.
7. Lifecycle hardening and final validation.

Do not combine an unproven authority change with camera, recoil, scope, and
gadget changes in one test artifact. Record every rejected experiment and
restore the preceding known artifact before proceeding.

## Completion Criteria

The refactor is complete only when all of the following are true:

- The tracked infantry weapon drives native BF1942 authoritative yaw/pitch.
- HMD view remains independent from tracked weapon aim.
- SP and MP execute the same BFVR infantry aim and fire behavior.
- MP bullets produce authoritative hit markers, damage, and kills.
- MP grenades, TNT, and mines use the tracked weapon/pointer direction through
  native server-recognized aim.
- BFVR no longer mutates gameplay fire matrices in either SP or MP.
- Smooth and Snap are immediate and free of body-catch-up replay.
- Every listed infantry feature retains parity.
- Every non-infantry system remains unchanged.
- Deterministic tests, clean x86 builds, loader dry-run, headset validation,
  and public-server validation all pass.
