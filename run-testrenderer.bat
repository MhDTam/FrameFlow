@echo off
rem Starts the DX11 test renderer uncapped; FrameFlow detects it and applies its profile.
cd /d "%~dp0"
start "" "bin\FrameFlow.TestRenderer.exe" %*
