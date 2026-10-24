@echo off
rem Automation helper: clean rebuild of everything, unit tests, full benchmark report.
setlocal
cd /d "%~dp0\.."
if not exist logs mkdir logs
echo running > logs\ci-final.status
taskkill /IM FrameFlow.UI.exe /F > nul 2>&1
taskkill /IM FrameFlow.TestRenderer.exe /F > nul 2>&1
taskkill /IM FrameFlow.Engine.exe /F > nul 2>&1
call build.bat nopause || goto :fail
call tools\vsenv.bat > nul 2>&1
ctest --test-dir build\native --output-on-failure > logs\tests.log 2>&1 || goto :fail
bin\FrameFlow.Benchmark.exe --modes all --baseline --seconds 8 --out docs\benchmark-report.md > logs\benchmark.log 2>&1
echo done > logs\ci-final.status
exit /b 0
:fail
echo failed > logs\ci-final.status
exit /b 1
