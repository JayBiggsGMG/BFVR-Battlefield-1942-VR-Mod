[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$ControlPath,

    [Parameter(Mandatory)]
    [string]$GameExecutable,

    [ValidateRange(15, 300)]
    [int]$MaximumWaitSeconds = 60
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-CompatibilityValue {
    param(
        [Parameter(Mandatory)]
        [string]$KeyPath,

        [Parameter(Mandatory)]
        [string]$ValueName
    )

    $property = Get-ItemProperty -LiteralPath $KeyPath -Name $ValueName -ErrorAction SilentlyContinue
    if ($null -eq $property) {
        return $null
    }
    return $property.PSObject.Properties[$ValueName].Value
}

function Restore-CompatibilityValue {
    param(
        [Parameter(Mandatory)]
        [string]$KeyPath,

        [Parameter(Mandatory)]
        [string]$ValueName,

        [AllowNull()]
        [object]$OriginalValue
    )

    if ($null -eq $OriginalValue) {
        Remove-ItemProperty -LiteralPath $KeyPath -Name $ValueName -ErrorAction SilentlyContinue
        return
    }

    New-ItemProperty -LiteralPath $KeyPath -Name $ValueName -Value $OriginalValue -PropertyType String -Force | Out-Null
}

$userLayersKey = 'HKCU:\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers'
$machineLayersKey = 'HKLM:\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers'
$readyPath = "$ControlPath.ready"
$donePath = "$ControlPath.done"
$reportPath = "$ControlPath.report.json"
$controlDirectory = Split-Path -Parent $ControlPath

New-Item -ItemType Directory -Path $controlDirectory -Force | Out-Null
if ((Test-Path -LiteralPath $readyPath) -or (Test-Path -LiteralPath $donePath) -or (Test-Path -LiteralPath $reportPath)) {
    throw "Compatibility test control path already exists: $ControlPath"
}

$userOriginal = Get-CompatibilityValue -KeyPath $userLayersKey -ValueName $GameExecutable
$machineOriginal = Get-CompatibilityValue -KeyPath $machineLayersKey -ValueName $GameExecutable
$report = [ordered]@{
    game_executable = $GameExecutable
    user_original = $userOriginal
    machine_original = $machineOriginal
    layers_removed = $false
    completion_signal_received = $false
    restoration_succeeded = $false
}

$userLayerRemoved = $false
$machineLayerRemoved = $false

try {
    Remove-ItemProperty -LiteralPath $userLayersKey -Name $GameExecutable -ErrorAction Stop
    $userLayerRemoved = $true
    Remove-ItemProperty -LiteralPath $machineLayersKey -Name $GameExecutable -ErrorAction Stop
    $machineLayerRemoved = $true
    $report.layers_removed = $true
    [System.IO.File]::WriteAllText($readyPath, "ready`r`n", [System.Text.Encoding]::ASCII)

    $deadline = [DateTime]::UtcNow.AddSeconds($MaximumWaitSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Test-Path -LiteralPath $donePath) {
            $report.completion_signal_received = $true
            break
        }
        Start-Sleep -Milliseconds 200
    }
}
finally {
    try {
        if ($userLayerRemoved) {
            Restore-CompatibilityValue -KeyPath $userLayersKey -ValueName $GameExecutable -OriginalValue $userOriginal
        }
    }
    catch {
        $report.user_restore_error = $_.Exception.Message
    }
    try {
        if ($machineLayerRemoved) {
            Restore-CompatibilityValue -KeyPath $machineLayersKey -ValueName $GameExecutable -OriginalValue $machineOriginal
        }
    }
    catch {
        $report.machine_restore_error = $_.Exception.Message
    }
    $report.user_restored = Get-CompatibilityValue -KeyPath $userLayersKey -ValueName $GameExecutable
    $report.machine_restored = Get-CompatibilityValue -KeyPath $machineLayersKey -ValueName $GameExecutable
    $report.restoration_succeeded = ($report.user_restored -eq $userOriginal -and $report.machine_restored -eq $machineOriginal)
    $report.completed_utc = [DateTime]::UtcNow.ToString('o')
    [System.IO.File]::WriteAllText($reportPath, ($report | ConvertTo-Json -Depth 4), [System.Text.Encoding]::UTF8)
}

if (-not $report.restoration_succeeded) {
    throw 'The BF1942 compatibility values were not restored exactly.'
}
