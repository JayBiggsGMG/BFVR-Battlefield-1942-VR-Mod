[CmdletBinding()]
param(
    [string]$GameRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
    [string]$ProfilePath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'profiles\bf1942-win32-decbb52f.json')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Write-Result {
    param(
        [Parameter(Mandatory = $true)][ValidateSet('PASS', 'WARN', 'FAIL', 'INFO')][string]$Kind,
        [Parameter(Mandatory = $true)][string]$Message
    )

    Write-Host ('[{0}] {1}' -f $Kind, $Message)
}

function Get-ExpectedModuleHash {
    param([Parameter(Mandatory = $true)][string]$Entry)

    $parts = $Entry -split ':\s*', 2
    if ($parts.Count -ne 2 -or [string]::IsNullOrWhiteSpace($parts[0]) -or [string]::IsNullOrWhiteSpace($parts[1])) {
        throw "Invalid module inventory entry in profile: $Entry"
    }

    return [pscustomobject]@{
        Name = $parts[0]
        Sha256 = $parts[1].Trim().ToUpperInvariant()
    }
}

try {
    $resolvedGameRoot = (Resolve-Path -LiteralPath $GameRoot).Path
    $resolvedProfile = (Resolve-Path -LiteralPath $ProfilePath).Path
    $profile = Get-Content -LiteralPath $resolvedProfile -Raw | ConvertFrom-Json

    if ($profile.schema_version -ne 1) {
        throw "Unsupported profile schema version: $($profile.schema_version)"
    }

    $exePath = Join-Path $resolvedGameRoot $profile.executable.filename
    if (-not (Test-Path -LiteralPath $exePath -PathType Leaf)) {
        Write-Result FAIL "Missing expected executable: $exePath"
        exit 2
    }

    $failures = 0
    $warnings = 0
    $exeHash = (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash.ToUpperInvariant()
    if ($exeHash -eq $profile.executable.sha256.ToUpperInvariant()) {
        Write-Result PASS "Executable matches profile $($profile.profile_id): $($profile.executable.filename)"
    }
    else {
        Write-Result FAIL "Executable hash differs from profile $($profile.profile_id). BFVR must not attach."
        $failures++
    }

    foreach ($entry in $profile.observed_environment.observed_root_modules) {
        $expected = Get-ExpectedModuleHash $entry
        $modulePath = Join-Path $resolvedGameRoot $expected.Name
        if (-not (Test-Path -LiteralPath $modulePath -PathType Leaf)) {
            Write-Result WARN "Profiled module is absent: $($expected.Name)"
            $warnings++
            continue
        }

        $actualHash = (Get-FileHash -LiteralPath $modulePath -Algorithm SHA256).Hash.ToUpperInvariant()
        if ($actualHash -eq $expected.Sha256) {
            Write-Result PASS "Profiled module matches: $($expected.Name)"
        }
        else {
            Write-Result WARN "Profiled module hash differs: $($expected.Name). Coexistence is unvalidated."
            $warnings++
        }
    }

    $profiledNames = @($profile.observed_environment.observed_root_modules | ForEach-Object { (Get-ExpectedModuleHash $_).Name })
    $rootDllNames = @(Get-ChildItem -LiteralPath $resolvedGameRoot -File -Filter '*.dll' | ForEach-Object { $_.Name })
    $unprofiledNames = @($rootDllNames | Where-Object { $_ -notin $profiledNames })
    foreach ($name in $unprofiledNames) {
        Write-Result WARN "Unprofiled root DLL detected: $name. No BFVR attach decision is available."
        $warnings++
    }

    Write-Result INFO "This validator is read-only. It does not launch, attach to, patch, replace, or move game files."

    if ($failures -gt 0) {
        exit 2
    }
    if ($warnings -gt 0) {
        exit 1
    }
    exit 0
}
catch {
    Write-Result FAIL $_.Exception.Message
    exit 2
}
