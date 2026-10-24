@echo off
rem Builds everything: native engine + tools (CMake/Ninja/MSVC) and the WinUI 3 app (.NET).
rem Output: bin\   Logs: logs\build.log   Usage: build.bat [nopause]
setlocal
cd /d "%~dp0"
if not exist logs mkdir logs
set "LOG=%~dp0logs\build.log"
echo ==== FrameFlow build %date% %time% > "%LOG%"

call tools\vsenv.bat >> "%LOG%" 2>&1 || goto :fail
echo ---- native configure >> "%LOG%"
cmake -S . -B build\native -G Ninja -DCMAKE_BUILD_TYPE=Release >> "%LOG%" 2>&1 || goto :fail
echo ---- native build >> "%LOG%"
cmake --build build\native >> "%LOG%" 2>&1 || goto :fail

call tools\dotnetenv.bat >> "%LOG%" 2>&1 || goto :fail
echo ---- ui build (%DOTNET%) >> "%LOG%"
"%DOTNET%" build apps\FrameFlow.UI\FrameFlow.UI.csproj -c Release -p:Platform=x64 -nologo -clp:NoSummary;ErrorsOnly >> "%LOG%" 2>&1 || goto :fail

echo ==== BUILD SUCCEEDED >> "%LOG%"
echo FrameFlow build succeeded. Binaries are in bin\
if /i not "%~1"=="nopause" pause
exit /b 0

:fail
echo ==== BUILD FAILED >> "%LOG%"
echo FrameFlow build FAILED - see logs\build.log
if /i not "%~1"=="nopause" pause
exit /b 1
