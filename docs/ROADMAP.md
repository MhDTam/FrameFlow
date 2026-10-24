# Roadmap

Each phase is done only when it builds, its tests pass and its behaviour was verified on a real machine.

| Phase | Scope | Status |
|---|---|---|
| 1 | Repository skeleton, WinUI shell, engine process, named-pipe IPC, game detection, JSON profiles, FPS input | Done |
| 2 | High-resolution pacer, DX11 test renderer, benchmark proving precision | Done |
| 3 | DX11 backend: startup or automatic running-process attach, import/swap-chain interception, pacing before present, frame timestamps | Experimental; broad game compatibility remains |
| 4 | DX12 on the shared DXGI path, command-queue detection and independent probe | Experimental; broad game compatibility remains |
| 5 | Profile auto-activation polish, tray icon, global hotkeys (Ctrl+Shift+F / + / -), close-to-tray setting | Planned |
| 6 | Stability matrix: windowed / borderless / fullscreen, Alt+Tab, resolution and monitor changes, game restart, sleep/resume | Planned |

Later (architected for, not started): Vulkan, OpenGL, DX9, presentation-edge pacing, VRR-aware pacing,
Reflex, frame-generation awareness, in-swap-chain OSD for exclusive fullscreen, frametime capture and benchmark export, signed updater.

## Rules carried into every phase

* Measure before optimizing: every pacing change re-runs `FrameFlow.Benchmark` (mean error, P95, P99, CPU).
* No anti-cheat evasion of any kind. Protected games are skipped by default; any attempt requires
  an explicit per-game risk choice and may still be blocked by the game.
* The hook never throws across API boundaries and fails open where possible. The current experimental
  hook is pinned in the process until game exit; runtime detach is not implemented.
* No proprietary code or assets from other frame limiters; GPL code is not copied into FrameFlow.
