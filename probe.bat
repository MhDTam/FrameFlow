@echo off
setlocal
cd /d "%~dp0"
if not exist logs mkdir logs
set LOG=logs\probe.log
echo ==== probe %date% %time% > %LOG%
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
echo --- vswhere >> %LOG%
%VSWHERE% -all -products * -format text >> %LOG% 2>&1
echo --- VC tools >> %LOG%
%VSWHERE% -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath >> %LOG% 2>&1
echo --- packages of interest >> %LOG%
%VSWHERE% -latest -products * -include packages -format text | findstr /i "packages_.*id: Microsoft.VisualStudio.Component.VC.CMake Microsoft.VisualStudio.Workload.NativeDesktop Microsoft.VisualStudio.Workload.ManagedDesktop Microsoft.VisualStudio.ComponentGroup.WindowsAppSDK Microsoft.VisualStudio.Workload.Universal Microsoft.NetCore.Component.SDK Microsoft.VisualStudio.Component.Windows11SDK" >> %LOG% 2>&1
echo --- cmake in PATH >> %LOG%
where cmake >> %LOG% 2>&1
echo --- dotnet >> %LOG%
where dotnet >> %LOG% 2>&1
dotnet --list-sdks >> %LOG% 2>&1
dotnet --list-runtimes >> %LOG% 2>&1
echo --- Windows SDKs >> %LOG%
dir /b "%ProgramFiles(x86)%\Windows Kits\10\Include" >> %LOG% 2>&1
echo --- OS >> %LOG%
ver >> %LOG%
echo --- GPU / displays >> %LOG%
powershell -NoProfile -Command "Get-CimInstance Win32_VideoController | Select Name,CurrentRefreshRate,CurrentHorizontalResolution,CurrentVerticalResolution | Format-List" >> %LOG% 2>&1
echo --- nuget reachability >> %LOG%
powershell -NoProfile -Command "try { (Invoke-WebRequest -UseBasicParsing https://api.nuget.org/v3/index.json -TimeoutSec 15).StatusCode } catch { $_.Exception.Message }" >> %LOG% 2>&1
echo ==== done >> %LOG%
