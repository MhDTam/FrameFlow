$ErrorActionPreference = 'Stop'

$games = @(Get-Process -Name eurotrucks2 -ErrorAction SilentlyContinue)
if ($games.Count -ne 1) {
    throw "Start Euro Truck Simulator 2 normally through Steam, then run this script while exactly one game process is running (found $($games.Count))."
}

$game = $games[0]
if (-not $game.Path -or -not (Test-Path -LiteralPath $game.Path)) {
    throw 'Could not read the running game executable path.'
}

$launcher = Join-Path (Split-Path $PSScriptRoot -Parent) 'bin\FrameFlow.Launch.exe'
if (-not (Test-Path -LiteralPath $launcher)) {
    throw 'Build FrameFlow before running the diagnostic attach.'
}

Write-Output "Attaching diagnostic hook to PID $($game.Id). The game may crash."
& $launcher --diagnose-unsafe --detour-present --attach $game.Id $game.Path
if ($LASTEXITCODE -ne 0) {
    throw "Diagnostic attach failed with exit code $LASTEXITCODE. Check the FrameFlow hook log."
}
Write-Output 'Diagnostic hook attached. Play until the crash or for a few minutes, then check %LOCALAPPDATA%\FrameFlow\CrashDumps and the hook log.'
