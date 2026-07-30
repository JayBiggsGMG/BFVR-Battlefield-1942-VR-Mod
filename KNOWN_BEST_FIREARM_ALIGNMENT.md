# KNOWN-BEST FIREARM AIM AND ALIGNMENT BASELINE

## Do not lose this implementation

The commit containing this document is the best validated BFVR firearm
hand/controller/gun/aim alignment system achieved as of 2026-07-29.

Permanent Git tag:

`bfvr-known-best-firearm-alignment-2026-07-29`

If later weapon, IK, recoil, camera, or controller work breaks firearm
alignment, compare against or restore this exact tagged commit before trying
new calibration constants.

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

Primary implementation:

- `src/client/BFSoldierNativeArmIk.cpp`
- `src/client/WeaponAimOverlay.cpp`
- `src/client/WeaponPoseRuntimeCache.cpp`
- `src/stereo/WeaponFireAimMath.cpp`

Research and evidence:

- `docs/first-person-arms-research.md`

## Deliberately outside this baseline

- Knives and grenades still have twisted-wrist presentation and require their
  own later item-class solution.
- Native firearm recoil is not yet a validated part of this alignment system.
  Do not sacrifice pointer-accurate aim or reintroduce unbounded accumulated
  recoil merely to make recoil visible.
- Left-hand/two-handed weapon IK remains later work.

## Restoration

To inspect this known-good state:

```text
git show bfvr-known-best-firearm-alignment-2026-07-29
```

To create a recovery branch without disturbing current work:

```text
git switch -c recover-known-best-firearm-alignment \
  bfvr-known-best-firearm-alignment-2026-07-29
```

The baseline was built successfully as an x86 `BFVRClient`, and
`BFVRStereoMathTests` plus `BFVRWeaponFireAimMathTests` passed before the
headset validation.
