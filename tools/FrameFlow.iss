#ifndef AppVersion
  #define AppVersion "1.0.3"
#endif

[Setup]
AppId={{5AD9F433-A5FD-4F57-A5C2-A968FCB63DF7}
AppName=FrameFlow
AppVersion={#AppVersion}
AppVerName=FrameFlow {#AppVersion}
AppPublisher=MhDTam
AppPublisherURL=https://github.com/MhDTam/FrameFlow
AppSupportURL=https://github.com/MhDTam/FrameFlow/issues
AppUpdatesURL=https://github.com/MhDTam/FrameFlow/releases
DefaultDirName={localappdata}\Programs\FrameFlow
DefaultGroupName=FrameFlow
PrivilegesRequired=lowest
SetupArchitecture=x64
MinVersion=10.0.17763
WizardStyle=modern
SetupIconFile=..\apps\FrameFlow.UI\Assets\FrameFlow.ico
UninstallDisplayIcon={app}\FrameFlow.exe
LicenseFile=..\LICENSE
Compression=lzma2/max
SolidCompression=yes
OutputDir=..\dist
OutputBaseFilename=FrameFlow-v{#AppVersion}-Setup
CloseApplications=yes
RestartApplications=no

[Files]
Source: "..\dist\FrameFlow-InstallerPayload\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\FrameFlow"; Filename: "{app}\FrameFlow.exe"; WorkingDir: "{app}"

[Run]
Filename: "{app}\FrameFlow.exe"; Description: "Launch FrameFlow"; Flags: nowait postinstall skipifsilent
