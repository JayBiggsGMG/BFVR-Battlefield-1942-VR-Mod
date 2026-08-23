# Building the BFVR installer

1. Build the Win32 client/launcher and x64 presenter.
2. Run `tools\Stage-BFVRPlayer.ps1` into the default payload directory shown
   in `BFVR.iss`, or pass another payload with `/DPayloadRoot=...`.
3. Compile `BFVR.iss` with Inno Setup 6.7.3 or a compatible later release.

Example:

```powershell
& 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe' `
  '/DPayloadRoot=F:\path\to\BFVR' `
  '/DOutputRoot=F:\path\to\release' `
  '.\installer\BFVR.iss'
```

The setup and uninstaller are intentionally unsigned. Do not configure a
signing tool. The exact final installer must be installed, launched, and
uninstalled on a clean supported Battlefield 1942 copy before publication.

BFVR versions use one stable Inno Setup `AppId`. When Setup finds an older
registered BFVR installation, it selects that exact directory and shows a
dedicated update page before continuing. When the current version is already
installed, Setup instead offers Repair/reinstall and Uninstall maintenance
choices. Updating or repairing replaces program files and player guides while
preserving the existing `UserConfig.txt`. Release validation must cover a fresh
install, an upgrade from the prior public installer, and a same-version repair
before the final setup executable is published.
