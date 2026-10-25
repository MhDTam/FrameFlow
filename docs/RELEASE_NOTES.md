FrameFlow 1.0.1 is a Windows x64 compatibility update. DX11/DX12 game hooking remains experimental.

This update displays an idle game as **No game running**, lets you forget the last selected game,
and uses the supplied FrameFlow F logo for the app and installer icon. Automatic DXGI attachment
is disabled for Euro Truck Simulator 2 after repeated access violations observed immediately after
attachment with game version 1.61.1.9. The game can start normally; FrameFlow's in-process frame
pacing is unavailable for it in this release. An NVIDIA driver limit remains an explicit choice
when supported by the system.

Close FrameFlow and Euro Truck Simulator 2 before installing this update, then start the game
normally. The old 1.0.0 process must no longer be running, because it still attempts automatic
DXGI attachment.

Choose `FrameFlow-v1.0.1-Lite.zip` if the x64 .NET 8 Runtime is installed. It includes Windows
App SDK Runtime 1.7. Choose `FrameFlow-v1.0.1-Portable.zip` for a standalone package with .NET 8
included as well.

`FrameFlow-v1.0.1-Setup.exe` is a standalone, per-user Windows installer with Start Menu shortcut
and uninstaller. It includes both runtimes and is smaller than the standalone ZIP because it uses
solid LZMA2 compression. The installer is not code-signed.

Extract the ZIP, then open `FrameFlow.exe`. The app detects games, offers a custom FPS limit,
and shows live frametime statistics when a compatible DX11/DX12 session is active. NVIDIA driver
limiting is available only when selected explicitly. Games with detected anti-cheat are skipped
by default; game compatibility varies.

The SHA-256 file next to each download can be used to verify it.

FrameFlow's original code and assets are MIT-licensed. Third-party components retain their own
licenses; see `LICENSE` and `THIRD_PARTY_NOTICES.md` inside the installed folder or ZIP.

## Interface

![FrameFlow 1.0.1 main window](https://raw.githubusercontent.com/MhDTam/FrameFlow/main/docs/frameflow-main.png)
