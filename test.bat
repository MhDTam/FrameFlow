@echo off
rem Runs the native unit tests (CTest). Log: logs\tests.log
setlocal
cd /d "%~dp0"
if not exist logs mkdir logs
call tools\vsenv.bat > nul 2>&1 || exit /b 1
ctest --test-dir build\native --output-on-failure > logs\tests.log 2>&1
set RC=%ERRORLEVEL%
type logs\tests.log
if /i not "%~1"=="nopause" pause
exit /b %RC%
