# BFVR Development

The GitHub repository contains the full BFVR source, deterministic tests,
third-party source required by the build, and developer diagnostic tools. The
player installer contains none of those development-only files.

## Build requirements

- Windows 10 or Windows 11, 64-bit.
- Visual Studio 2022 Build Tools with C++ and a Windows SDK.
- CMake 3.24 or newer.
- An x86 build environment for the client/launcher and an x64 environment for
  the OpenXR presenter.

The project uses pinned OpenXR, MinHook, and d3d8to9 source already present in
`third_party` and `runtime`.

## Build the x86 client and launcher

From an x86 Visual Studio developer prompt:

```powershell
cmake -S .\src -B ..\build\bfvr-win32 -G "NMake Makefiles" `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build ..\build\bfvr-win32
ctest --test-dir ..\build\bfvr-win32 --output-on-failure
```

## Build the x64 presenter

From an x64 Visual Studio developer prompt:

```powershell
cmake -S .\src -B ..\build\bfvr-x64 -G "NMake Makefiles" `
  -DCMAKE_BUILD_TYPE=Release -DBFVR_PRESENTER_ONLY=ON
cmake --build ..\build\bfvr-x64 --target BFVRPresenter
```

## Runtime assets

`assets/` in the BFVR source checkout is the canonical runtime asset tree.
Building `BFVRClient` or `BFVRPresenter` always synchronizes that complete tree
into the selected target directory, even when only an asset changed and the
native target itself is already up to date.

The outer checkout's `Launch-BFVR-VR.bat` is restricted to the
`WorkInProgress` branch and mirrors the canonical tree into its local launch
payload before starting the game. That developer convenience does not commit,
merge, tag, publish, or modify either remote branch.

## Diagnostics

`BFVR_DIAGNOSTICS` has three values:

- `off`: player-performance mode. No observer log, proof traces, periodic
  samplers, or CPU/GPU performance timing.
- `normal`: useful development logs and summaries.
- `deep`: expensive proof/verification paths used for focused investigations.

Rendering, state restoration, and startup device discovery required for VR are
not disabled by `off`.

The native `BFVR.exe` retains the explicit diagnostic command-line options.
Separate probe/test executables are development build products and must not be
placed in a player package. The outer working checkout's
`Launch-BFVR-VR.bat` is a development convenience only.

The long-form investigation record remains in [devREADME.md](../devREADME.md).

## One-run interop diagnostic

For a user report involving OpenXR startup or D3D9Ex/D3D11 shared textures,
build both architectures and stage a separate diagnostic folder:

```powershell
.\tools\Stage-BFVRInteropDiagnostic.ps1 `
  -Win32Build ..\build\bfvr-release-win32\RelWithDebInfo `
  -X64Build ..\build\bfvr-presenter-x64\Release `
  -Destination ..\build\bfvr-interop-diagnostic\BFVR

.\tools\Package-BFVRInteropDiagnosticBuild.ps1 `
  -StagedBfvr ..\build\bfvr-interop-diagnostic\BFVR `
  -Destination ..\build\BFVR-interop-diagnostic.zip
```

The archive extracts as a separate `BFVR-InteropDiagnostic` folder beside the
existing `BFVR` installation. The user launches its
`Run-BFVRInteropDiagnostic.cmd` once, reproduces the issue, and exits BF1942
normally. The wrapper creates `BFVR-interop-diagnostic.zip` beside the game;
that archive contains only the loader, producer, presenter, and BF42++ logs
plus a SHA-256 manifest. The diagnostic install uses its own seeded settings
file and does not replace the user's existing BFVR files or configuration.

## Stage the player payload

Build both architectures and the settings seed writer, then run:

```powershell
.\tools\Stage-BFVRPlayer.ps1 `
  -Win32Build ..\build\bfvr-win32 `
  -X64Build ..\build\bfvr-x64\Release `
  -Destination ..\build\bfvr-player-v1.0.1\BFVR
```

The staging script refuses to overwrite an existing destination, copies the
runtime asset tree directly from canonical `assets/`, and emits only the
explicit player manifest.
