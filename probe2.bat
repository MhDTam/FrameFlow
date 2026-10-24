@echo off
setlocal
cd /d "%~dp0"
if not exist logs mkdir logs
set LOG=logs\probe2.log
echo ==== probe2 > %LOG%
set VS=C:\Program Files\Microsoft Visual Studio\2022\Community
dir /b "%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin" >> %LOG% 2>&1
dir /b "%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja" >> %LOG% 2>&1
dir /b "%VS%\MSBuild\Current\Bin\MSBuild.exe" >> %LOG% 2>&1
dir /b "%VS%\VC\Tools\MSVC" >> %LOG% 2>&1
echo --- user dotnet >> %LOG%
"%LOCALAPPDATA%\Microsoft\dotnet\dotnet.exe" --list-sdks >> %LOG% 2>&1
dir /b "%LOCALAPPDATA%\Microsoft\dotnet\sdk" >> %LOG% 2>&1
dir /b "C:\Program Files\dotnet\sdk" >> %LOG% 2>&1
echo --- vs packages >> %LOG%
"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -include packages -format json > logs\vspackages.json 2>&1
powershell -NoProfile -Command "$j = Get-Content logs\vspackages.json -Raw | ConvertFrom-Json; $j[0].packages | ? { $_.type -in 'Workload','Component' } | % { $_.id } | Sort-Object" >> %LOG% 2>&1
del logs\vspackages.json
echo ==== done >> %LOG%
