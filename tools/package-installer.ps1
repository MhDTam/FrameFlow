param(
    [string]$Version = '1.0.2',
    [string]$IsccPath
)

$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$') {
    throw 'Version must be major.minor.patch, for example 1.0.0'
}

$rootDir = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$distDir = Join-Path $rootDir 'dist'
$portableDir = Join-Path $distDir 'FrameFlow-Portable'
$payloadDir = Join-Path $distDir 'FrameFlow-InstallerPayload'
$installer = Join-Path $distDir "FrameFlow-v$Version-Setup.exe"

if (-not (Test-Path -LiteralPath (Join-Path $portableDir 'FrameFlow.UI.exe'))) {
    throw 'Build the standalone Portable package first with tools/package-portable.ps1'
}

if (-not $IsccPath) {
    $candidates = @(
        (Join-Path $rootDir 'build\inno-tool\installed\ISCC.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 7\ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 7\ISCC.exe')
    )
    $IsccPath = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (-not $IsccPath -or -not (Test-Path -LiteralPath $IsccPath)) {
    throw 'Inno Setup 7 compiler ISCC.exe was not found; pass -IsccPath'
}

# Fixed staging path, resolved and checked before recursive removal.
$resolvedPayload = [IO.Path]::GetFullPath($payloadDir)
$resolvedDist = [IO.Path]::GetFullPath($distDir)
if (-not $resolvedPayload.StartsWith($resolvedDist + [IO.Path]::DirectorySeparatorChar,
                                     [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Installer payload escaped the dist directory'
}
if (Test-Path -LiteralPath $resolvedPayload) {
    Remove-Item -LiteralPath $resolvedPayload -Recurse -Force
}
New-Item -ItemType Directory -Path $resolvedPayload -Force | Out-Null

$excludeFiles = @('portable.txt', 'run.bat', 'README-PORTABLE.txt', 'FrameFlow.TestRenderer.exe')
Get-ChildItem -LiteralPath $portableDir -File | Where-Object Name -NotIn $excludeFiles | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $resolvedPayload -Force
}
Get-ChildItem -LiteralPath $portableDir -Directory | Where-Object Name -ne 'data' | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $resolvedPayload $_.Name) -Recurse -Force
}
Copy-Item -LiteralPath (Join-Path $rootDir 'third_party\inno-setup\LICENSE.txt') `
    -Destination (Join-Path $resolvedPayload 'licenses\InnoSetup-LICENSE.txt') -Force

if (Test-Path -LiteralPath (Join-Path $resolvedPayload 'portable.txt')) {
    throw 'Portable mode marker must not be installed'
}
if (Test-Path -LiteralPath (Join-Path $resolvedPayload 'data')) {
    throw 'Portable data must not be installed'
}
foreach ($name in @('FrameFlow.exe', 'FrameFlow.UI.exe', 'FrameFlow.Engine.exe',
                    'FrameFlow.Launch.exe', 'FrameFlow.Hook64.dll', 'LICENSE',
                    'THIRD_PARTY_NOTICES.md')) {
    if (-not (Test-Path -LiteralPath (Join-Path $resolvedPayload $name))) {
        throw "Installer payload missing $name"
    }
}

& $IsccPath "/DAppVersion=$Version" "/O$distDir" "/FFrameFlow-v$Version-Setup" "/Qp" `
    (Join-Path $PSScriptRoot 'FrameFlow.iss')
if ($LASTEXITCODE -ne 0) { throw "Inno Setup compilation failed with exit code $LASTEXITCODE" }
if (-not (Test-Path -LiteralPath $installer)) { throw 'Installer executable is missing' }

$hash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
$hashFile = "$installer.sha256"
Set-Content -LiteralPath $hashFile -Value "$hash  $(Split-Path $installer -Leaf)" -Encoding ascii
Write-Host "Installer: $installer"
Write-Host "SHA-256: $hashFile"
