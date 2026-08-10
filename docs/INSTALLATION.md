# Installing BFVR v1.0.0

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
game process when a map starts. BF42++ is maintained and downloaded separately;
it is not included in the BFVR installer.

1. Download BF42++ 2.0 RC6, or a newer compatible release, from the
   [official BF42++ page](https://www.moddb.com/games/battlefield-1942/addons/bf42plusplus-v2-0-rc6).
2. Extract all BF42++ installation files into the folder containing
   `BF1942.exe`. Keep their original names, including `bf42++.exe`,
   `bf42++.dll`, and `bf42++BlackScreen.exe`.
3. Start `bf42++.exe` once by itself to confirm BF42++ works, then exit the
   game. When using VR afterward, start `BFVR.exe`; BFVR performs BF42++'s DLL
   loading step before it loads BFVR.

Do not rename `bf42++.dll` to `dsound.dll`. Remove obsolete BF42Plus 1.3.4 if
it left an old `dsound.dll` in the game folder. If another current mod owns a
`dsound.dll`, consult that mod's compatibility instructions.

## Install with the setup program

1. Download `BFVR-Setup-v1.0.0.exe` from the official v1.0.0 GitHub Release.
2. Double-click the downloaded installer.
3. Because the installer is unsigned, Windows may say **Unknown publisher**.
   If SmartScreen appears, choose **More info**, verify that the filename and
   published SHA-256 checksum match the official release, and then choose
   **Run anyway**.
4. Select your Battlefield 1942 folder. This is the folder containing
   `BF1942.exe` and the three BF42++ program files. The installer creates a new
   `BFVR` folder inside it and stops with instructions if BF42++ is incomplete.
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
