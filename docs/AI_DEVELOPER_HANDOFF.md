# BFVR AI and Developer Handoff

This is the primary continuation guide for human developers and AI coding
agents taking over BFVR. It explains what the major pieces do, which behaviors
must be preserved, and how to verify changes. It is intentionally more current
and task-oriented than the chronological `devREADME.md`.

## Start here

Read these sources in this order:

1. `README.md` for player-facing scope and requirements.
2. This handoff for the current architecture and invariants.
3. `docs/DEVELOPMENT.md` for reproducible build and test commands.
4. `devREADME.md` for the long-form history and evidence behind BFVR.
5. The relevant source and tests for the subsystem being changed.

The separate public
[Battlefield 1942 reverse-engineering repository](https://github.com/JayBiggsGMG/bf1942reverseengineering)
contains decompilation, memory mapping, and broader engine research. It is a
useful starting point for new game-internal work, but it predates some BFVR
discoveries. Treat it as supporting evidence, not as a newer source of truth
than this repository's current code and tests.

## What is included

The BFVR repository contains:

- Complete BFVR C++ source for the Win32 game-side components and x64 OpenXR
  presenter.
- Pinned OpenXR, MinHook, and d3d8to9 dependencies needed by the build.
- Player artwork and menu assets.
- Deterministic native tests and diagnostic probe targets.
- Player-payload staging and Inno Setup installer definitions.
- MIT license and third-party notices.

It intentionally does not contain Battlefield 1942, BF42++, private test game
installations, or the full outer reverse-engineering workspace.

## Process architecture

BF1942 is a 32-bit game, while current PC OpenXR runtimes are commonly 64-bit.
BFVR therefore uses two cooperating processes:

```text
BFVR.exe (Win32 launcher)
  -> starts suspended BF1942.exe
  -> selects one BF42++ loading path
  -> injects BFVRClient.dll
  -> resumes and follows the game process

BF1942.exe + BFVRClient.dll (Win32)
  -> redirects D3D8 through BFVRD3D8To9.dll
  -> renders stereo eyes and UI
  -> publishes shared D3D9Ex/D3D11-compatible textures and control state

BFVRPresenter.exe (x64)
  -> creates the OpenXR 1.0 instance/session
  -> consumes the shared textures with D3D11
  -> submits world projection, UI, menus, scope, vignette, and other layers
  -> samples OpenXR controllers and publishes normalized input to Win32
```

The cross-process protocol is pointer-free and versioned. Do not place native
pointers, C++ object ownership, or architecture-sized fields in shared records.

## Major source areas

### Launcher and compatibility

- `src/loader/BFVRLoader.cpp`
- `src/loader/Bf42PlusPlusCompatibility.*`

The launcher owns player startup, diagnostics defaults, BF42++ selection,
game-build reporting, injection, process replacement following, startup-movie
suspension/restoration, and launch-local renderer isolation.

### Win32 game client

- `src/client/BFVRClient.cpp`
- `src/client/D3D8StereoPairProbe.cpp` and `src/client/internal/`
- `src/client/ControllerInputOverlay.cpp`
- `src/client/BF1942FrameLimiterOverride.*`

The client owns game-side hooks, stereo world/UI production, recovered
Battlefield input submission, native arm/weapon integration, frame-limiter
override, and shared producer state.

### D3D8 translator

- `third_party/d3d8to9-1.15.1/`

BFVR carries a pinned and modified d3d8to9 translator. BFVR-specific changes
are documented in that directory's `BFVR_PATCHES.md` and `bfvr_*` source files.
The translator supplies D3D9Ex shared targets, packed depth, current
post-Reset presentation dimensions, and BFVR runtime diagnostics.

### x64 presenter and OpenXR

- `src/presenter/BFVRPresenter.cpp`
- `src/presenter/SharedTextureConsumer.*`
- `src/openxr/OpenXRPresentation.*`
- `src/openxr/OpenXRQuickMenu.*`

The presenter owns OpenXR lifecycle, swapchains, D3D11 consumption/effects,
desktop mirror, controller actions, haptics, and composition-layer submission.

### Settings and menu

- `src/settings/UserSettings.*`
- `src/stereo/SettingsMenuInteraction.*`
- `src/client/SettingsMenuArt.*`
- `assets/SettingsMenu/`

`UserConfig.txt` is versioned, documented, and written atomically. Installer
updates preserve an existing user file. New keys require a safe default,
decode/encode coverage, menu wiring where applicable, and persistence tests.

The potential 1.0.2 settings group adds these runtime invariants:

- `show_arms` is a migration-compatible three-state visibility setting:
  `arms_and_hands`, `hands_only`, or `no_hands_or_arms`. Hands Only retains
  only game-selected mesh templates with explicit left/right-hand names;
  combined and unrecognized meshes fail closed to hidden. Tracking, native
  animation, controller weapon transforms, hand placement, and elbow solving
  continue while draws are hidden. Do not treat a narrow projection alone as
  ownership during an active or entering scope: magnified world soldiers
  satisfy the same lower bounds, so suppression must fail closed there.
- Hand weapons, mounted guns, and controller-pointer knife/throwable/gadget
  items each own an independent `WorldCrosshairMode`. Crosshair color is a
  shared base tint inside the existing D3D8 per-eye renderer; do not move this
  path into an OpenXR overlay merely to isolate it from grading.
- `hapticDeathSequence` and `localPlayerLifeState` share the verified
  local-player observer. Shared protocol version 21 lets the presenter start
  a bounded death effect and cancel it on a verified respawn.
- Movement and death comfort are two targets of one `OpenXRComfortVignette`.
  Death styling takes priority inside that compositor, which remains above
  the stereo world and below Ref2/interface layers.
- Color profile, exposure, contrast, and saturation are fused into the final
  world scaler shader after AO/SSGI/reflections/bloom. Both world eyes always
  retain that pass for live changes. Ref2 is passed neutral color settings;
  separately composed UI never enters the shader.

## Critical compatibility invariants

These are established behaviors, not cleanup opportunities.

### BF42++

- BFVR requires compatible BF42++ but does not bundle it.
- A separate installation exposes `bf42++.dll`; BFVR injects that library
  before its own client.
- Some packages bundle BF42++ as a DirectSound proxy named `dsound.dll`.
  BFVR recognizes the proxy structurally and leaves it on its natural loading
  path.
- If both forms exist, the recognized bundled proxy wins and BFVR must not
  inject the second DLL.
- Obsolete BF42Plus 1.3.4 remains a distinct blocked security case.

### Battlefield executables

- Unknown executable hashes produce a warning, not a launcher block.
- Game-internal features must locate supported code through evidence-backed
  signatures or validated relationships and fail closed when evidence does not
  match.
- Do not generalize one address across builds without matching evidence.

### Renderer packages

- Community installations may preload dgVoodoo or DXVK/Vulkan D3D DLLs.
- BFVR redirects the game to its pinned translator and isolates the translator's
  D3D9 dependency from package-local `d3d9.dll`.
- Do not rename, overwrite, or permanently remove a package's renderer files.
  Ordinary non-VR launches must remain unchanged.

### OpenXR

- Request OpenXR API 1.0. Newer headers do not justify requesting a newer API
  version; SteamVR and VDXR compatibility depends on the 1.0 request.
- `XR_KHR_D3D11_enable` is the mandatory graphics extension.
- The runtime selects the HMD adapter and swapchain sizes. Battlefield desktop
  resolution is not a hard-coded eye resolution requirement.

### Shared rendering and pacing

- Use the translator's current post-Reset presentation dimensions rather than
  stale initial CreateDevice dimensions.
- Keep producer ownership until the x64 consumer's queued source work is safe.
- The SteamVR pacing correction intentionally overlaps legacy shared-resource
  GPU completion with `xrEndFrame`; do not restore the former serial wait.
- UI capture policies distinguish full replacement frames from accumulating
  HUD draws. Combining them blindly can create trails or repeated translucent
  elements.

### Frame limiter

- BFVR disables BF1942's internal `lockFps` value only inside the BFVR-launched
  process.
- The override is signature-backed, validates the live owner/value memory, and
  periodically reasserts `-1.0f` from the Present path.
- Do not edit `VideoDefault.con`; that would affect ordinary flat launches and
  could leave user files altered after a failure.

### Diagnostics

- `BFVR_DIAGNOSTICS=off` is the player default and must have no periodic file
  logging or expensive proof collection.
- `normal` enables bounded development summaries.
- `deep` enables expensive evidence probes for focused investigations.
- Required rendering, state restoration, and compatibility work is not a
  diagnostic and remains active when diagnostics are off.

## Controls

OpenXR input is normalized in the x64 presenter and converted to recovered
Battlefield logical input in the Win32 client. Surface vehicles and aircraft
are separate control branches.

The default aircraft layout is:

- Left stick: throttle and roll.
- Right stick: pitch and yaw.
- `Aircraft Pitch + Roll on Same Stick`: off.
- `Swap Aircraft Sticks`: off.

The two aircraft options apply only to the recovered `VCAir` category. They do
not change infantry, ground vehicles, boats, turrets, or mounted weapons.

SteamVR binding changes are runtime-local. Meta OpenXR and VDXR use BFVR's
suggested OpenXR bindings, not a user's SteamVR-only customization.

## Currently validated compatibility

Base-package tests include:

- The development/HD Battlefield installation.
- An Anthology non-Vulkan/dgVoodoo installation.
- The Moongamers dgVoodoo package with bundled BF42++ proxy.
- An Anthology VK/Vulkan installation with separately installed BF42++.

Runtime tests include:

- Meta Quest Link / Meta OpenXR.
- SteamVR OpenXR.
- Virtual Desktop through VDXR.
- Virtual Desktop through SteamVR mode.

This matrix proves those tested combinations only. It is not permission to
claim that every executable, headset, controller, mod, or runtime works.

## Build and test

Follow `docs/DEVELOPMENT.md`. The required acceptance floor for a source change
is:

1. Configure and build the Win32 tree.
2. Run the complete Win32 `ctest` suite with zero failures.
3. Configure and build the x64 presenter tree.
4. Run targeted no-HMD probes when the changed subsystem has one.
5. Perform a physical headset test for presentation, controls, runtime, or
   package-compatibility changes.

Build success does not prove headset behavior. Record exactly which package,
OpenXR runtime, headset, and test path supplied a compatibility result.

## Release procedure

For a release candidate:

1. Update version metadata, `CHANGELOG.md`, player documents, and installer
   definitions.
2. Build clean Win32 and x64 release artifacts.
3. Run all automated tests.
4. Stage only `installer/player-manifest.txt` through
   `tools/Stage-BFVRPlayer.ps1`.
5. Build the unsigned Inno Setup installer.
6. Install that exact installer into a clean supported Battlefield copy.
7. Test startup, menus, single-player, multiplayer, firing, settings, VR
   shutdown, uninstall, and preservation/removal of user configuration.
8. Publish the exact tested installer and its SHA-256 checksum with the matching
   source tag.

Never substitute a rebuilt binary after installer testing without repeating
the installer test.

## AI-agent workflow

An AI agent continuing this project should:

1. Inspect `git status` before editing and preserve unrelated user changes.
2. Identify the owning subsystem and read its tests before modifying code.
3. Separate observed evidence from interpretation.
4. Use pure helper policies and deterministic tests for new routing, math, or
   compatibility decisions.
5. Prefer the smallest reversible change that preserves the invariants above.
6. Update this handoff when a confirmed result changes an architectural rule.
7. Update `CHANGELOG.md` only for user-visible behavior.
8. Never claim physical compatibility from static analysis or compilation.

When historical notes conflict, use this priority order:

1. Current passing source and tests.
2. Current logs from a controlled test.
3. This handoff and `docs/DEVELOPMENT.md`.
4. `devREADME.md` historical record.
5. The older external reverse-engineering repository.
