@echo off
rem Builds and packages the standalone FrameFlow Portable distribution.
rem Output: dist\FrameFlow-Portable\ and dist\FrameFlow-v1.0.0-Portable.zip
setlocal
cd /d "%~dp0"

set "NO_PAUSE="
set "ARGS="
:arg_loop
if "%~1"=="" goto run
if /i "%~1"=="nopause" (
    set "NO_PAUSE=1"
) else (
    set ARGS=%ARGS% %1
)
shift
goto arg_loop

:run
powershell -NoProfile -ExecutionPolicy Bypass -File "tools\package-portable.ps1" %ARGS%
if errorlevel 1 (
    echo.
    echo Packaging failed!
    if not defined NO_PAUSE pause
    exit /b 1
)

echo.
echo Packaging completed successfully.
if not defined NO_PAUSE pause
exit /b 0
