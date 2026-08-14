# BFVR User Guide

## Starting and stopping

Connect the headset and start its OpenXR software first, then launch `BFVR.exe`.
Exit through Battlefield 1942's normal menus. BFVR and its VR presenter stop
when the game session ends.

Mouse and keyboard remain available, including in menus.

## Default Touch-style controls

- **Left stick:** Move. Its click retains the game's contextual vehicle action.
- **Right stick left/right:** Turn. Its click retains the other contextual
  vehicle action.
- **Right stick up:** Jump and parachute.
- **Right stick down:** Toggle crouch.
- **Right trigger:** Fire or click a menu item.
- **Right grip:** Aim down sights or use the game's alternate-fire action.
- **Left trigger:** Use/interact while held.
- **Left grip:** Hold the weapon with two hands when the weapon supports it.
- **Right A:** Hold to open the Quick Menu, point at a choice, then release A
  to select it. Releasing away from the menu cancels.
- **Right B:** Reload. Hold for 2.5 seconds to recenter forward.
- **Left X:** Prone.
- **Left Y:** Hold the scoreboard.
- **Left Menu:** Toggle the map.

Keyboard controls continue to work. Vehicle, aircraft, turret, and mounted-gun
stick behavior follows the vehicle being controlled.

## Valve Index and Vive controllers

BFVR includes OpenXR bindings for Meta/Oculus Touch, Valve Index, HTC Vive
Wands, and the OpenXR simple-controller profile.

On Index controllers, A/B provide the corresponding face-button actions. Index
has no default Map button, but the named **Map** action can be assigned through
SteamVR's controller-binding screen.

Vive Wands use their trackpads for movement and turning. Left Menu opens the
map and right Menu opens the Quick Menu. The limited Wand button count leaves
Prone, Scoreboard, and Reload unassigned by default; SteamVR may remap BFVR's
named actions to preferred controls.

## Quick Menu and VR Settings

Hold right A to open the Quick Menu. Its main choices send Battlefield 1942's
normal Escape, Enter, number-key, and camera-key actions. The strip below it
contains mounted-camera decoupling, kit swapping, and the persistent VR
Settings panel.

VR Settings includes:

- Seated/Standing play mode.
- Snap/Smooth turning, turn speed, and movement direction.
- A presentation page that places Comfort Vignette and the default-on dark-red
  Death Camera Comfort effect together, followed by Show and 3D Crosshair
  Color.
- A physical-calibration page with manual height adjustment, standing-height
  calibration, and Recenter Forward.
- A live **Show** selector with Arms & Hands, Hands Only, and No Hands/Arms.
  Hands Only retains explicitly named separate hand meshes while hiding arm,
  combined, and unrecognized meshes. Mods that combine hands and arms cannot
  be split, so their combined mesh is hidden in Hands Only. Animation,
  controller hand/elbow placement, and weapon pose solving stay active in all
  modes, and scoped world characters remain fully visible.
- Vehicle/aircraft aim inversion.
- Two aircraft layout options placed directly below Flight Pitch. `Aircraft
  Pitch + Roll on Same Stick` chooses whether pitch shares a stick with roll
  instead of yaw.
  `Swap Aircraft Sticks` moves that complete pitch stick between the right and
  left controller. Together they allow pitch/yaw or pitch/roll on either stick.
  They work with every controller profile BFVR accepts because they rearrange
  aircraft axes after controller input is read, rather than depending on a
  Quest-specific button layout.
- Controller vibration and two-hand grip style.
- Hand-weapon, mounted-weapon, and knives/throwables/gadgets crosshair choices,
  each with On, Hit Marker Only, and Off modes.
- White, green, blue, purple, red, pink, orange, and yellow 3D crosshair
  colors. The same color is used for the hit marker, including BF1942's native
  hit feedback while looking through an exact scope. The original flat-game
  crosshair color is restored when the scope closes.
- FXAA, sharpening, ambient occlusion, water reflections, and bloom.
- A second Graphics page containing Original, Filmic, and Vibrant world color
  profiles plus exposure, contrast, and saturation controls. Zero is neutral
  at the center of each manual slider. These settings affect the stereo world
  and its 3D crosshair, but not the native HUD, menus, scopes, Quick Menu, or
  VR Settings panel.
- A third Graphics / Audio page with a default-on **Kill Sound** toggle. It
  plays only after Battlefield confirms that the local player killed another
  player, in either single-player or multiplayer. Grenade-style kills confirmed
  within 300 ms play once as one multi-kill burst; later kill sounds can still
  overlap rather than cutting off the previous sound. Windows/headset output volume applies;
  Battlefield's private master-volume slider does not currently scale this
  custom WAV.

The Quick Menu, VR Settings controls, and BFVR's Back-to-Game button use
Battlefield's normal menu highlight, confirm, and cancel sounds. These sounds
follow the active game or mod's native menu-audio setup and are always enabled
in the same way as Battlefield's own menu feedback.

Choose **Save** to apply changes. Settings involving startup graphics resources
say that a BFVR restart is required. **Cancel** discards unsaved changes, and
**Defaults** loads the release defaults into the menu until Save is chosen.

`UserConfig.txt` is stored beside `BFVR.exe`. It contains explanations for all
settings and may be edited with BFVR closed. Deleting it makes BFVR create a
fresh file containing the release defaults on the next start.

## Troubleshooting

### BFVR says BF42++ is required

If the game package does not already contain a BF42++ `dsound.dll` proxy,
download BF42++ separately and install it beside `BF1942.exe` using the
official package with its original filenames. See the BF42++ section of the
[Installation Guide](INSTALLATION.md). For VR, start `BFVR.exe`; it selects
one usable BF42++ loading path before loading BFVR into Battlefield 1942.

Do not combine a bundled BF42++ `dsound.dll` with a second copied
`bf42++.dll`. BFVR recognizes a bundled BF42++ proxy and uses that one directly
to avoid loading BF42++ twice. If both are already present, BFVR 1.0.1 ignores
the extra `bf42++.dll` for that launch, so the duplicate should not break BFVR.

If BFVR reports obsolete BF42Plus 1.3.4, remove that old `dsound.dll` and
install current BF42++ from its official page. That warning is a security
check, not a claim that the computer has already been compromised.

### The headset does not enter VR

Confirm that the headset is connected and that the intended OpenXR runtime is
active. SteamVR users should check SteamVR's desktop **Settings > OpenXR** page.
Meta users should check the OpenXR setting in the Meta Quest Link PC app.
Virtual Desktop users should select **VDXR/VirtualDesktopXR** in Virtual
Desktop Streamer and confirm that the headset overlay says `Runtime: VDXR`.

The presenter log now records the active runtime's name and version. For a
VDXR failure, also collect `%ProgramData%\Virtual Desktop\OpenXR.log`.

### BFVR warns that the BF1942.exe build is unfamiliar

The warning does not block the game. BFVR will try to start it. Battlefield
1942 has several retail, digital, and community-modified executables, and some
may differ at internal locations BFVR uses. If the build works, report that
result so it can be recorded. If it fails, include where the executable came
from and exactly what happened in a GitHub issue.

### Windows or antivirus warns about BFVR

The v1.0.1 installer is unsigned, and BFVR must load its DLL into an old game
process. Only use files from the official BFVR GitHub Release and compare the
published checksum. Do not disable antivirus globally.

### BFVR says elevation is required

This normally means Windows is configured to run `BF1942.exe` as administrator.
Close it, then right-click `BFVR.exe` and choose **Run as administrator**. Do
not change compatibility settings unless you understand why they were added.

### Reset the settings

Close BFVR and delete only `Battlefield 1942\BFVR\UserConfig.txt`. BFVR will
create a clean default copy on the next launch.

### Reporting a problem

Include the headset, graphics card, active OpenXR runtime, where the game
executable came from, map/mod, and a short list of steps that reproduces the
issue. Never upload Battlefield game files.

## Multiplayer

BFVR preserves Battlefield 1942's normal simulation and networking paths, but
it is still a client-side executable mod. Use it only on servers whose rules
permit client mods. BFVR does not bypass anti-cheat or server restrictions.
