# Installing BFVR v1.0.1

This guide assumes no modding or programming experience.

## Before installing

1. Make sure ordinary Battlefield 1942 starts successfully without BFVR.
2. Install BF42++ as described below.
3. Close Battlefield 1942 and any older BFVR programs.
4. Connect your headset and make sure its normal PC VR software works.

BFVR requires 64-bit Windows 10 or Windows 11. It does not include Battlefield
1942 or any Battlefield game files.

## Install BF42++ first

BFVR currently requires BF42++ to keep Battlefield 1942 in the same working
game process when a map starts. BF42++ is maintained separately and is not
included in the BFVR installer.

First look in the folder containing `BF1942.exe`:

- If it already contains `dsound.dll`, the game package may already include a
  BF42++ proxy. Do not add another BF42++ copy yet. Install BFVR and let its
  launcher inspect that file safely.
- If it does not contain a bundled BF42++ proxy, install the official package
  as described below.

1. Download BF42++ 2.0 RC6, or a newer compatible release, from the
   [official BF42++ page](https://www.moddb.com/games/battlefield-1942/addons/bf42plusplus-v2-0-rc6).
2. Extract all BF42++ installation files into the folder containing
   `BF1942.exe`. Keep their original names, including `bf42++.exe`,
   `bf42++.dll`, and `bf42++BlackScreen.exe`.
3. Start `bf42++.exe` once by itself to confirm BF42++ works, then exit the
   game. When using VR afterward, start `BFVR.exe`; BFVR performs BF42++'s DLL
   loading step before it loads BFVR.

Do not rename `bf42++.dll` yourself. Some community game packages deliberately
ship BF42++ as `dsound.dll`; BFVR can recognize that proxy structurally and use
it without loading a second BF42++ copy. An unfamiliar future proxy is allowed
with a warning rather than blocked by an exact file-version check. The one
specifically known obsolete BF42Plus 1.3.4 `dsound.dll` remains blocked because
its abandoned updater is unsafe.

If both a recognized bundled BF42++ `dsound.dll` and a separately copied
`bf42++.dll` are present, BFVR 1.0.1 prioritizes the bundled proxy and does not
inject the extra DLL. This prevents the duplicate from breaking BFVR, but the
extra BF42++ files are unnecessary and can make ordinary non-VR troubleshooting
more confusing. The simplest setup is to keep only the BF42++ form originally
provided by the game package.

## Install with the setup program

1. Download `BFVR-Setup-v1.0.1.exe` from the official v1.0.1 GitHub Release.
2. Double-click the downloaded installer.
3. Because the installer is unsigned, Windows may say **Unknown publisher**.
   If SmartScreen appears, choose **More info**, verify that the filename and
   published SHA-256 checksum match the official release, and then choose
   **Run anyway**.
4. Select your Battlefield 1942 folder. This is the folder containing
   `BF1942.exe`. The installer creates a new `BFVR` folder inside it. If Setup
   cannot see a separate `bf42++.dll`, it explains what it found but does not
   block installation; the BFVR launcher performs the more careful proxy check.
5. Optionally create a desktop shortcut, then finish the installation.

The installer confirms that the selected folder contains `BF1942.exe`, but it
does not restrict the executable to one exact version. Battlefield 1942 has
several retail, digital, and community-modified executables. Some may require
additional compatibility work because BFVR connects to internal game code.

## Select the OpenXR runtime

OpenXR lets one BFVR build work with different headset systems. Windows has one
active OpenXR runtime at a time, and BFVR automatically uses it. There is no
`bfvr.toml` file and no separate SteamVR edition.

### Meta Quest Link or Air Link

Open the Meta Quest Link PC application and make the Meta/Oculus runtime active
in its OpenXR settings if it is not already active. Connect Link or Air Link
before starting BFVR.

### SteamVR

Start SteamVR and connect the headset. In the small SteamVR desktop window,
open the menu, choose **Settings**, then **OpenXR**. If SteamVR is not shown as
the current OpenXR runtime, choose **Set SteamVR as OpenXR Runtime**.

The OpenXR loader always uses the currently active runtime; this selection is
owned by the headset software, not BFVR. See the
[Khronos OpenXR loader documentation](https://registry.khronos.org/OpenXR/specs/1.1/loader.html)
for the underlying standard behavior.

### Virtual Desktop (Quest/Pico)

1. Open **Virtual Desktop Streamer** on the PC.
2. In its OpenXR runtime setting, select **VDXR** or
   **VirtualDesktopXR** instead of SteamVR.
3. Connect to the PC from the Virtual Desktop headset application.
4. Start `BFVR.exe` from the connected PC desktop.

Virtual Desktop's own overlay should show `Runtime: VDXR`. BFVR asks for
OpenXR 1.0 and D3D11, which match VDXR's published implementation. If startup
fails, include BFVR's presenter log and Virtual Desktop's
`%ProgramData%\Virtual Desktop\OpenXR.log` in the report. A SteamVR-through-
Virtual-Desktop route may also run, but native VDXR avoids the extra SteamVR
layer and is the preferred test path.

## Start BFVR

1. Start and connect your headset software.
2. Double-click the installed **BFVR** shortcut, or open the game's `BFVR`
   folder and double-click `BFVR.exe`.
3. Do not start `BF1942.exe` separately. BFVR starts and manages it.
4. Use Battlefield 1942's normal menu to load a map.

BFVR temporarily skips the old Bink intro movie because that renderer path is
not VR-compatible. The movie is restored when the game exits. BFVR does not
overwrite it.

## Updating

Close BFVR, run the newer installer, and choose the same game folder. The
installer replaces BFVR program files but preserves your `UserConfig.txt`.

## Uninstalling

Open Windows **Installed apps** (or **Apps & features**), select **BFVR**, and
choose **Uninstall**. The uninstaller asks whether to remove your saved BFVR
settings. It removes BFVR files and shortcuts without removing Battlefield
1942, BF42++, or any game data.
