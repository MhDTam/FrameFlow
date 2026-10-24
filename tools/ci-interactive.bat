@echo off
rem Automation helper: rebuild native, then start engine + test renderer + UI for hands-on testing.
setlocal
cd /d "%~dp0\.."
if not exist logs mkdir logs
call tools\vsenv.bat > logs\build-native.log 2>&1
cmake --build build\native >> logs\build-native.log 2>&1
taskkill /IM FrameFlow.UI.exe /F > nul 2>&1
taskkill /IM FrameFlow.TestRenderer.exe /F > nul 2>&1
taskkill /IM FrameFlow.Engine.exe /F > nul 2>&1
timeout /t 1 /nobreak > nul
start "" bin\FrameFlow.TestRenderer.exe --width 960 --height 540
timeout /t 2 /nobreak > nul
start "" bin\FrameFlow.UI.exe
