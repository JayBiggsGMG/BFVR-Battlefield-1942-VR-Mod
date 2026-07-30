# KNOWN-BEST FIREARM AND FIRST-PERSON ARM IK BASELINE

## Do not lose this implementation

The commit containing this document is the best validated BFVR firearm,
controller, two-hand support, and first-person arm IK implementation achieved
as of 2026-07-30.

Permanent Git tag:

`bfvr-known-best-first-person-arms-2026-07-30`

If later weapon, IK, recoil, camera, or controller work breaks firearm
alignment, compare against or restore this exact tagged commit before trying
new calibration constants.

The earlier firearm-only recovery point remains available as
`bfvr-known-best-firearm-alignment-2026-07-29`.

## Headset-validated behavior

The project owner directly confirmed all of the following in Quest/OpenXR
headset testing:

- Rifle and pistol hand positions are correct.
- Rifle and pistol wrist, gun, and barrel angles are correct.
- The gun follows the controller freely without a world/ground tether.
- OpenXR right-hand aim owns the gun direction and shots follow that pointer.
- Rifle/pistol switching does not reuse the other weapon's wrist relation.
- The correct relationship is available before firing; no first-shot
  calibration is required.
- Death, respawn, match-start cameras, and cinematic camera angles do not
  create alignment state.
- Crouching and going prone lower the hand, firearm, and muzzle with the
  player; returning to standing restores their height.
- The free left hand follows tracked position and relative wrist rotation.
- Squeezing near a rifle acquires its authored support span; left-hand movement
  then provides bounded two-hand steering around the fixed right grip while
  rendered barrel direction and projectile aim retain one shared basis.
- Close pistol/cupped support remains permanently visual-only.
- The explicit Maya pole-vector implementation is the new best arm baseline:
  the owner reports that pistol and rifle arms are generally good.
- PID 31372 recorded exactly zero final-hand target error for all first twelve
  pole-adjusted solves, while rifle and pistol support continued to acquire
  and release across several soldier lifetimes.

## Architecture that produced the validated result

Preserve these invariants:

1. OpenXR grip position is the physical held-object position.
2. OpenXR aim orientation is the authoritative firearm and projectile
   direction.
3. BF1942's currently selected item supplies its own authored
   `handFromFire` relation through the active-item AnimatedBundle attachment
   callback before controller IK is enabled.
4. That relation is scoped to the current item and soldier lifetime. It is
   never process-global and never copied from rifle to pistol.
5. BF1942's row-vector order is:
   `handFromFire = nativeHandWorld * inverse(nativeFireWorld)`, followed by
   `targetHandWorld = handFromFire * controllerGunWorld`.
6. Crouch/prone changes translation only:
   `stanceDelta = poseCameraPosition(currentPose) -
   poseCameraPosition(standing)`. Apply it in soldier-local space before the
   soldier world transform.
7. Spawn cameras, death cameras, controller/menu alignment poses, first shots,
   and accumulated legacy recoil must not redefine the firearm basis.
8. Elbow bend changes only Maya's explicit zero pole argument for the exact
   current BFVR-owned 1P hand target. Shoulder, animated elbow, wrist, target,
   and both output pointers remain native and unchanged.
9. Right primary slot 3 retains its already-good authored rifle pole. Left and
   non-primary right arms use a finite, mirrored direction-only pole with
   singularity fallbacks. No third-person chest, shoulder, pelvis, or limb
   position participates.

Primary implementation:

- `src/client/BFSoldierNativeArmIk.cpp`
- `src/client/BFSoldierNativeArmPole.cpp`
- `src/client/WeaponAimOverlay.cpp`
- `src/client/WeaponPoseRuntimeCache.cpp`
- `src/stereo/ArmPoleVectorMath.cpp`
- `src/stereo/WeaponFireAimMath.cpp`

Research and evidence:

- `docs/first-person-arms-research.md`

## Deliberately outside this baseline

- The owner accepted pistol and rifle arms as generally good, not universally
  perfect. Knife, grenade, gadget, every stance/locomotion edge, and arbitrary
  mod/faction arm rigs still require separate validation.
- Native firearm recoil is not yet a validated part of this alignment system.
  Do not sacrifice pointer-accurate aim or reintroduce unbounded accumulated
  recoil merely to make recoil visible.
- BF1942's authored pistol-cup animation state/timing remains unidentified;
  the accepted captured-close fallback stays permanently visual-only.

## Restoration

To inspect this known-good state:

```text
git show bfvr-known-best-first-person-arms-2026-07-30
```

To create a recovery branch without disturbing current work:

```text
git switch -c recover-known-best-first-person-arms \
  bfvr-known-best-first-person-arms-2026-07-30
```

The headset-validated x86 `BFVRClient.dll` has SHA-256
`CD21A3AA5A07C212A87C9662871D5ED9B8AB47CC28CAAECCF0102FE9530CCCF6`.
`BFVRStereoMathTests`, `BFVRWeaponFireAimMathTests`,
`BFVROffHandSupportPolicyTests`, and `BFVRArmPoleVectorMathTests` all passed
before the headset validation.
