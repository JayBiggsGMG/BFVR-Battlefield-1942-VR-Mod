# BFVRLauncher foundation

This is the first executable component of BFVR.  It is deliberately a
no-attach launcher: it verifies an exact build profile and may start the
unmodified game in flat mode.  It cannot load a BFVR client, inject a module,
replace a DLL, patch an executable, or write game files.

## Build

The current source has no external package dependencies and builds with the
locally installed .NET 8 SDK:

```powershell
dotnet build .\BFVR\src\BFVRLauncher\BFVRLauncher.csproj --configuration Release
```

During source-tree development, pass explicit paths because the output is not
yet in the release `BFVR` directory:

```powershell
dotnet .\BFVR\src\BFVRLauncher\bin\Release\net8.0\BFVRLauncher.dll --verify `
  --game-root 'F:\Battlefield 1942' `
  --profile 'F:\Battlefield 1942\BFVR\profiles\bf1942-win32-decbb52f.json'
```

## Commands

- `--verify` is the default action. It checks the executable and profiled root
  modules, reporting unexpected DLLs as unvalidated.
- `--launch-flat -- <game arguments>` starts the verified original client with
  no BFVR module loaded.
- `--enable-vr` intentionally fails until a separately validated runtime client
  and safe loader path exist.

The future release build will place `BFVRLauncher.exe` directly in `BFVR` and
ship all required runtime dependencies in that same parent folder.
