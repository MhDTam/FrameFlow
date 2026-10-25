FrameFlow 1.0.2 is a Windows x64 update focused on user control and clear game compatibility.

Game detection no longer enables a frame limit or injects the DXGI hook. Choose **FrameFlow** or
**NVIDIA** under **Limiter**, then turn on **Frame Pacing** for each game run. Neither method starts
automatically on the next run. The FPS slider now accepts every integer from 30 to 240 instead of
snapping to its reference labels; click the large FPS number for precise limits from 10 to 1000.

**Mini OSD** now appears over windowed and borderless games when switched on, even while waiting
for frame data. Live FPS and the graph require a compatible FrameFlow hook or administrator access
for ETW present tracing. Exclusive fullscreen can prevent a separate overlay window from appearing.
The app and installer use the supplied glossy aqua F logo.

The experimental DXGI hook remains disabled for Euro Truck Simulator 2 version 1.61.1.9. Late
attachment repeatedly crashed the game. Direct executable launch exits before startup on this
Steam installation, with or without the hook. Start the game through Steam and select the NVIDIA
limiter manually if available. This is a compatibility fallback, not FrameFlow's in-process pacer.

Choose `FrameFlow-v1.0.2-Lite.zip` if the x64 .NET 8 Runtime is installed. It includes Windows
App SDK Runtime 1.7. Choose `FrameFlow-v1.0.2-Portable.zip` for a standalone package with .NET 8
included as well. `FrameFlow-v1.0.2-Setup.exe` is a compressed per-user installer with shortcuts
and an uninstaller. The installer is not code-signed.

Close old FrameFlow processes before updating. Extract a ZIP before opening `FrameFlow.exe`.
The SHA-256 file next to each download can be used to verify it. FrameFlow's original code and
assets are MIT-licensed; third-party components retain their own licenses.

## Interface

![FrameFlow 1.0.2 main window](https://raw.githubusercontent.com/MhDTam/FrameFlow/main/docs/frameflow-main.png)
