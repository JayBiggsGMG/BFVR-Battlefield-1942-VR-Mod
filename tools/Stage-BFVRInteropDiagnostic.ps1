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

$destinationPath = [System.IO.Path]::GetFullPath($Destination)
& (Join-Path $PSScriptRoot 'Stage-BFVRPlayer.ps1') `
    -Win32Build $Win32Build `
    -X64Build $X64Build `
    -Destination $destinationPath

$toolsPath = Join-Path $destinationPath 'tools'
New-Item -ItemType Directory -Path $toolsPath -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Run-BFVRInteropDiagnostic.cmd') -Destination $destinationPath
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Package-BFVRInteropDiagnostic.ps1') -Destination $toolsPath

Write-Host 'Staged the opt-in BFVR one-run interop diagnostic launcher.'
Write-Host (Join-Path $destinationPath 'Run-BFVRInteropDiagnostic.cmd')
