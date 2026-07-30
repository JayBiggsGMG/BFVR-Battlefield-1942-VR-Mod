# BFVR first-person native-arms research

Status: active evidence dossier (2026-07-29)

Implementation note (2026-07-29): BFVR now enables the Level-A renderer-only
native-arm path by default. `BFVR_NATIVE_1P_ARMS=0` opts out. It bypasses the
existing replay suppression only for the already fail-closed native first-person
`AnimatedMeshSkinning` draw signature, so the game continues selecting its own
faction/class/mod arms and animations. This is not controller-driven IK, a
custom mesh/hand implementation, full-body
awareness, or a gameplay change; it supplies a bounded native-arm visual test
while the separate live skeleton/ownership investigation remains open.

The owner has confirmed that the native arms are now visible. That establishes
only that the renderer suppression was removed; it is not evidence of a live
object owner, wrist transform, controller target, or motion-control route.

## Scope and non-negotiable constraints

- No third-person full-body-awareness system.
- No manual reloads, physical magazine handling, or other replacement of native
  weapon-state gameplay. Reload, throw, swap, fire, and networking remain
  game-authoritative and use their existing animations.
- Use BF1942's native first-person floating arms when possible: its existing
  mesh, hands, skinning, and authored animation poses.
- Do not create or import bespoke hand/arm assets.
- Do not hard-code a particular faction, kit, or weapon's arms. The game or
  installed mod must remain the source of the active first-person arms.
- Keep gameplay-authoritative firing, projectile origin, reload, and network
  state native unless separately proven safe to change.

## What a credible native-arm solution needs

| Need | Why it matters | Current state |
| --- | --- | --- |
| Active 1P arm mesh and skeleton owner | Lets BFVR affect the arms currently chosen by the game, not an asset chosen by BFVR. | Unknown on WinPC. |
| Faction/kit/mod asset-selection chain | Preserves the currently equipped native visual set. | Confirmed directly on WinPC: `FUN_004F7F20` walks game-owned direct children and shows the index-matched `SoldierPartInfo` records marked `1` in first person. Live pointer discovery/lifetime still needs observation. |
| Bone hierarchy, especially upper arm, forearm, wrist/hand | A controller target needs a known native end effector and a bounded arm chain. | All 12 currently audited stock/GCMOD soldier templates declare `animations/UsSoldier.ske`; its hand -> forearm -> upper arm -> clavicle -> neck -> spine ancestry is parsed. WinPC template init resolves `Bip01 R Hand` to `+0x33C`; live 1P ownership remains unknown. |
| Native weapon-to-hand attachment relationship | Prevents floating or mismatched weapon/hand placement. | The native right-hand matrix is delivered to the game-selected active-item child; its concrete type and the 1P wrist/left-hand details remain unknown. |
| Animation timing/lifetime | Reload, throw, fire, swap, death, and respawn must retain their native paths. | Unknown. |
| WinPC animation and IK update boundary | Required before any game-side arm target/control experiment. | `FUN_004FB150` exactly maps to `BFSoldier::updateAnimations`; `FUN_0054F390` exactly maps to `AnimatedBundle::updateIk`, which calls `FUN_006123B0` (`Skeleton::applyIk`). Safe local-1P ownership/timing remain unknown. |
| Renderer identification | Needed to restore only the intended native arms after their current VR suppression. | Confirmed for the narrow skinned draw family, not mesh ownership. |

## Confirmed local evidence

- BFVR can identify the exact `AnimatedMeshSkinning` draw family. The WinPC
  structural draw boundary is `0x005AEEC0`, observed draw return is
  `0x005AF40F`, and the relevant programmable shader is
  `SkinningShader2Bones`.
- That shader's WVP uses `c0..c3`; the bone palette begins at `c9`.
- The existing VR presentation currently suppresses only the profiled
  first-person subset, while retaining ordinary remote-soldier correction.
- The Mac client has named `Skeleton::applyIk` and
  `Skeleton::applyIK2BoneSolver` routines. `applyIk` stores a per-bone target
  position and transform in a skeleton-owned IK handle. `AnimatedMesh::setSkeleton`
  binds skeletons to skinned meshes. These are static Mac facts only.
- The Mac client also has a native `HandFireArms` object/template family. It
  derives from `FireArms`; its template creates a `0x2D4`-byte instance and
  exposes native `ObjectTemplate.itemIndex`/`itemType` metadata. BF1942's
  modding tutorial confirms this is the game-owned *hand-weapon* type. It is a
  useful weapon/animation/attachment lead, but not evidence that it owns the
  visible arm mesh, skeleton pointer, faction selection, or a WinPC offset.
- The Mac `BFSoldierTemplate` owns a skeleton and resolves `Bip01 Spine`,
  `Bip01 Head`, and `Bip01 R Hand` by name. Its `bindToSkeletonPart` path
  builds child-to-bone records, while live `BFSoldier::addPart` resolves a
  game-owned child object against the soldier skeleton; eligible kit parts use
  that route. This is the strongest current evidence that faction/mod-safe arm
  discovery should follow native soldier/kit child bindings rather than a
  BFVR-side faction-to-arm lookup table.
- Read-only parsing of representative stock skeleton entries,
  `animations/UsSoldier.ske` and `animations/JapSoldier.ske`, confirms both
  the native biped arm vocabulary and its parent links. `UsSoldier` (67 bones)
  has `R Hand[44] -> R Forearm[43] -> R UpperArm[42] -> R Clavicle[41] ->
  Neck[15] -> Spine3[14]`, with the matching left chain ending in
  `L Hand[21]`; `JapSoldier` (60 bones) has the same chain pattern ending in
  `R Hand[31]` / `L Hand[12]`. These two stock skeletons therefore supply
  concrete native two-bone arm chains and wrist/hand end effectors. Their live
  1P owner, bone transforms, mod coverage, and safe runtime lifetime still
  require passive proof.
- The same `.ske` parse supplies the static transform translation column in
  BF1942's documented left-handed axes (`x` right, `y` up, `z` away). The two
  stock skeletons are similar but not interchangeable: the upper-arm/forearm/
  hand translation magnitudes are `0.1377/0.3003/0.3050` for the US right arm
  and `0.1302/0.2837/0.2882` for the separate `JapSoldier.ske` right arm (file
  units). However, the installed Japanese template itself declares
  `animations/UsSoldier.ske`; the alternate Japanese skeleton is not selected
  by the currently audited templates. These are asset bind-data measurements,
  not live wrist positions. The current installation shares the US skeleton
  across its soldier templates, but BFVR must still discover the active native
  skeleton instead of assuming every future mod follows that convention.
- A direct config audit covers all eight installed stock `BFSoldier` templates
  plus GCMOD Endor, Fleet, Scout, and MonTrooper. Each BFSoldier declares
  `animations/UsSoldier.ske` (case differs only in text); face children use
  `UsFace.ske` where present. In representative US, Japanese, and hands-only
  MonTrooper configs, the marked 1P rBody/hand presentation parts are
  `SimpleObject` geometry; in this project `rBody` is the arm mesh, not a torso.
  do not declare a separate skeleton. Thus the selected native parts vary by
  faction/mod, while the current root soldier skeleton is shared; no live mesh
  or parent-skeleton ownership is claimed yet.
- The exact WinPC cross-build match is now known: `FUN_004F5D50` is
  `BFSoldierTemplate::init`. It resolves the same named bones and stores the
  right-hand index at template `+0x33C` (spine `+0x338`, head `+0x340`). This
  is a passive-observation lead only—not a safe live pointer, mesh identity,
  or a write target.
- WinPC `FUN_004FF440` is the exact `BFSoldier` constructor match. It clones
  the game-selected template skeleton twice, storing them at soldier `+0x298`
  and `+0x29C`, then applies the template's child-to-bone bindings. The source
  template skeleton is at `+0x334`; template/runtime binding vectors begin at
  `+0x37C`/`+0x2A4`. This proves a native per-soldier skeleton and attachment
  path that follows the selected template. The two skeleton roles are still
  unknown, so this is not permission to read, modify, or label either one as
  the local first-person arms.
- The semantic Mac counterpart resolves the constructor's immediate geometry
  link. While it traverses each created soldier child, a
  `SkeletonCollisionMesh` child receives the second clone; otherwise a child
  exposing the `AnimatedMesh` interface receives the first clone through the
  native `AnimatedMesh::setSkeleton` call. That setter keeps a compatible
  Skeleton and builds the mesh's skin-to-bone matching data. The same
  child-interface branch and clone ordering are present in WinPC
  `FUN_004FF440`. This is strong static evidence that a skinned native soldier
  part follows the soldier Skeleton rather than requiring a separately
  declared per-hand Skeleton. It still does not prove which runtime
  `SimpleObject` child exposes that interface for a given faction/mod, identify
  the local player, or establish a safe read/write point.
- The `SimpleObject` route is now directly connected to that branch. Its
  template stores the configured geometry string; its constructor resolves the
  named geometry template, creates the geometry instance, and registers it as
  the child object's geometry component. The generic
  `BObject<IPlayerObject>::queryComponent` implementation retrieves that exact
  component category and asks it for the requested interface. The semantic
  soldier constructor calls precisely this child-component query for
  `SkeletonCollisionMesh` first and `AnimatedMesh` second. Consequently, a
  configured 1P `SimpleObject` that instantiates as `AnimatedMesh` is directly
  bound to the game-selected soldier Skeleton during construction—not by a
  BFVR asset lookup. The current BFVR renderer already observes the relevant
  1P draw family as `AnimatedMeshSkinning`; untested geometry types/mods still
  require the fail-closed fallback.
- WinPC `FUN_004C9740` registers the native template command
  `setIsFirstPersonPart`, and `FUN_00502DF0` is its matched setter: it writes
  the configured integer to the active `SoldierPartInfo` record. The exact
  `FUN_00502500` getter maintains the template's 8-byte records at
  `+0x36C..+0x374` (marker plus LOD value). The exact WinPC
  `BFSoldier::setFirstPerson` match is `FUN_004FAE80`; it calls
  `FUN_004F7F20` (`updateVisibleSets`). That pass follows each direct child's
  `+0x54` sibling link and maps traversal ordinal `n` to the template record at
  `+0x36C + n * 8`; in normal first person, it shows marker `1` parts;
  the camera marker `2` follows a separate path. This directly proves the
  native, game-selected arms set. A live soldier pointer, mesh/skeleton owner,
  and lifetime still need passive proof before BFVR can use it.
- `BFSoldier::updateAnimations` is now an exact Mac-to-WinPC cross-build match:
  Mac `0x000CE644`, WinPC `FUN_004FB150`. It advances the native animation state
  machines against soldier `+0x298` Skeleton, transforms that skeleton, then
  gives the resolved template `Bip01 R Hand` matrix (`+0x33C * 0xE8 + 0x48`) to
  the game-selected active-item child (the matched active-item index is soldier
  `+0x3E8`) and iterates child-to-bone attachments. This establishes native
  animation-before-hand-attachment ordering on Windows, but does not type that
  child as a particular weapon, identify the local 1P arms, or authorize a
  runtime hook.
- The same WinPC update resolves the ordinary runtime child-binding record
  layout: soldier `+0x2A4..+0x2A8` is a contiguous 16-byte vector whose entries
  carry a game-owned child receiver at `+4`, root bone index at `+8`, and
  bone-span count at `+0xC`; receiver flags at its `+4` select the attachment
  callback route. This supports a bounded read-only topology observer from an
  already-proven local `BFSoldier*`; it neither identifies a first-person arm
  entry nor permits writes/calls.
- `AnimatedBundleTemplate::addSkeletonIK` is an exact cross-build mapping:
  Mac `0x000C8BC4`, WinPC `FUN_00551570`. It canonicalizes a named bone and
  records a target/rotation `SkeletonIkInfo` entry in the WinPC template
  `+0x208..+0x20C` vector. This confirms data-driven named-bone IK metadata;
  it is not evidence that BF1942's 1P arms use it.
- `AnimatedBundle::updateIk` is also exact: Mac `0x000C5874`, WinPC
  `FUN_0054F390`. It iterates those 0x58-byte entries, resolves named bones on
  the active soldier Skeleton, converts the configured target into skeleton
  space, and calls WinPC `FUN_006123B0` (`Skeleton::applyIk`). The latter and
  `FUN_00611220` (`Skeleton::getBoneIndex`) are exact Mac-to-WinPC matches.
  1P-arm participation, live ownership, and a safe experiment boundary are
  still unproven, so BFVR must not call the solver directly.
- The native target is consumed in the ordinary soldier animation transform:
  WinPC `FUN_00611690` exactly maps to `Skeleton::transform` and invokes
  `FUN_006114B0`, the exact Windows `Skeleton::applyIK2BoneSolver`, for each
  active target handle. Thus native animation, two-bone solve, final hand
  matrix, and skinned rendering remain one engine-owned path. This establishes
  timing, not permission to inject controller targets.
- The native object hierarchy now closes an important gap: `HandFireArms`
  derives from `FireArms`, which derives from `AnimatedBundle`; their template
  hierarchy similarly reaches `AnimatedBundleTemplate`. Moreover,
  `AnimatedBundle::handleVisualUpdate` calls `updateIk` and then native
  animation update. Thus `HandFireArms` is not the arm mesh, but it is a
  game-owned weapon/animation/IK bridge capable of driving an active soldier
  Skeleton through its authored `addSkeletonIK` metadata. Whether installed 1P
  weapons actually configure that metadata remains to be observed.
- `AnimatedBundle::AnimatedBundle` is now also an exact Mac-to-WinPC match:
  Mac `0x000C4770`, WinPC `FUN_005508F0`. The WinPC template’s `SkeletonIkInfo`
  vector is at `+0x208..+0x20C` (the Mac counterpart is `+0x194..+0x198`), and
  the instance owns its copied bundle Skeleton at `+0x12C`. These are passive
  trace landmarks only, not a claim that the bundle Skeleton is the 1P arm
  Skeleton or permission to inspect/write a live object.

## What flat-to-VR mods generally do

There are two stages, which should not be conflated:

1. **Held-object alignment:** attach the native weapon or its presentation
   transform to the controller's grip pose, applying a per-weapon authored
   offset. This is the common, lower-risk first step. OpenXR distinguishes the
   grip pose used for held objects from the aim pose used for pointing
   ([specification](https://registry.khronos.org/OpenXR/specs/1.0-khr/html/xrspec.html));
   UEVR documents the corresponding native-mesh/controller calibration flow
   ([guide](https://docs.uevr.io/usage/adding_6dof.html)).
2. **Arm solving:** obtain the game's existing arm skeleton, choose wrist/hand
   targets derived from the held object/controllers, and solve the arm chain.
   FRIK is an example of a mature version: it reuses native first-person hand
   nodes and weapon offsets, then drives a constrained shoulder-elbow-wrist
   solve ([source](https://raw.githubusercontent.com/rollingrock/Fallout-4-VR-Body/main/src/skeleton/Skeleton.cpp)).

The FRIK source is useful chiefly as a warning about scope. Its arm solver has
to find named first-person hand nodes, normalize separate native weapon-offset
nodes, reject unreachable targets, handle extension, calculate an elbow plane,
align wrist twist, and smooth instability. BF1942's native two-bone solver
removes the need to recreate that math only if a safe native target path is
first established. It is not evidence that simply feeding controller poses to
an arbitrary bone will produce stable arms.

BFVR must reach stage 1's *native-arm discovery* before deciding whether stage
2 is technically safe. It should not build a custom-hand substitute as a
shortcut.

BF1942 itself has an independently documented player-hand IK use case: the
historical Willy vehicle tutorial describes an `AnimatedBundle` with two
`addSkeletonIK` records that lock the player's hands to steering-wheel
locations ([source](https://classic-battlefield-modding.fandom.com/wiki/BF42_Add_a_Land_Vehicle)).
The installed Willy template was also read directly: its targets are
`Bip01_R_Hand` and `Bip01_L_Hand`. Together with the mapped Windows path, this
shows that the engine can make a player's native hand chain follow
content-authored targets.
It does **not** show that infantry hand weapons author those records, nor does
it permit BFVR to add or drive targets.

## BF1942-specific asset implication

The BF1942 Mod Development Toolkit creates handheld weapons with the native
`HandFireArms` type and configures their skeleton/animation data for weapon
handling, including firing, reloading, and running
([reference](https://bfmods.com/mdt/Tutorials/How%20to%20Create%20New%20Weapons/How%20to%20Create%20New%20Weapons.html)).
This supports the native presentation path because the held weapon and its
poses already live in game/mod content. It does not make `HandFireArms` the
arm renderer, reveal the live WinPC arm object graph, or authorize editing
those assets.

## Native 1P asset inventory (archive TOC evidence)

The shipped content removes the proposed bespoke-asset blocker for the stock
game. The table of contents of `Mods/bf1942/Archives/animations.rfa` contains
the following native first-person assets:

| Content | Observed native examples | Implication |
| --- | --- | --- |
| Faction arm-mesh skins | `1pBritBody.skn`, `1PDesGerBody.skn`, `1pGerBody.skn`, `1pJapBody.skn`, `1PRussBody.skn`, `1PUsBody.skn` | Stock first-person arm presentation is already faction-specific despite the legacy `Body` asset label. |
| Faction hand skins | Separate left/right pairs for British, German, Japanese, Russian, and US forces (for example `1pUsLeftHand.skn` / `1pUsRightHand.skn`) | Existing native hand geometry can be retained; BFVR need not create hand art. |
| Authored 1P animations | `animations/StandWalkRun/1p/...` and extensive `animations/WeaponHandling/1P/...` BAF families, including idle, deploy, fire, reload, and stance movement | The content includes the native weapon-handling poses that a hand-only replacement would otherwise have to recreate. |

Installed mod content also varies the native first-person presentation. The TOC
of `Mods/GCMOD/Archives/standardmesh.rfa` includes `1PRebPilot.sm`,
`1PTiePilot.sm`, `1PJedi_body.sm`, `1PDarkjedi.sm`, and a matching
`1pRebPilotLeftHand.sm`/`1pRebPilotRightHand.sm` pair. This is direct local
evidence that a fixed BFVR faction-to-arm table would be wrong even in the
current installation.

These are archive-name observations, not a runtime binding proof. Existing
read-only extraction has already shown item-specific `HandWeapons` configuration
(weapon skeletons, anchors, and camera offsets), but neither that configuration
nor the TOCs identify which live soldier child, skeleton, or draw owns each arm
asset. They establish the correct requirement: trace and preserve the game/mod's
current native selection at runtime, rather than importing or selecting a
replacement asset in BFVR.

The existing weapon-presentation research makes this requirement concrete:
`Bar1918`, `Colt`, `KnifeAllies`, and `GrenadeAllies` use different native
`soldierCameraPosition`, `.ske`, main-part, and child-pivot data. A firing
`FireArms` instance exposes its template at `+0x4C`, and the profiled
`HandFireArms` serializer reads its `soldierCameraPosition` at template
`+0x3EC..+0x3F4`; however, BFVR has not yet safely associated every classified
draw with that live template. Thus a controller-to-wrist or controller-to-weapon
offset must eventually come from the active native template/attachment chain,
not a BFVR weapon/faction lookup table.

The native chain is now known in principle: a `HandFireArms` item is an
`AnimatedBundle`, whose template can hold named-bone `SkeletonIkInfo`.
During visual update, the bundle resolves that target against the active soldier
Skeleton and sets the native IK target before its own animation update. This is
the native content route used by the Willy steering hands. The direct infantry
audit below shows that the currently installed hand-weapon templates do not
reuse it, so a future controller feature would require a separately approved
dynamic target path. It is not a justification to inject a new IK record or
call a solver directly.

### Direct installed-template IK audit

This question now has a firm answer for the currently installed direct
hand-weapon templates. Using the existing read-only archive inspector, all
`objects/HandWeapons/*/Objects.con` entries were decompressed and checked:

| Archive | Readable direct `Objects.con` entries | Creating `HandFireArms` | Declaring `addSkeletonIK` |
| --- | ---: | ---: | ---: |
| Stock `Mods/bf1942/Archives/Objects.rfa` | 28 | 26 | 0 |
| Installed `Mods/GCMOD/Archives/objects.rfa` | 36 | 33 | 0 |

The templates do still create native weapon Skeletons and AnimatedBundle/LOD
hierarchies. The absence is specifically of pre-authored *infantry hand target*
records. Therefore, controller-driven arms cannot generally reuse an
`addSkeletonIK` target already supplied by the active weapon, even though the
same engine feature is documented for native vehicle steering hands. This audit
does not rule out includes, level overrides, or runtime-created data, but it
rules out treating authored infantry targets as the baseline design.

The practical consequence is precise: a future native right-arm experiment does
not need bespoke hand art or an external arm solver, because the game already
has a hand-targeted two-bone native solve. But it must introduce a separately
approved dynamic *native target* path, since infantry content supplies no
targets to reuse. That path would have to derive the active local native
soldier/hand/weapon relation, run between authored animation evaluation and the
normal Skeleton transform, and yield to reload, throw, swap, and death. None of
that runtime work is in scope or authorized by this research.

### Native skinned-part binding

The static constructor path now removes one narrower uncertainty from the
asset discussion. A `BFSoldier` first creates two per-soldier clones of the
template Skeleton, then walks the game-created child hierarchy. In the semantic
Mac build, a child with the `AnimatedMesh` interface receives the first clone
through `AnimatedMesh::setSkeleton`; the setter establishes the mesh's
skin-to-bone matching. The second clone is supplied to a
`SkeletonCollisionMesh` child. The retail WinPC constructor is an exact
structural match, including the same child-interface decision and clone order.

Therefore, an installed first-person part that resolves to `AnimatedMesh` can
use the native soldier Skeleton even though its `SimpleObject` configuration
does not declare another Skeleton. This makes native faction/mod-selected arms
more plausible than an external replacement model, but it is deliberately not
a live binding claim: the configuration does not name the resolved geometry
implementation, and no local runtime child, lifetime, or draw has been
observed. A passive live trace remains the gate for Level A.

### Native faction/mod part selection

The installed soldier templates directly resolve the user’s no-hardcoded-assets
requirement. Each `BFSoldier` config attaches its native first-person parts then
marks them with `setIsFirstPersonPart 1`; `SoldierCamera` instead uses value 2.

| Installed soldier template | Marked native 1P presentation parts |
| --- | --- |
| Stock British / Canadian | British 1P arm mesh (`rBody` label) + British left/right hands |
| Stock German desert / German | Corresponding desert/German 1P arm mesh + German left/right hands |
| Stock Japanese / Russian / US Marine / US | Corresponding faction 1P arm mesh + its left/right hands |
| GCMOD Endor / Fleet / Scout | Corresponding custom 1P arm mesh + its left/right hands |
| GCMOD MonTrooper | Custom left/right hands only |

This is a critical architectural result: stock infantry supplies the native
whole-arm/body presentation you prefer, but a compatible mod may intentionally
provide hands only. BFVR should therefore discover and present the active
template’s marked native part set, rather than imposing "whole arms" or
"hands-only" as a global policy. Static content selection is proven; live child
ownership, mesh/skeleton identity, and lifetime still need passive tracing.

### Existing BFVR render-level arm boundary

BFVR already has a separate, non-asset-based way to recognize the current
first-person arm *draw family*. In presentation mode, its static draw policy
accepts a draw only when it is the exact profiled `AnimatedMeshSkinning` route
and is using BF1942's narrow first-person perspective projection (`m00 >= 2.0`,
`m11 >= 3.5`). It then omits that draw from VR replay. The classifier explicitly
fails closed for ordinary remote-soldier projections and unclassified narrow
draws, and its regression test covers both near misses. It contains no faction,
class, weapon, or asset-name table.

That is important for scope: **Level A does not need a private live
`BFSoldier*` or a new arm renderer just to restore native floating arms.** It
can use this existing render-level ownership boundary and preserve every native
faction/mod selection, skeleton pose, reload, and weapon-state animation. The
decision to suppress the draw was a stereo-presentation workaround, not proof
that the arms are absent from the game.

This remains intentionally weaker than object ownership. The boundary identifies
the local first-person rendered family, not a particular mesh object, skeleton
pointer, bone, or left/right hand. Before re-enabling it, a separately approved
presentation experiment must verify that the existing narrow-projection and
skinning correction produce stable stereo arms across a stock faction and a
hands-only mod, with weapon, reload, throw, swap, and death transitions. It
cannot yet support controller wrist targets (Level B).

## Viable architecture and fallbacks

| Level | What is shown/controlled | Preconditions | Faction/mod rule |
| --- | --- | --- | --- |
| A — native animated parts | Restore the game-selected native 1P part set and retain its authored poses; keep existing controller-driven weapon presentation separate. | Existing fail-closed render-level 1P arm classification, plus a separately approved stereo/transition validation across native variants. | Follow the existing native first-person draw route and `setIsFirstPersonPart 1` selection; never load an arm asset by name. |
| B — native right-arm targeting | Drive only the native right wrist/hand from the controller while preserving native weapon attachment, recoil, reload, and gameplay fire. | Proven local-1P ownership, safe scheduling, an active native weapon anchor, and a separately approved dynamic target path: installed infantry templates do not author `addSkeletonIK` targets. | Derive bone/anchor from the active template and skeleton; validate every faction/class/mod switch. |
| C — two-controller floating arms | Add a constrained left-arm/off-hand target; blend or yield to native reload, grenade, and swap animations. | Both arm chains, left-hand attachment semantics, safe solver/update behavior, and transition rules are proven. | The same game-owned child/skeleton path must select each variant automatically. |
| Fallback | Native authored arms without controller solving, or no arms if ownership cannot be safely proved. | Any missing proof above. | No substitution with a universal hand/arm model. |

Level A is the first worthwhile arm milestone. On stock infantry it naturally
means native floating arms; on a mod it may mean its native hands-only set. Level B is the plausible motion-
control target; Level C should not be promised until the left-hand and transition
evidence exists. In all levels, BF1942's native firing, reload, projectile, and
network paths remain authoritative.

## Recommended investigation order

1. For Level A, first use the already fail-closed renderer classification in a
   separately approved presentation-only validation: verify native arms are
   stereo-stable and naturally transition through a stock faction, a weapon
   change, and a hands-only mod without a BFVR asset table.
2. Capture a passive local-infantry trace that identifies the active 1P arm
   `AnimatedMesh`/skeleton versus its weapon mesh only if Level B is desired.
   The implemented `--first-person-arm-probe` waits up to two minutes for a
   local first-person traversal, then preserves a brief full
   renderer-correlation interval. Its existing local camera-shake callback supplies
   a candidate local `BFSoldier*`, and a temporary thread-local breakpoint at
   `FUN_004F7F20+0x7A` records the game's own child/marker traversal. It logs
   the local candidate, two native skeleton pointers, right-hand index,
   active-item index, and each direct child’s marker/template/sibling pointer.
   It does not call game methods or write game data. A live capture remains
   pending.

   **Update (2026-07-29):** That child-traversal implementation was retired
   after its first local/offline run exited `0xC0000096` as first person became
   active. The revised probe does not use a thread breakpoint or access debug
   registers. It is isolated from D3D8/OpenXR diagnostics and captures only
   guarded local-soldier roots (template, animated/collision Skeleton pointers,
   right-hand index, and active-item index). It makes no game-method call or
   data write. It does not identify an arm child or mesh; that requires a
   separate renderer-correlated observation.

   The revised root trace then completed cleanly in one local infantry run:
   camera and visibility both identified `BFSoldier 0x14E9C764` in first person,
   with separate animated/collision Skeletons, a readable animated bone-record
   base, right-hand index `44`, active-item index `3`, and no guarded-read
   failures. This validates the local Skeleton-root contract only; it does not
   name an arm mesh or permit pose control.

   The next safe revision also reads the already mapped `BFSoldier`
   `+0x2A4..+0x2A8` child-binding vector, recording only each existing receiver
   pointer, flags, root bone, and span. It does not call a game method or walk
   the separate visibility-child chain. This narrows the native attachment
   candidates but still requires renderer correlation before claiming an arm
   mesh.

   One local run produced three bindings with roots `14`, `17`, and `66` and
   spans `3`, `0`, and `0`. Root `14` is the audited `Bip01 Spine3`; the other
   two roots and all three receiver types remain untyped. The trace therefore
   proves the live binding-vector layout but not a visible arm owner: these can
   be kit/attachment objects rather than marker-1 body/hand meshes.

   The next revision bridges that remaining gap without invoking game code: it
   forwards the exact WinPC animated-mesh draw boundary `FUN_005AEEC0`, and
   after local roots are captured records unique instances whose own `+0xF4`
   projection satisfies the same narrow first-person thresholds used by BFVR's
   renderer policy. This identifies renderer-level first-person mesh instances,
   but still does not infer a direct-child relationship or change their poses.

   **Timing correction (2026-07-29):** The first run of that revision captured
   its local roots only at the original two-minute deadline, leaving no time
   for the required three-second mesh-correlation interval. Its lack of mesh
   records is therefore invalid rather than negative. The bounded probe now
   retains its hooks for the complete post-root interval and signals the
   loader immediately after removal; one replacement local run is pending.

   **Valid renderer correlation (2026-07-29):** The corrected one-shot run
   (PID 6708) completed normally after capturing the local first-person
   `BFSoldier`/animated Skeleton roots. It then observed three unique
   `FUN_005AEEC0` AnimatedMesh instances, each with FOV `0.470000` and narrow
   projection `m00=2.349391`, `m11=4.176696`: meshes `0x1FD51C58`,
   `0x1FD52108`, and `0x1FD52298` (with distinct game templates). This proves
   a temporal local-1P/draw association without an asset table. It does not
   identify which instance is body, hand, or weapon, prove a Skeleton binding,
   or authorize pose control.

   The next read-only discriminator compares each captured WinPC instance's
   candidate `+0x18C` Skeleton pointer with the local soldier's animated and
   collision Skeletons. Semantic Mac `AnimatedMesh::setSkeleton` owns `+0x18C`;
   the shared cross-build instance layout already matches `+0x24` template and
   `+0xF0/+0xF4` FOV/projection fields. A pointer match would be useful native
   binding evidence, not a license to write the Skeleton or claim body/hand
   identity.

   **Live Skeleton binding (2026-07-29):** PID 32704 completed that comparison.
   All three local narrow-projection candidates had `mesh +0x18C` equal to the
   captured local animated Skeleton `0x262CDFB0`, rather than collision Skeleton
   `0x262CDFF8`. BFVR can now identify the game-selected, Skeleton-bound native
   1P presentation set without a faction/mod asset table. It still cannot label
   the individual meshes or safely drive their Skeleton; those are separate
   motion-control gates.

   The next name-discovery trace records guarded printable previews of each
   template's `+0x34` storage and its potential external-string pointer. The
   semantic BStandardMesh constructor uses that field as the native template
   name. This is a passive ABI observation so BFVR can verify faction/mod
   selection; it neither calls string/game methods nor treats a preview as an
   asset substitution path.

    **Native 1P labels (2026-07-29):** PID 32076 resolved the tested local set
    as `rBody`, `rRightHand`, and `rLeftHand`; all three remain bound to the
    local animated Skeleton. The owner confirms the visible 1P set is arms and
    hands only, with no torso. Accordingly, `rBody` is only the native template
    label for the arm mesh and must never be interpreted as full-body awareness.

    **Post-animation native hand pose (2026-07-29):** PID 27264 then completed
    the prefix-verified forwarding observation at `FUN_004FB150`. After the
    unchanged original `BFSoldier::updateAnimations` returned, it captured the
    selected Skeleton's index-44 right-hand matrix. Its translation row was
    `(0.119452, -0.004070, 0.413563, 1.000000)`. This proves the exact native
    authored-pose output boundary for the current selected arm/hand set. It
    neither controls the hand nor authorizes an IK, Skeleton, item, or template
    write; native reloads and all other animation transitions remain untouched.

    Static inspection of that same function further shows the game resolves the
    soldier's active item from its current-item index, obtains its
    `AnimatedBundle` interface, and calls
    `setRelativeBoneTransform(0, finalRightHandMatrix)`. This is the native
    hand-to-active-item route BFVR must retain. It is not evidence that every
    item is a firearm or a safe target for an injected call.

    The next bounded observer revision forwards the prefix-verified WinPC
    query-interface implementation only during the original local soldier's
    `updateAnimations` call. It records returned item/interface pointers and
    vtables with guarded reads, but does not call an interface, alter its
    relative transform, or identify a concrete item type until a live result
    supports that conclusion.

    **Candidate dispatch rejection (2026-07-29):** PID 7156 completed the
    prefix-verified candidate query trace with the normal local arms, hand
    matrix, and clean shutdown, but no candidate query call. Therefore
    `FUN_0053B820` is not the observed active-item override in that run. This
    does not weaken the native attachment contract; BFVR will derive the live
    item's actual virtual dispatch target before another passive trace. The
    next revision directly reads the already-owned lookup object, its vtable
    slot, and a short code prefix; it does not call that target or alter an item.

    **Dispatch reconstruction correction (2026-07-29):** PID 4800 and PID 9492
    each produced one guarded-read failure while native arm/hand and
    post-animation observations completed normally. The issue was not timing:
    the exact update code treats soldier `+0x11C` as the vtable pointer of an
    embedded dispatch subobject and calls its slot `+0x14`. The observer had
    dereferenced that vtable as a second object. The corrected version reads the
    embedded subobject's vtable slot directly and still calls nothing.

    **Live current-item resolver (2026-07-29):** PID 12056 captured the correct
    embedded dispatch at `soldier +0x11C`, vtable `0x008EB0F0`, slot `+0x14` =
    `0x004F9EA0`, with zero read failures. Its live role and index-guard prefix
    match semantic `BFSoldier::getItem(int)`. BFVR now has the game-owned
    active-item resolution path; it must preserve, not call or replace, it.

### Native controller-IK contract (2026-07-29)

This is now a concrete native-engine path, not a rendering workaround:

1. `BFSoldier::updateAnimations` first advances the native animation state and
   reaches `Skeleton::transform` on the local Skeleton at `soldier +0x298`
   near the end of its update. Only after that transform does it give the
   resulting native right-hand matrix to the active item through
   `AnimatedBundle::setRelativeBoneTransform(0, finalRightHandMatrix)`. BFVR
   must leave that attachment call in place; it is what keeps every ordinary
   game-selected weapon/gadget in the hand.
2. The engine's `Skeleton::applyIk` (`0x006123B0` in this WinPC build) accepts
   an end-bone index, a target position, and a target orientation matrix. Its
   exact native consumer, `Skeleton::transform` (`0x00611690`), invokes the
   two-bone arm solver before it finalizes the bone matrices. The profiled
   `applyIk` entry begins `53 55 56 57 8B F9 8B 47 0C 85 C0 8D 4F 08`.
3. `AnimatedBundle::updateIk` proves the required coordinate conversion. It
   converts an attachment point from its owning object to world space, then
   applies the inverse soldier transform before calling `Skeleton::applyIk`.
   Therefore BFVR must deliver controller targets in the current soldier
   Skeleton frame, not camera, clip, eye, or view-model-render space.
4. At an explicit calibration commit, BFVR captures the native hand target `H`
   and current controller grip `G0` as a row-vector attachment
   `A = H * inverse(G0)`. Each frame it supplies
   `T = A * G_current`. This gives `T == H` at commit while applying later
   controller translation in the hand/Skeleton frame. Reversing that order
   (`G_current * inverse(G0) * H`) also preserves the commit instant but puts
   translation in the wrong frame and was disproven by the native motion
   probe. No faction-, class-, weapon-, or mod-specific hand/arm asset or
   offset is hard-coded; `H` comes from the currently selected native arm
   Skeleton.
5. The injection must be temporary and occur at `Skeleton::transform`, not at
   the entry to `BFSoldier::updateAnimations`: the latter is too early, before
   that frame's animation-state work. BFVR first observes one completed,
   unmodified native hand pose. On a later local transform it snapshots the
   selected bone's existing IK-handle index, calls `applyIk` immediately before
   the original transform, then restores the index in guaranteed cleanup after
   the solver returns and before the weapon-attachment code continues. This
   lets BF1942 consume one controller target for that frame while preserving
   authored vehicle targets and the next frame's unmodified native animation.

The native format supports both hands: the game content command is
`ObjectTemplate.AddSkeletonIK <bone> <position> <rotation>`, and documented
BF1942 vehicle content provides separate `Bip01_R_Hand` and `Bip01_L_Hand`
targets. The initial implementation should drive the already live-proven right
hand first. Adding the left controller requires a dynamic, mod-safe live
`Bip01 L Hand` resolver; BF1942 only caches the right-hand index on the soldier
template, so BFVR must not guess a left-hand numeric bone index.

This is deliberately arms-only. It does not add manual reloads, physical
magazines, or custom hand meshes. OpenXR's grip pose is the correct source for
the hand target; the aim pose remains the appropriate source for a gun/ray
direction. The distinction is defined by the [OpenXR specification](https://registry.khronos.org/OpenXR/specs/1.0-khr/html/xrspec.html#input-sources),
which describes grip as the pose for an object held in the hand. The broader
pattern is also consistent with FRIK/VRIK-style mods, which animate limbs from
the controller and HMD positions, but BFVR avoids their full-body scope
([FRIK description](https://www.nexusmods.com/fallout4/mods/53464)).

The decisive BF1942-specific corroboration is existing content: vehicle
authors use two `addSkeletonIK` targets to lock player hands to a steering
wheel ([Classic Battlefield Modding tutorial](https://classic-battlefield-modding.fandom.com/wiki/BF42_Add_a_Land_Vehicle));
the Mod Development Toolkit examples show the explicit right- and left-hand
bone names ([MDT example](https://www.realtimerendering.com/erich/bf1942/mdt/MDTDOC/Confiles/ObjectTemplate/Properties/HeatAddWhenFire.htm)).

The remaining implementation work is bounded: use the prefix-validated native
transform boundary, connect the already accepted OpenXR grip sample, perform
the one-time calibration/map capture, inject/restore the native handle, and
extend the runtime resolver for the left hand. It is not further renderer
research or asset creation.

**Initial implementation status (2026-07-29):** BFVR now has that
prefix-validated right-hand implementation in
`BFSoldierNativeArmIk.cpp`. The normal `--weapon-motion-probe` launcher route
enables it itself; the old visual-only weapon transform is suppressed on that
route so it cannot double-transform the gun. The native hook follows only the
current camera soldier in first person, declines any hand already occupied by
native vehicle/mod IK, observes one unmodified completed hand pose, then
calibrates and injects immediately before the engine's own two-bone transform
step. It restores the no-target handle index immediately after that transform,
before the unchanged weapon attachment. Its first `applyIk` allocation is retained
as a private, inactive reusable record for that live Skeleton, preventing the
engine from appending a new target record every transform. This replaces the initial, incorrect
`BFSoldier::updateAnimations`-entry hook exposed by the first headset run. The
first transform-boundary run then exposed and corrected a global-hook ownership
bug: non-local Skeleton transforms were interleaved with the local arm transform
and repeatedly reset calibration, cancelling controller deltas. The hook now
rejects those calls before touching controller/calibration state. The Win32
BFVR client and loader compile and the stereo-math checks pass. The following
headset run proved persistent wrist rotation but no positional arm reach. The
following static cross-build review corrected an error in the first diagnosis:
`+0x78` is the translation row of the same 4x4 final hand matrix beginning at
`+0x48`, not a separate endpoint vector. The previous `+0x78` edit was
therefore a no-op and the unchanged headset result was expected. The current
build removes that ambiguity and automatically records bounded controller
grip, requested target, and post-solver hand-position deltas whenever the
controller moves materially. The next run proved that the engine does solve
positional targets: three samples ended at the requested point with zero
reported error. Other directions were fed into the wrong arm frame because
BFVR had composed the row-vector grip delta on the left of the hand target.
The implementation now uses the proven attachment order
`target = hand * inverse(calibrationGrip) * currentGrip`, matching BFVR's
existing weapon-pose math. Runtime validation of that actual positional fix,
then succeeded in PID 29920: the owner confirmed both native wrist rotation and
positional arm motion, while the bounded probe recorded exact requested/solved
target deltas. This is working native right-arm 6DOF, not merely a render
transform.

That successful run exposed two integration corrections. The authored
floating-arm root sits too far behind the VR head, so BFVR shifts the complete
current local 1P Skeleton root 0.15 metres forward rather than stretching only
the hand away from the shoulder. This preserves the native shoulder, elbow,
wrist, attached weapon, and faction/mod-selected parts together; authored
vehicle/mod hand IK still takes priority. PID 32020 judged that placement
better.

PID 32020 also corrected the fire-path interpretation. With the controller-fire
overlay disabled, the solved hand and rendered gun stayed attached but impacts
followed the flat centre-HUD direction. The native hand-to-item transform
therefore does not redirect `WeaponFire_Core`; native-arm mode must retain the
existing local-player/caller-gated fire overlay and feed it the solved hand's
world rotation once. BF1942 still owns native muzzle position, barrel offsets,
spread, cadence, projectile creation, and networking.

Finally, using the complete calibrated rigid relation
`hand * inverse(calibrationGrip) * currentGrip` makes the large difference
between the native hand origin and OpenXR grip origin act as a lever arm.
Controller roll then translates the gun around a pivot well below the hand.
The revised hand mapping composes the calibrated orientation relation only;
position is independently `nativeHandPosition + controllerPositionDelta`.
This retains 1:1 reach while rotation occurs at the wrist. Fire alignment and
the corrected pivot need one headset validation.

The pivot validation passed. After temporarily entering the Quest dashboard,
the owner placed the tracked physical hand over the visible native hand and
returned to the game; the resulting recalibration made arm translation and
rotation feel essentially flawless. This proves the remaining initial mismatch
is the retained native-hand calibration position.

Driving the hand directly from raw OpenXR LOCAL position was then headset
rejected: it put the arm/gun near the owner's crotch, and Quest-menu tracking
reacquisition could not correct it. Expressing the grip relative to the
matching centre-head pose produced the exact same placement. That run logged
native hand `(0.1394,0.3777,0.4686)` against computed target
`(-0.0067,-0.3054,0.4004)`, proving neither candidate is the final Skeleton
 frame. BFVR restored the accepted native-hand position plus independent 1:1
 controller displacement for PID 33220. The owner repeated the already
 successful physical-over-virtual Quest procedure and identified the final
 attempt as the closest. It recorded native hand `(0.1392,0.3777,0.4687)` and
 raw grip `(0.0419,-0.2990,0.5986)`, hence the measured global
 tracking-to-Skeleton translation `(0.0973,0.6767,-0.1299)`. The implementation
 now applies that translation automatically to the raw grip while keeping
 orientation calibration separate. This is a coordinate-frame value, not a
 faction/arm/weapon asset offset, and still needs validation across a new
 process/reference-space and another native faction.

Fire direction also passed, but the shot appeared to originate to the right and
slightly above the moved muzzle because the earlier fire helper deliberately
preserved the flat native translation. In native-arm mode the complete solved
hand world attachment now moves both fire rotation and origin; the legacy
visual-only path can still retain native origin. BF1942 continues applying its
weapon/barrel muzzle offsets, spread, cadence, projectile construction, and
networking.

The native-arm path had also bypassed the D3D8 overlay that consumed BFVR's
previously accepted recoil transfer. It now consumes the same native
soldier-scoped recoil sequence directly and composes the tested recoil rotation
inside the hand-orientation target, then restores the accepted independent
 wrist position. The native hand, attached gun, and paired fire transform can
 therefore share one computed recoil attachment without moving the wrist pivot.
 That computation is not yet a working presentation result: PID 33220 logged
 eight consumed sequence entries and accumulated pitch `-0.2201005`, yaw
 `-0.1217485`, while the owner saw no recoil. Native-arm recoil is explicitly
 failed/pending despite the internal counter. Moved muzzle origin also remains
 pending an aligned headset judgment.

PID 34000's Berlin death/respawn run isolated a separate orientation-lifetime
bug. The first and second alive-player bindings used different BFSoldier and
Skeleton pointers, but both observed exactly the same native hand position
`(0.2780,-0.1170,0.4791)`. BFVR nevertheless recomputed the hand/grip
orientation attachment at each alive edge from the controller's instantaneous
pose. The owner therefore had to tilt the controller differently after
respawn. The mapping now retains its first valid orientation attachment for the
injected/OpenXR process and only refreshes the soldier/Skeleton binding and
motion-probe anchors. Tracking loss, death, and respawn clear the temporary
binding and visual cache without silently choosing a new controller angle.
Injection and the 0.15-m root shift additionally require the existing local
`BFPlayer isAlive` byte, keeping the death/deployment camera outside the native
arm owner.

This retention currently assumes no explicit OpenXR LOCAL recenter. OpenXR
reports a runtime origin change separately through
`XrEventDataReferenceSpaceChangePending`; BFVR still needs to transport that
event's effective generation before a deliberate runtime recenter can safely
select a new hand/grip orientation relation. Ordinary BF1942 death and
match-start cameras do not redefine the OpenXR space.

The same run reported a projectile path roughly 10-15 feet above the visible
gun. No clean shutdown record preserved that fire matrix, so the owner's
match-start-camera explanation remains a theory rather than a confirmed
identity. It is mechanically reachable, however: applying the complete
native-hand world attachment to a distant cinematic/death-camera fire origin
rotates that distant point around the hand. The native-arm pose cache now
carries source-hand and solved-hand world anchors plus the soldier lifetime.
WeaponFire applies the moved-origin attachment only when that lifetime still
matches, the native fire origin is within 1.25 m of the native hand, and the
solved displacement is within 1.5 m. Otherwise it forwards the native shot and
logs the measured distances immediately. Deterministic tests distinguish a
nearby muzzle from a 4.5-m cinematic origin.

PID 34000 also invalidated the assumption that the published recoil sequence
was monotonic for one native arm: the consumed sequence advanced through 7 and
then returned to 1 while the arm soldier pointer was unchanged. Combined with
PID 33220's no-visible-recoil result, the running sum is unsafe for the native
hand and matching gameplay fire attachment. Native-arm recoil consumption is
was disabled in the next build. The legacy recoil math remained available to
its separately classified held-weapon path pending a stable local-soldier
source and a visible return-to-neutral result.

PID 24580 then proved that the remaining gross direction error was not Quest
tracking. The right controller flags were fully valid/tracked, but native arm
IK used `gripPose` and a captured stock-hand orientation; the available
OpenXR `aimPose` never owned the gun. Eight local shots used the same indirect
hand attachment, with a native fire-to-hand distance of `0.936 m` and solved
hand displacement around `0.57 m`. The replacement uses grip position only as
the held-object point and direct right-aim orientation as the gun direction
every frame. No alive edge, spawn camera, or prior controller angle is part of
that orientation. `WeaponFire_Core` receives the same solved held-gun basis
and origin, then retains BF1942's weapon/barrel offsets, spread, cadence, and
projectile construction. This removes the previous operation that rotated a
camera-derived fire origin around the hand.

That run also confirmed fresh native recoil values while the consumer was
disabled. The publisher now uses one process-monotonic generation that cannot
reset when another soldier transiently queries the global hook. The native arm
consumes each matching local-soldier sample once, applies the accepted
degree/sign conversion around the grip pivot, and publishes the same recoiled
aim pose to fire. Pure tests cover direct controller forward/origin ownership
and recoil pivot preservation. Visual gun-to-pointer agreement, projectile
path, muzzle placement, recoil kick/return, and respawn still require headset
validation.

PID 10356 confirmed that the direct fire half of that contract works: shots
followed the controller pointer. It also exposed a separate visual-space error.
The visible hand and gun pointed sharply back/right because the OpenXR
gun/barrel aim basis was written directly into the anatomical
`Bip01 R Hand` target. The native hand bone has an authored rotational basis
relative to the fire parent. The runtime now recovers that relation from the
first verified local infantry shot as
`nativeHandFromFire = nativeHand * inverse(nativeFire)`. Fire continues to use
the unmodified direct controller gun pose; following IK frames use
`nativeHandFromFire * controllerGun`. Before capture, hand translation follows
the grip but its rotation stays native, preventing the broken-wrist pose. This
capture is rig alignment, not Quest/menu or spawn-camera calibration.
Deterministic tests cover relation recovery, application under a different aim
basis, and non-rigid rejection. The corrected visual relation and portability
across weapon changes still require headset validation.

PID 17720 established why multiplication order matters here. The owner's
down/left, ground-tether description occurred even though every live motion
probe reported identical grip, target, and solved translation deltas with zero
target error. The implementation had used the opposite, column-vector-like
order, turning a local wrist correction into a world-axis constraint. The
corrected pure helper pre-multiplies the local hand/fire relation, preserves
the controller translation, and is covered by a non-commuting yaw/pitch test
that distinguishes the two orders.

PID 14916 then exposed the lifecycle error. The rifle remained in its native
fallback angle until the first shot and became nearly correct immediately
afterward; selecting the pistol reused that rifle hand-bone correction and
left the wrist down/left. The relation was therefore not a universal gun
direction and could not remain process-global. That run also summed eight
legacy recoil-state samples to pitch `-0.3611841`, yaw `-0.0806845` while
shots rose far above the pointer. Because direct OpenXR pointer fire had
already been confirmed, the first-shot/global alignment cache and accumulated
native-arm recoil consumer are removed.

The installed WinPC executable supplies a generic pre-shot replacement.
`BFSoldier::updateAnimations` calls the selected item's AnimatedBundle
interface slot `+0xC` and returns at `0x004FBC4B`; vtable `0x008F94DC` maps the
slot to `0x0054ECC0`. That function's null guard, `0xE8` selected-bone stride,
and 16-dword matrix copy exactly match semantic
`AnimatedBundle::setRelativeBoneTransform`. BFVR now observes bone 0 only at
that exact alive-local callback. A new item/soldier receives two untouched
native updates, after which its own native hand/fire relation is recovered
in one world frame before any shot and scoped to that item/lifetime. The
controller aim pose owns the gun and firing basis continuously; the hand IK
follows through the selected item's authored relation. Switching rifle/pistol
cannot reuse the other weapon's wrist state, and spawn/death cameras do not
participate.
Because the attachment callback is global, BFVR also requires the same thread's
immediately preceding `Skeleton::transform` to match the alive current-camera
soldier and the callback matrix pointer to equal that skeleton's exact final
right-hand matrix. Remote/interleaved soldiers cannot replace local alignment.

PID 12772 then validated the result in-headset: the owner reported correct hand
position, wrist angle, aim, rifles, and pistols. Knives and grenades retain a
separate twisted-wrist defect for later work.

The remaining crouch/prone problem is translation-only. BF1942 lowers its
native camera when the soldier pose changes, but the absolute OpenXR
controller target previously remained in standing soldier space. Exact
Mac-to-WinPC matches identify `BFSoldier::getPose` at installed
`0x004F6CA0` (standing/crouching/prone `0/1/2`) and
`BFSoldier::getPoseCameraPosition` at `0x004F6CC0` (WinPC template
`+0x254 + pose*0xC`). BFVR now adds
`cameraPosition[currentPose] - cameraPosition[standing]` to the controller
gun's local translation before the soldier world transform. This moves the
hand, attached weapon, and muzzle origin together while leaving the validated
OpenXR aim and per-item hand/fire rotations unchanged. Both installed getter
bodies are fully signature-checked and their results are range/finite checked.
The client and both math suites pass. The owner then confirmed in-headset that
this fixed crouch and prone while retaining the already-correct firearm hand
position, angles, aim, rifles, and pistols. This complete firearm state is
preserved by repository-root `KNOWN_BEST_FIREARM_ALIGNMENT.md` and tag
`bfvr-known-best-firearm-alignment-2026-07-29`.

This matches the OpenXR role split: grip is the held-object/hand pose and aim is
the pointing pose, with OpenXR forward along `-Z`
([OpenXR 1.1](https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html)).
It also matches the standard flat-to-VR practice of attaching the weapon mesh
to the motion controller while retaining/calibrating the native mesh offset
([UEVR 6DOF guide](https://docs.uevr.io/usage/adding_6dof.html)).

When controller tracking is lost or the controller sleeps, BFVR clears the
temporary soldier binding and yields to the authored pose instead of holding a
stale hand target. Tracking recovery resumes from the current tracked aim pose
without sampling or retaining a replacement zero angle. The observed pop is
intentional fail-closed behavior and may later be smoothed. A dynamic
`Bip01 L Hand` resolver remains required before two-controller arms.
3. Repeat that object-level trace across at least two factions/classes, a weapon
   change, and a mod weapon. The decisive result is whether the same game-owned
   update/draw path selects different native assets without a BFVR asset table.
4. Recover the skeleton's bone list/hierarchy and determine how the weapon
   attaches to each wrist/hand during idle, fire, reload, throwing, and swap.
5. Passively prove live scheduling and local 1P ownership at the mapped WinPC
    animation-update/hand-attachment boundary, then trace the active native
   AnimatedBundle/IK-target chain. The installed direct weapon templates do not
   contribute a target, so any dynamic target path needs separate scope and
   safety approval. Prove exact ABI and timing before a reversible,
   presentation-only experiment.
6. If that proof does not materialize, keep the native arms in their authored
   animation and make only the already-proven controller-driven weapon a
   supported option. Never replace them with a fixed external hand model.

## Decision criteria

Native controller-driven arms are plausible only if BFVR can prove all of the
following on WinPC: active native asset ownership, faction/mod selection,
stable skeleton lifetime, concrete wrist targets, a safe update boundary, and
correct handling of native animation transitions. Failing any one of these is
a reason to retain the native animated arms or omit arms, not to hard-code a
single arm model.

## Off-hand and two-handed weapon policy (2026-07-30)

The 2026-07-29 known-best firearm path is the starting invariant: right grip
owns the physical hold position, right aim owns firearm direction, the current
game-selected item supplies its authored `handFromFire` rotation, and the same
gun pose reaches the visual hand/weapon and `WeaponFire_Core`. Left-hand work
must be additive and must fail back to that exact one-hand behavior.

### External implementation findings

- OpenXR defines `grip` as the pose for reliably rendering an object held in a
  hand and `aim` as the runtime's pointing pose. The off hand should therefore
  acquire and place a support hand from its **grip** pose, not from its aim ray
  ([OpenXR 1.1](https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html#input-sources)).
- UEVR's generic flat-to-VR route attaches a chosen native weapon component to
  one controller and calibrates that mesh/controller offset. It does not infer
  a universal second grip; a game-specific integration must recover that
  weapon's support relationship
  ([UEVR 6DOF guide](https://docs.uevr.io/usage/adding_6dof.html)).
- Half-Life 2: VR Mod requires the off hand to be near a weapon handle/grab
  point and the player to hold Grip. The primary hand continues to aim; the
  effect of two-handing is weapon-dependent, ranging from recoil control to
  aesthetic support
  ([HL2VR manual](https://halflife2vr.com/manual/#weapons)).
- GTFO VR demonstrates the alternative automatic policy: double-handed aiming
  activates when the off hand approaches the weapon grip and releases after it
  moves far enough away. Its threshold and always-on behavior are configurable
  ([GTFO VR source project](https://github.com/DSprtn/GTFO_VR_Plugin#aiming)).
- Lambda1VR and RTCWQuest use held off-hand Grip for weapon stabilization.
  Lambda1VR explicitly rejects the two-controller direction when the hands are
  closer than 15 cm because the short baseline destabilizes handgun aiming
  ([Lambda1VR controls](https://github.com/Team-Beef-Studios/Lambda1VR#controls),
  [RTCWQuest controls](https://github.com/Team-Beef-Studios/RTCWQuest#controls)).
- Unity's XR Interaction Toolkit models multi-hand interaction with a separate
  `secondaryAttachTransform`, multiple-grab transformers, and an explicit
  reinitialization choice when returning to one grab. This is useful structural
  corroboration for an authored support socket plus a deliberate release
  transition, not a BFVR runtime dependency
  ([XR Grab Interactable](https://docs.unity.cn/Packages/com.unity.xr.interaction.toolkit%403.0/manual/xr-grab-interactable.html)).
- FRIK's mature arm path rejects invalid or unreachable controller targets and
  smooths wrist-twist changes to suppress elbow shake. BF1942 owns the actual
  two-bone solve, but BFVR still needs the same finite/reachability/lifecycle
  gates around any submitted target
  ([FRIK Skeleton source](https://github.com/rollingrock/Fallout-4-VR-Body/blob/main/src/skeleton/Skeleton.cpp)).

These implementations do not support a single universal behavior for all
weapons. In particular, a long gun benefits from the line between two separated
hands, while a cupped pistol has such a short baseline that using the off-hand
position as full aim authority can amplify controller noise. BFVR should derive
the distinction from the selected item's native authored hand separation before
adding a manual weapon class table.

### BFVR-specific transform policy

The existing local `Skeleton::transform` transaction already observes the
native right-hand matrix and the exact selected active item. Once a mod-safe
`Bip01 L Hand` resolver exists, one untouched native update can provide a
same-lifetime tuple:

```text
nativeLeftWorld
nativeRightWorld
nativeFireWorld
```

In BF1942's established row-vector order, the authored support relation is:

```text
leftFromFire = nativeLeftWorld * inverse(nativeFireWorld)
```

Under the current one-hand controller gun pose, its predicted support socket is:

```text
predictedLeftWorld = leftFromFire * controllerGunWorld
```

That relationship preserves the active animation's rifle fore-end, pistol-cup,
and wrist pose without a BFVR per-weapon offset. It must be refreshed only
after untouched native warm-up updates and scoped to the exact item and soldier
lifetime, just like the validated right-hand `handFromFire` relation.

Support acquisition should compare the tracked left grip with
`predictedLeftWorld` rather than comparing the two controller origins. Require
held left squeeze plus a small acquisition radius, then retain the state until
squeeze release or a larger release radius. The larger radius supplies
hysteresis. Tracking/focus loss, item/soldier changes, death, menus, non-finite
matrices, or an existing native left-hand IK handle release immediately to the
unmodified one-hand path.

While supported:

1. Right grip remains the weapon translation pivot.
2. The weapon is never scaled to make two independently tracked hands fit.
3. For a long authored support span, apply the minimal rotation about the right
   grip that carries the predicted support direction toward the tracked left
   grip. Right aim supplies the starting basis and retains roll authority.
4. Draw the left hand at the authored support socket, not at an arbitrary point
   floating off the weapon. Radial controller/socket mismatch is tolerated only
   within the bounded capture/release region.
5. For close pistol/cupped support, permanently keep the known-good right aim
   entirely authoritative and use the off hand as visual IK only. A short
   two-controller baseline is poorly conditioned for direction and must never
   steer, translate, scale, stabilize, or otherwise modify the validated
   pistol gun/fire basis.
6. If the gun basis changes for long-gun support, publish that exact basis to
   both the active visual/IK path and `WeaponFire_Core`; never create separate
   visual and projectile aims.
7. Do not reduce native spread/recoil, change cadence, or add a gameplay
   accuracy bonus in the first implementation. Other mods do so, but it is a
   separate gameplay policy rather than a requirement for two-hand pose.

The release fallback is the existing direct right-grip/right-aim firearm pose.
An optional short visual transition can be evaluated later, but the first
bounded implementation should prefer exact visual/fire agreement and immediate
fail-closed release over retaining a stale support pose.

### Ungripped left hand and the knife-pose suggestion

The owner's observed knife idle is useful evidence: roughly two seconds after
equipping the knife, BF1942 exposes a plausible neutral left-arm/hand pose. It
should be captured passively and compared with other native item, faction, and
mod poses. It should **not** become a runtime dependency that requires a knife
animation or hard-coded stock skeleton index.

The first ungripped experiment should be narrower: solve the dynamically
resolved left hand to the tracked left-grip **position** while retaining the
current native wrist orientation. This tests left-arm reach, shoulder placement,
handle ownership, and cleanup without inventing a controller-to-anatomical-wrist
rotation. Only after the neutral native poses and left-hand bone axes are
measured should BFVR decide whether the knife pose supplies a reusable neutral
orientation reference or whether the current animation is the safer fallback.

PID 21068 validated that position-only endpoint solve and exposed an independent
extreme/opposite elbow-pole defect, which is intentionally deferred. The next
bounded wrist test treats the accepted per-item native orientation as a zero
pose and applies only controller rotation relative to the grip orientation at
that capture. This can validate twist axes/order without claiming a final
absolute palm/controller calibration or changing the elbow plane.

PID 21116 then validated the relative wrist mapping across two active-item
bindings: all rotation/twist axes and directions were correct. The elbow still
selected the opposite pole and pointed away from the player, confirming that
the accepted endpoint/wrist result should remain unchanged while pole-plane
work is deferred.

### Support acquisition and input resolution

At the owner's direction, left squeeze no longer submits native prone. It is
exclusive to off-hand support, and right-stick-down remains the prone route.
This removes the gameplay side effect instead of conditionally mediating two
meanings on the same press.

The first runtime acquisition slice is visual-only for every item. It uses the
right-authoritative gun matrix only to predict the authored support socket,
requires the tracked left grip to remain within 0.12 m while squeezed for
0.04 s, retains through a 0.20-m radius, and replaces only the left-hand IK
target. It cannot modify weapon aim or fire. Close pistol/cupped support remains
permanently visual-only; possible bounded long-gun steering is a later,
separate policy.

PID 9512 rejected that first positional anchor while validating its safety.
BFVR had applied `leftFromFire` to a controller gun matrix whose orientation
was the gun basis but whose translation was intentionally the right-grip
origin. The stock slot-3 sample measured 0.3276 m left-hand-to-right-hand but
only about 0.107 m left-hand-to-fire-origin, explaining why the rifle hand
landed beside the dominant grip.

The corrected visual policy therefore uses two explicit item-slot modes:

1. Slot 3 applies
   `nativeLeftWorld * inverse(nativeRightHandWorld)` to the exact solved
   controller right-hand pose, preserving BF1942's real two-hand span.
2. Slot 2 does not reuse BF1942's currently sampled 0.4426-m idle left-hand
   pose as a pistol cup. The owner confirms that the game does contain a real
   authored cupped pistol pose, but the current untouched warm-up sample does
   not expose its animation state/timing. Until that state is recovered,
   close+squeeze captures the current visual left-to-right relation without a
   jump and locks that cup to the right hand until release. Left-controller
   noise never reaches weapon aim.

Other slots fail back to the free hand. PID 6476 and the owner's direct report
validate both corrected modes in-headset. Pistol support remains permanently
visual-only; long-gun steering remains a separate policy.

That long-gun policy is now implemented for supported slot 3. It starts from
the exact one-hand gun basis, holds the right-grip translation fixed, and
applies the minimal twist-free swing that brings BF1942's authored support
direction toward the tracked left-grip direction. The swing is capped at 35
degrees, radial mismatch does not scale or translate the gun, and collapsed or
ambiguous opposite directions fail back to one-hand aim. It is eligible only
after the existing support state has acquired and only while current
focus/tracking/squeeze, item binding, and native IK ownership remain valid.

The adjusted gun matrix is installed before both visual right-hand/weapon
attachment calculation and `WeaponFire_Core` publication. Thus rendered aim
and projectile aim share one basis. The slot-2 captured cup is rejected by the
steering bridge and binding, so the pistol remains permanently visual-only.
Build `777A1982D5D38713292E7DBA9AAFFDA81C87BB64587C91DB794AC6CA38C48C2E`
passes deterministic fixed-pivot, clamp, no-scale, radial-mismatch,
degeneracy, current-input, tracked-grip conversion, and pistol-rejection
coverage; headset behavior is still open.

PID 24796 closes the first focused headset check. The owner reported that the
two-hand result works very well. Runtime evidence shows repeated slot-3
acquisition, next-frame steering activation, and release across three soldier
lifetimes, with first-applied swings between 0.72 and 15.99 degrees. Repeated
slot-2 cup acquisitions produced no primary-steering activation, confirming
the pistol remained visual-only in the live session. The actual 35-degree
limit, focus/tracking-loss fallback, semantic mod classification, native
pistol-cup timing, and elbow-pole correction remain separate open checks.

### Implementation gates

1. Resolve `Bip01 L Hand` by native name on the live Skeleton; never hard-code
   stock index 21.
2. Passively validate same-update left/right/item transforms across rifle,
   pistol, knife, grenade, weapon switch, reload, death/respawn, and at least
   one mod/faction path.
3. Pure-test the `Free -> Candidate -> Supported` state machine, hysteresis,
   primary-pivot minimal rotation, close-support visual-only policy, item
   lifetime invalidation, and all non-finite/untracked fallbacks.
4. Extend the temporary native IK transaction with a separate reusable left
   handle and guaranteed restoration of both bone handle indices. Preserve any
   vehicle/mod-authored target.
5. Headset-validate rifle and pistol separately before claiming general
   two-hand support. Knives and grenades remain outside the claim while their
   existing right-wrist presentation is unresolved.
