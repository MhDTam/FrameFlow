FrameFlow 1.0.3 is a Windows x64 update adding timed in-app graph screenshots, median frametime statistics, and improved compatibility.

- **Timed In-App Graph Screenshots**: Added a scheduled capture countdown (1–600 seconds) in the UI. Players can schedule a capture, return to the game, and FrameFlow automatically renders a crisp high-resolution snapshot of its live UI and frametime graph to `Pictures\FrameFlow`.
- **Median Frametime FPS Metric**: The primary FPS metric now reflects median frametime to prevent isolated single-frame spikes from skewing the typical framerate, while 1% low and the live graph retain every spike.
- **Dedicated FrameFlow Pacing**: The third-party limiter fallback has been replaced with FrameFlow's drift-free timeline pacing engine.
- **Euro Truck Simulator 2 Validation**: Added an opt-in Steam Overlay compatible Present detour, verified automatic attach reliability, and documented driving frametime metrics.
- **Updated Main Interface Visuals**: Refreshed the main window screenshot with the latest UI and timed screenshot feature.

Choose `FrameFlow-v1.0.3-Lite.zip` if the x64 .NET 8 Runtime is installed. It includes Windows App SDK Runtime 1.7. Choose `FrameFlow-v1.0.3-Portable.zip` for a standalone package with .NET 8 included as well. `FrameFlow-v1.0.3-Setup.exe` is a compressed per-user installer with shortcuts and an uninstaller. The installer is not code-signed.

Close old FrameFlow processes before updating. Extract a ZIP before opening `FrameFlow.exe`. The SHA-256 file next to each download can be used to verify it. FrameFlow's original code and assets are MIT-licensed; third-party components retain their own licenses.

## Interface

![FrameFlow 1.0.3 main window](https://raw.githubusercontent.com/MhDTam/FrameFlow/main/docs/frameflow-main.png)
