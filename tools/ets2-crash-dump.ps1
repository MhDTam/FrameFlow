param(
    [ValidateSet('Enable', 'Disable', 'Status')]
    [string]$Action = 'Status'
)

$ErrorActionPreference = 'Stop'
$key = 'HKCU:\Software\Microsoft\Windows\Windows Error Reporting\LocalDumps\eurotrucks2.exe'
$dumpDir = Join-Path $env:LOCALAPPDATA 'FrameFlow\CrashDumps'

if ($Action -eq 'Status') {
    if (Test-Path -LiteralPath $key) {
        Get-ItemProperty -LiteralPath $key | Select-Object DumpFolder, DumpType, DumpCount, FrameFlowDiagnostic
    } else {
        Write-Output 'ETS2 crash dumps are not configured for this user.'
    }
    return
}

if ($Action -eq 'Enable') {
    if (Test-Path -LiteralPath $key) {
        $existing = Get-ItemProperty -LiteralPath $key
        if ($existing.FrameFlowDiagnostic -ne 1) {
            throw 'An existing ETS2 crash-dump configuration belongs to another tool. It was not changed.'
        }
    }
    New-Item -ItemType Directory -Path $dumpDir -Force | Out-Null
    New-Item -Path $key -Force | Out-Null
    New-ItemProperty -LiteralPath $key -Name DumpFolder -Value $dumpDir -PropertyType ExpandString -Force | Out-Null
    New-ItemProperty -LiteralPath $key -Name DumpType -Value 1 -PropertyType DWord -Force | Out-Null
    New-ItemProperty -LiteralPath $key -Name DumpCount -Value 2 -PropertyType DWord -Force | Out-Null
    New-ItemProperty -LiteralPath $key -Name FrameFlowDiagnostic -Value 1 -PropertyType DWord -Force | Out-Null
    Write-Output "ETS2 minidumps enabled in $dumpDir. Run again with -Action Disable after testing."
    return
}

if (Test-Path -LiteralPath $key) {
    $existing = Get-ItemProperty -LiteralPath $key
    if ($existing.FrameFlowDiagnostic -ne 1 -or $existing.DumpFolder -ne $dumpDir) {
        throw 'The ETS2 crash-dump configuration has changed. It was not removed.'
    }
    Remove-Item -LiteralPath $key -Recurse
}
Write-Output 'FrameFlow ETS2 crash-dump configuration disabled. Existing dump files were kept.'
