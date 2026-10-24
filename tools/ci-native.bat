@echo off
rem Automation helper: native build + unit tests + quick benchmark, all logged to logs\.
setlocal
cd /d "%~dp0\.."
if not exist logs mkdir logs
echo running > logs\ci-native.status
call tools\vsenv.bat > logs\ci-native.log 2>&1 || goto :fail
cmake -S . -B build\native -G Ninja -DCMAKE_BUILD_TYPE=Release >> logs\ci-native.log 2>&1 || goto :fail
cmake --build build\native >> logs\ci-native.log 2>&1 || goto :fail
ctest --test-dir build\native --output-on-failure > logs\tests.log 2>&1
bin\FrameFlow.Benchmark.exe --fps 60,144 --modes all --baseline --seconds 4 --out logs\benchmark-quick.md > logs\benchmark-quick.log 2>&1
echo done > logs\ci-native.status
exit /b 0
:fail
echo failed > logs\ci-native.status
exit /b 1
