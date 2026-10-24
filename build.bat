@echo off
rem Builds everything: native engine + tools (CMake/Ninja/MSVC) and the WinUI 3 app (.NET).
rem Output: bin\   Usage: build.bat [nopause]
setlocal
cd /d "%~dp0"
echo ==== FrameFlow build %date% %time%

call tools\vsenv.bat || goto :fail
echo ---- native configure
cmake -S . -B build\native -G Ninja -DCMAKE_BUILD_TYPE=Release || goto :fail
echo ---- native build
cmake --build build\native || goto :fail

call tools\dotnetenv.bat || goto :fail
echo ---- ui build (%DOTNET%)
"%DOTNET%" build apps\FrameFlow.UI\FrameFlow.UI.csproj -c Release -p:Platform=x64 -nologo -clp:NoSummary;ErrorsOnly || goto :fail

echo FrameFlow build succeeded. Binaries are in bin\
if /i not "%~1"=="nopause" pause
exit /b 0

:fail
echo FrameFlow build FAILED
if /i not "%~1"=="nopause" pause
exit /b 1
