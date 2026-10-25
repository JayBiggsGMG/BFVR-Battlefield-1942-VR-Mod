[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$GameRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$gamePath = (Resolve-Path -LiteralPath $GameRoot).Path
$bfvrPath = Join-Path $gamePath 'BFVR'
$zipPath = Join-Path $gamePath 'BFVR-interop-diagnostic.zip'
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$stagingPath = Join-Path $env:TEMP ("BFVR-interop-diagnostic-{0}-{1}" -f $PID, $timestamp)

$logFiles = @(
    @{ Source = Join-Path $bfvrPath 'loader.log'; Name = 'loader.log' },
    @{ Source = Join-Path $bfvrPath 'BFVR-interop-producer.log'; Name = 'BFVR-interop-producer.log' },
    @{ Source = Join-Path $gamePath 'bf42++_debug.log'; Name = 'bf42++_debug.log' }
)
$logFiles += Get-ChildItem -LiteralPath $bfvrPath -Filter 'BFVRPresenter-game*.log' -File -ErrorAction SilentlyContinue |
    ForEach-Object { @{ Source = $_.FullName; Name = $_.Name } }
$availableLogs = @($logFiles | Where-Object { Test-Path -LiteralPath $_.Source -PathType Leaf })
if ($availableLogs.Count -eq 0) {
    throw "No diagnostic logs were found under '$bfvrPath'. Run BFVR using Run-BFVRInteropDiagnostic.cmd."
}

New-Item -ItemType Directory -Path $stagingPath | Out-Null
try {
    $manifest = [System.Collections.Generic.List[string]]::new()
    $manifest.Add("BFVR one-run interop diagnostics collected $([DateTime]::Now.ToString('o'))")
    foreach ($entry in $availableLogs) {
        $destination = Join-Path $stagingPath $entry.Name
        Copy-Item -LiteralPath $entry.Source -Destination $destination
        $hash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
        $length = (Get-Item -LiteralPath $destination).Length
        $manifest.Add(("{0} size={1} SHA256={2}" -f $entry.Name, $length, $hash))
    }
    Set-Content -LiteralPath (Join-Path $stagingPath 'capture-manifest.txt') -Value $manifest -Encoding UTF8
    Compress-Archive -Path (Join-Path $stagingPath '*') -DestinationPath $zipPath -Force
}
finally {
    Remove-Item -LiteralPath $stagingPath -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ("Created diagnostic archive: {0}" -f $zipPath)
Write-Host ("Included {0} log file(s)." -f $availableLogs.Count)
