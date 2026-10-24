@echo off
rem Automation helper: WinUI build only (normal verbosity) -> logs\build-ui.log
setlocal
cd /d "%~dp0\.."
if not exist logs mkdir logs
echo running > logs\ci-uibuild.status
call tools\dotnetenv.bat > logs\build-ui.log 2>&1 || goto :fail
"%DOTNET%" build apps\FrameFlow.UI\FrameFlow.UI.csproj -c Release -p:Platform=x64 -nologo -v:normal > logs\build-ui-verbose.log 2>&1 || goto :fail
echo done > logs\ci-uibuild.status
exit /b 0
:fail
echo failed > logs\ci-uibuild.status
exit /b 1
