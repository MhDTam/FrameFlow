@echo off
rem Automation helper: builds the WinUI app, then runs engine + test renderer + UI snapshots.
setlocal
cd /d "%~dp0\.."
if not exist logs mkdir logs
echo running > logs\ci-ui.status
call tools\vsenv.bat > logs\build-native.log 2>&1 || goto :fail
cmake -S . -B build\native -G Ninja -DCMAKE_BUILD_TYPE=Release >> logs\build-native.log 2>&1 || goto :fail
cmake --build build\native >> logs\build-native.log 2>&1 || goto :fail
ctest --test-dir build\native --output-on-failure > logs\tests.log 2>&1
call tools\dotnetenv.bat > logs\build-ui.log 2>&1 || goto :fail
echo DOTNET=%DOTNET% >> logs\build-ui.log
"%DOTNET%" build apps\FrameFlow.UI\FrameFlow.UI.csproj -c Release -p:Platform=x64 -nologo >> logs\build-ui.log 2>&1 || goto :fail
echo built > logs\ci-ui.status
if /i "%~1"=="buildonly" goto :done

taskkill /IM FrameFlow.UI.exe /F > nul 2>&1
taskkill /IM FrameFlow.TestRenderer.exe /F > nul 2>&1
taskkill /IM FrameFlow.Engine.exe /F > nul 2>&1
del /q logs\ui-*.png logs\renderer-report.txt > nul 2>&1

start "" bin\FrameFlow.Engine.exe
timeout /t 2 /nobreak > nul
start "" bin\FrameFlow.TestRenderer.exe --seconds 40 --report logs\renderer-report.txt
timeout /t 3 /nobreak > nul
start "" /wait bin\FrameFlow.UI.exe --snapshot logs\ui-main.png --snapshot-delay 9000
start "" /wait bin\FrameFlow.UI.exe --snapshot logs\ui-advanced.png --view advanced --snapshot-delay 5000
start "" /wait bin\FrameFlow.UI.exe --snapshot logs\ui-settings.png --view settings --snapshot-delay 4000
timeout /t 25 /nobreak > nul
taskkill /IM FrameFlow.TestRenderer.exe > nul 2>&1
timeout /t 2 /nobreak > nul
start "" /wait bin\FrameFlow.UI.exe --snapshot logs\ui-nogame.png --snapshot-delay 4000
taskkill /IM FrameFlow.Engine.exe /F > nul 2>&1
copy /y "%LOCALAPPDATA%\FrameFlow\Logs\engine.log" logs\engine.log > nul 2>&1
copy /y "%LOCALAPPDATA%\FrameFlow\Logs\ui.log" logs\ui.log > nul 2>&1
copy /y "%LOCALAPPDATA%\FrameFlow\Logs\testrenderer.log" logs\testrenderer.log > nul 2>&1
:done
echo done > logs\ci-ui.status
exit /b 0
:fail
echo failed > logs\ci-ui.status
exit /b 1
