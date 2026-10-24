@echo off
rem Measures pacing accuracy on this machine. Report: docs\benchmark-report.md
rem Usage: benchmark.bat [extra args, e.g. --quick]
setlocal
cd /d "%~dp0"
bin\FrameFlow.Benchmark.exe --modes all --baseline --seconds 8 --out docs\benchmark-report.md %*
pause
