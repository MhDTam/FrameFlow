# Build and stage a self-contained Windows portable release.
param(
    [string]$Version = '1.0.2',
    [switch]$SkipBuild,
    [switch]$Lite
)

$ErrorActionPreference = 'Stop'
$rootDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$binDir = Join-Path $rootDir 'bin'
$distDir = Join-Path $rootDir 'dist'
$flavor = if ($Lite) { 'Lite' } else { 'Portable' }
$outputDir = Join-Path $distDir "FrameFlow-$flavor"
$zipFile = Join-Path $distDir "FrameFlow-v$Version-$flavor.zip"

if ($Version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?$') {
    throw 'Version must be a semantic version such as 1.0.0 or 1.0.0-rc.1'
}

if (-not $SkipBuild) {
    $build = Join-Path $rootDir 'build.bat'
    & cmd.exe /c "`"$build`" nopause"
    if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE" }
}

if ($Lite) {
    $sdkCandidates = @(
        (Join-Path $env:LOCALAPPDATA 'Microsoft\dotnet\dotnet.exe'),
        (Join-Path $env:ProgramFiles 'dotnet\dotnet.exe')
    )
    $dotnet = $sdkCandidates | Where-Object {
        (Test-Path -LiteralPath $_) -and ((& $_ --list-sdks 2>$null) -match '^[89]|^1[0-9]')
    } | Select-Object -First 1
    if (-not $dotnet) { throw 'A .NET SDK 8 or newer is required to package the Lite edition' }
    $liteUi = Join-Path $rootDir 'build\lite\ui'
    $liteBuildOutput = Join-Path $rootDir 'build\lite\bin'
    & $dotnet publish (Join-Path $rootDir 'apps\FrameFlow.UI\FrameFlow.UI.csproj') `
        -c Release '-p:Platform=x64' '-p:SelfContained=false' `
        '-p:WindowsAppSDKSelfContained=true' "-p:OutputPath=$liteBuildOutput" `
        -o $liteUi -nologo '-clp:NoSummary;ErrorsOnly'
    if ($LASTEXITCODE -ne 0) { throw "Lite UI publish failed with exit code $LASTEXITCODE" }
}

$requiredFiles = @('FrameFlow.exe', 'FrameFlow.UI.exe', 'FrameFlow.UI.dll', 'FrameFlow.Engine.exe',
                   'FrameFlow.Launch.exe', 'FrameFlow.Hook64.dll')
foreach ($name in $requiredFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $binDir $name))) {
        throw "Required build output is missing: $name"
    }
}

New-Item -ItemType Directory -Path $distDir -Force | Out-Null
# Fixed output path, resolved and checked before recursive removal.
$resolvedOutput = [System.IO.Path]::GetFullPath($outputDir)
$resolvedDist = [System.IO.Path]::GetFullPath($distDir)
if (-not $resolvedOutput.StartsWith($resolvedDist + [System.IO.Path]::DirectorySeparatorChar,
                                    [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Release output escaped the dist directory'
}
if (Test-Path -LiteralPath $resolvedOutput) {
    Remove-Item -LiteralPath $resolvedOutput -Recurse -Force
}
New-Item -ItemType Directory -Path $resolvedOutput -Force | Out-Null

$exclude = @('FrameFlow.Benchmark.exe', 'FrameFlow.DX12Probe.exe',
    'FrameFlow.IpcTests.exe', 'FrameFlow.ProcessTests.exe', 'FrameFlow.ProfileTests.exe',
    'FrameFlow.SessionProbe.exe', 'FrameFlow.TelemetryTests.exe', 'FrameFlow.TimingTests.exe',
    'createdump.exe')
if ($Lite) {
    foreach ($name in @('FrameFlow.exe', 'FrameFlow.Engine.exe', 'FrameFlow.Launch.exe',
                        'FrameFlow.Hook64.dll')) {
        Copy-Item -LiteralPath (Join-Path $binDir $name) -Destination $resolvedOutput -Force
    }
    Get-ChildItem -LiteralPath $liteUi -File | Where-Object Extension -ne '.pdb' | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $resolvedOutput -Force
    }
    Get-ChildItem -LiteralPath $liteUi -Directory | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $resolvedOutput $_.Name) -Recurse -Force
    }
    foreach ($name in @('App.xbf', 'MainWindow.xbf')) {
        $source = Join-Path $liteBuildOutput $name
        if (-not (Test-Path -LiteralPath $source)) { throw "Lite UI resource missing: $name" }
        Copy-Item -LiteralPath $source -Destination $resolvedOutput -Force
    }
    $viewResources = Join-Path $liteBuildOutput 'Views'
    if (-not (Test-Path -LiteralPath $viewResources)) { throw 'Lite UI view resources are missing' }
    $viewDestination = Join-Path $resolvedOutput 'Views'
    New-Item -ItemType Directory -Path $viewDestination -Force | Out-Null
    Get-ChildItem -LiteralPath $viewResources -File | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $viewDestination -Force
    }
} else {
    Get-ChildItem -LiteralPath $binDir -File | ForEach-Object {
        if ($_.Extension -ne '.pdb' -and $_.Name -notin $exclude) {
            Copy-Item -LiteralPath $_.FullName -Destination $resolvedOutput -Force
        }
    }
    Get-ChildItem -LiteralPath $binDir -Directory | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $resolvedOutput $_.Name) -Recurse -Force
    }
}
Copy-Item -LiteralPath (Join-Path $rootDir 'LICENSE') -Destination $resolvedOutput -Force
Copy-Item -LiteralPath (Join-Path $rootDir 'THIRD_PARTY_NOTICES.md') -Destination $resolvedOutput -Force
New-Item -ItemType Directory -Path (Join-Path $resolvedOutput 'licenses') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $rootDir 'third_party\nvapi\LICENSE.txt') -Destination (Join-Path $resolvedOutput 'licenses\NVAPI-MIT.txt') -Force
$notices = @(
    @('windows-app-sdk\LICENSE.txt', 'WindowsAppSDK-LICENSE.txt'),
    @('windows-app-sdk\NOTICE.txt', 'WindowsAppSDK-NOTICE.txt'),
    @('dotnet-runtime\LICENSE.txt', 'DotNetRuntime-LICENSE.txt'),
    @('dotnet-runtime\NOTICE.txt', 'DotNetRuntime-NOTICE.txt'),
    @('system-drawing-common\LICENSE.txt', 'SystemDrawing-LICENSE.txt'),
    @('system-drawing-common\NOTICE.txt', 'SystemDrawing-NOTICE.txt')
)
foreach ($notice in $notices) {
    Copy-Item -LiteralPath (Join-Path (Join-Path $rootDir 'third_party') $notice[0]) -Destination (Join-Path (Join-Path $resolvedOutput 'licenses') $notice[1]) -Force
}

# A visible marker survives ZIP programs that omit hidden dotfiles.
Set-Content -LiteralPath (Join-Path $resolvedOutput 'portable.txt') -Value 'FrameFlow Portable Mode' -Encoding utf8
New-Item -ItemType Directory -Path (Join-Path $resolvedOutput 'data\Profiles') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $resolvedOutput 'data\Logs') -Force | Out-Null

$runBat = '@echo off' + "`r`n" + 'cd /d "%~dp0"' + "`r`n" + 'start "" "FrameFlow.exe"' + "`r`n"
Set-Content -LiteralPath (Join-Path $resolvedOutput 'run.bat') -Value $runBat -Encoding ascii
$readme = @'
FrameFlow Portable

Run FrameFlow.exe or run.bat. Keep all files together when extracting the ZIP.
Windows 10/11 x64 is required. The package includes the .NET and Windows App SDK runtime.
Profiles, settings and logs are stored in the data directory next to the executable.
The DX11/DX12 game hook is experimental. Protected games may reject it.
'@
if ($Lite) {
    $readme = @'
FrameFlow Lite

Extract the ZIP and run FrameFlow.exe. Keep all files together.
Requires the x64 .NET 8 Runtime installed on Windows 10/11 x64.
Windows App SDK Runtime 1.7 is included. If .NET 8 is missing, use the larger Portable download.
Profiles, settings and logs are stored in the data directory next to the executable.
The DX11/DX12 game hook is experimental. Protected games may reject it.
'@
}
Set-Content -LiteralPath (Join-Path $resolvedOutput 'README-PORTABLE.txt') -Value $readme -Encoding utf8

if (Test-Path -LiteralPath $zipFile) { Remove-Item -LiteralPath $zipFile -Force }
Compress-Archive -Path (Join-Path $resolvedOutput '*') -DestinationPath $zipFile -Force

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead($zipFile)
try {
    $entries = @($archive.Entries | ForEach-Object { $_.FullName.Replace('\', '/') })
    foreach ($name in @('FrameFlow.exe', 'FrameFlow.UI.exe', 'FrameFlow.Engine.exe', 'FrameFlow.Launch.exe',
                        'FrameFlow.Hook64.dll', 'portable.txt', 'run.bat', 'LICENSE',
                        'THIRD_PARTY_NOTICES.md', 'licenses/NVAPI-MIT.txt',
                        'licenses/WindowsAppSDK-LICENSE.txt', 'licenses/WindowsAppSDK-NOTICE.txt',
                        'licenses/DotNetRuntime-LICENSE.txt', 'licenses/DotNetRuntime-NOTICE.txt',
                        'licenses/SystemDrawing-LICENSE.txt', 'licenses/SystemDrawing-NOTICE.txt')) {
        if ($entries -notcontains $name) { throw "Archive missing $name" }
    }
} finally {
    $archive.Dispose()
}

$hash = (Get-FileHash -LiteralPath $zipFile -Algorithm SHA256).Hash.ToLowerInvariant()
$hashFile = "$zipFile.sha256"
Set-Content -LiteralPath $hashFile -Value "$hash  $(Split-Path $zipFile -Leaf)" -Encoding ascii
Write-Host "Portable release: $zipFile"
Write-Host "SHA-256: $hashFile"
