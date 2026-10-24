FrameFlow 1.0.0 is a Windows x64 release. DX11/DX12 game hooking remains experimental.

Choose `FrameFlow-v1.0.0-Lite.zip` if the x64 .NET 8 Runtime is installed. It includes Windows
App SDK Runtime 1.7. Choose `FrameFlow-v1.0.0-Portable.zip` for a standalone package with .NET 8
included as well.

Extract the ZIP, then open `FrameFlow.exe`. The app detects games, offers a custom FPS limit,
and shows live frametime statistics when a compatible DX11/DX12 session is active. NVIDIA driver
limiting is available only when selected explicitly. Games with detected anti-cheat are skipped
by default; game compatibility varies.

The SHA-256 file next to each ZIP can be used to verify the download.

FrameFlow's original code and assets are MIT-licensed. Third-party components retain their own
licenses; see `LICENSE` and `THIRD_PARTY_NOTICES.md` inside the ZIP.
