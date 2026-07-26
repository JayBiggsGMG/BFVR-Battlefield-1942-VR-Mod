# BFVR safety and compatibility policy

BFVR is an optional, client-side presentation mod.  Its release package must
be removable by deleting the single `BFVR` folder beside `BF1942.exe`.

## Installation boundary

- BFVR must never replace `BF1942.exe`, a game DLL, game data, or an existing
  wrapper/overlay.
- A launcher may read the sibling game directory to identify the executable and
  installed modules, but it may write only under `BFVR` (for example, its log
  and configuration directories).
- An unknown executable hash or unresolved module conflict is a no-attach
  result, not a reason to overwrite or patch files.
- The portable ZIP is the primary artifact.  Any optional installer may create
  the same `BFVR` folder and shortcuts only.

## Existing community fixes

This checkout has observation-only signals of existing modifications: a
ReShade `dxgi.dll`, a local `dsound.dll` with a preserved `.oldversion` sibling,
and `bf42plus.ini`.  These facts do not identify the purpose, compatibility, or
load order of those modules.

The current executable also has two externally configured Windows compatibility
layers: `~ WIN95` in the current-user compatibility registry entry and
`NT4SP5 RUNASADMIN` in the machine-wide entry. A reversible 15-second normal
launch without both layers allowed BFVR's in-process x86 OpenXR bootstrap to
create an Oculus runtime instance; the same bootstrap returns
`XR_ERROR_RUNTIME_UNAVAILABLE` with the combination present. BFVR detects this
known combination and keeps the game flat with a clear log. It never alters
compatibility settings. In this development environment, the owner explicitly
chose to remove both entries permanently after the reversible test; a normal
90-second launch then reconfirmed the in-process headless OpenXR path. The test
does not identify whether Win95 mode, the NT4SP5/admin layer, or their
interaction is responsible. Any future test or user action that changes either
setting requires explicit user direction.

BFVR must test one existing rendering/input/audio layer at a time, publish the
result for the exact executable and module hashes, and leave every non-BFVR
file untouched.  Widescreen, Alt-Tab, audio, renderer, and overlay fixes are
all treated as potentially significant until tested.

## Safe development and test scope

- Use offline, local, or explicitly mod-approved servers while development is
  underway.
- Do not attempt to bypass anti-cheat, server checks, or protected multiplayer
  environments.
- Keep VR pose, stereo rendering, HUD composition, and controller presentation
  client-side; preserve the game's normal input and simulation contract.
- A failure to initialize OpenXR or a device-lifecycle event must leave a
  readable log and preserve a flat-screen fallback.
- The standalone OpenXR presentation probe uses only BFVR-owned D3D11 test
  textures and the exact runtime-selected adapter. It neither loads BF1942 nor
  falls back to a different adapter; successful headset output from that probe
  is required before considering any D3D8-client presentation hookup.
- The x64 companion receives no game pointer or D3D8 interface. Its versioned
  pointer-free mapping names three BFVR-created D3D11 resources, and it opens
  only those names on the runtime-selected adapter. Keyed-mutex waits are
  bounded, the x86 diagnostic producer owns the child process, and both sides
  release their mapping, handles, textures, and devices on normal or failed
  startup.

## Per-build acceptance record

Before BFVR can attach to a build, its profile must include:

- [ ] `BF1942.exe` hash, PE identity, and launch procedure.
- [ ] Complete local/offline regression map and reproducible test sequence.
- [ ] Root module inventory and explicit coexistence result for every detected
      non-stock layer.
- [ ] Validated D3D8 device creation, reset, and present lifecycle.
- [ ] Flat fallback, clean shutdown, and removal validation.

## Reporting a conflict

Keep the original installation unchanged.  Save the BFVR log, executable hash,
module inventory, headset/OpenXR runtime details, launch options, and a short
reproduction sequence.  Do not include redistributed game assets in a report.
