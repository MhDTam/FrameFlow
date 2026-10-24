@rem Finds a .NET SDK >= 8 (machine-wide or per-user install) and exposes it as %DOTNET%.
set "DOTNET="
for %%d in ("%ProgramFiles%\dotnet\dotnet.exe" "%LOCALAPPDATA%\Microsoft\dotnet\dotnet.exe") do (
  if exist "%%~d" call :probe "%%~d"
)
if not defined DOTNET (
  echo [FrameFlow] No .NET SDK 8 or newer found. Install it from https://dotnet.microsoft.com/download
  exit /b 1
)
exit /b 0

:probe
pushd "%~dp1"
for /f "tokens=1 delims=." %%v in ('dotnet.exe --list-sdks 2^>nul') do (
  if %%v GEQ 8 set "DOTNET=%~1"
)
popd
exit /b 0
