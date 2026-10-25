[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$StagedBfvr,

    [Parameter(Mandatory)]
    [string]$Destination
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$sourcePath = (Resolve-Path -LiteralPath $StagedBfvr).Path
$destinationPath = [System.IO.Path]::GetFullPath($Destination)
if (Test-Path -LiteralPath $destinationPath) {
    throw "The diagnostic archive already exists and will not be overwritten: $destinationPath"
}

$requiredFiles = @(
    'BFVR.exe',
    'BFVRClient.dll',
    'BFVRD3D8To9.dll',
    'BFVRPresenter.exe',
    'runtime\openxr\win64\openxr_loader.dll',
    'Run-BFVRInteropDiagnostic.cmd',
    'tools\Package-BFVRInteropDiagnostic.ps1',
    'UserConfig.txt'
)
foreach ($relativePath in $requiredFiles) {
    $candidate = Join-Path $sourcePath $relativePath
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw "A required diagnostic payload file is missing: $candidate"
    }
}

$tempRoot = Join-Path $env:TEMP ("BFVR-interop-payload-{0}-{1}" -f $PID, (Get-Date -Format 'yyyyMMdd-HHmmss'))
$payloadPath = Join-Path $tempRoot 'BFVR-InteropDiagnostic'
try {
    New-Item -ItemType Directory -Path $payloadPath -Force | Out-Null
    Copy-Item -Path (Join-Path $sourcePath '*') -Destination $payloadPath -Recurse
    Copy-Item -LiteralPath (Join-Path (Split-Path -Parent $PSScriptRoot) 'docs\INTEROP_DIAGNOSTIC.md') -Destination (Join-Path $payloadPath 'INTEROP-DIAGNOSTIC-README.md')
    $archiveParent = Split-Path -Parent $destinationPath
    if (-not (Test-Path -LiteralPath $archiveParent -PathType Container)) {
        New-Item -ItemType Directory -Path $archiveParent -Force | Out-Null
    }
    Compress-Archive -Path (Join-Path $tempRoot '*') -DestinationPath $destinationPath
}
finally {
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}

$archiveHash = (Get-FileHash -LiteralPath $destinationPath -Algorithm SHA256).Hash
$archiveSize = (Get-Item -LiteralPath $destinationPath).Length
Write-Host ("Created isolated diagnostic payload: {0}" -f $destinationPath)
Write-Host ("Size: {0} bytes; SHA-256: {1}" -f $archiveSize, $archiveHash)
