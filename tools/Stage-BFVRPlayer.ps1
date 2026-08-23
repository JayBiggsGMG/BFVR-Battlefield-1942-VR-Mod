[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$Win32Build,

    [Parameter(Mandatory)]
    [string]$X64Build,

    [Parameter(Mandatory)]
    [string]$Destination
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$sourceRoot = (Resolve-Path -LiteralPath (Split-Path -Parent $PSScriptRoot)).Path
$win32Root = (Resolve-Path -LiteralPath $Win32Build).Path
$x64Root = (Resolve-Path -LiteralPath $X64Build).Path
$destinationPath = [System.IO.Path]::GetFullPath($Destination)

if (Test-Path -LiteralPath $destinationPath) {
    throw "The player staging destination already exists and will not be overwritten: $destinationPath"
}

$requiredFiles = @(
    (Join-Path $win32Root 'BFVR.exe'),
    (Join-Path $win32Root 'BFVRClient.dll'),
    (Join-Path $win32Root 'BFVRD3D8To9.dll'),
    (Join-Path $win32Root 'BFVRUserSettingsSeedWriter.exe'),
    (Join-Path $x64Root 'BFVRPresenter.exe'),
    (Join-Path $x64Root 'runtime\openxr\win64\openxr_loader.dll'),
    (Join-Path $sourceRoot 'README.md'),
    (Join-Path $sourceRoot 'assets\BF42VRlogo.png'),
    (Join-Path $sourceRoot 'docs\INSTALLATION.md'),
    (Join-Path $sourceRoot 'docs\USER_GUIDE.md'),
    (Join-Path $sourceRoot 'THIRD_PARTY_NOTICES.md'),
    (Join-Path $sourceRoot 'licenses\OpenXR-Loader-1.1.61.txt'),
    (Join-Path $sourceRoot 'third_party\d3d8to9-1.15.1\LICENSE.md'),
    (Join-Path $sourceRoot 'third_party\minhook-1.3.4\LICENSE.txt')
)
foreach ($requiredFile in $requiredFiles) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "A required player source file is missing: $requiredFile"
    }
}
if (-not (Test-Path -LiteralPath (Join-Path $sourceRoot 'assets') -PathType Container)) {
    throw "The canonical BFVR asset directory is missing: $sourceRoot\assets"
}

New-Item -ItemType Directory -Path $destinationPath | Out-Null
New-Item -ItemType Directory -Path (Join-Path $destinationPath 'assets') | Out-Null
New-Item -ItemType Directory -Path (Join-Path $destinationPath 'docs') | Out-Null
New-Item -ItemType Directory -Path (Join-Path $destinationPath 'runtime\openxr\win64') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $destinationPath 'licenses') | Out-Null

Copy-Item -LiteralPath (Join-Path $win32Root 'BFVR.exe') -Destination $destinationPath
Copy-Item -LiteralPath (Join-Path $win32Root 'BFVRClient.dll') -Destination $destinationPath
Copy-Item -LiteralPath (Join-Path $win32Root 'BFVRD3D8To9.dll') -Destination $destinationPath
Copy-Item -LiteralPath (Join-Path $x64Root 'BFVRPresenter.exe') -Destination $destinationPath
Copy-Item -Path (Join-Path $sourceRoot 'assets\*') -Destination (Join-Path $destinationPath 'assets') -Recurse
Copy-Item -LiteralPath (Join-Path $x64Root 'runtime\openxr\win64\openxr_loader.dll') -Destination (Join-Path $destinationPath 'runtime\openxr\win64')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'README.md') -Destination $destinationPath
Copy-Item -LiteralPath (Join-Path $sourceRoot 'docs\INSTALLATION.md') -Destination (Join-Path $destinationPath 'docs')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'docs\USER_GUIDE.md') -Destination (Join-Path $destinationPath 'docs')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'THIRD_PARTY_NOTICES.md') -Destination $destinationPath
Copy-Item -LiteralPath (Join-Path $sourceRoot 'licenses\OpenXR-Loader-1.1.61.txt') -Destination (Join-Path $destinationPath 'licenses')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'third_party\d3d8to9-1.15.1\LICENSE.md') -Destination (Join-Path $destinationPath 'licenses\d3d8to9-LICENSE.md')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'third_party\minhook-1.3.4\LICENSE.txt') -Destination (Join-Path $destinationPath 'licenses\MinHook-LICENSE.txt')

$userConfigPath = Join-Path $destinationPath 'UserConfig.txt'
& (Join-Path $win32Root 'BFVRUserSettingsSeedWriter.exe') $userConfigPath
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $userConfigPath -PathType Leaf)) {
    throw 'The BFVR settings seed writer did not create UserConfig.txt.'
}

$files = Get-ChildItem -LiteralPath $destinationPath -Recurse -File
$totalBytes = ($files | Measure-Object -Property Length -Sum).Sum
Write-Host ('Staged BFVR player payload: {0} files, {1} bytes' -f $files.Count, $totalBytes)
Write-Host $destinationPath
