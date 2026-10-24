@rem Locates Visual Studio 2022 (C++ tools) with vswhere and loads the x64 developer environment.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo [FrameFlow] vswhere.exe not found. Install Visual Studio 2022 with "Desktop development with C++".
  exit /b 1
)
set "VSINSTALL="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
  echo [FrameFlow] Visual Studio with the C++ x64 toolset was not found.
  exit /b 1
)
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
where cmake >nul 2>nul || set "PATH=%VSINSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"
where ninja >nul 2>nul || set "PATH=%VSINSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
exit /b 0
