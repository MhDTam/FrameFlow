# FrameFlow architecture

## Processes

```
FrameFlow.UI.exe (WinUI 3)
   │  named pipe  \\.\pipe\FrameFlow.Engine.v1   (JSON lines, protocol v1)
   ▼
FrameFlow.Engine.exe (C++20, windowless)
   │  threads: IPC · detection (1 Hz) · telemetry (15 Hz)
   │  shared memory  Local\FrameFlow.Session.<pid>   (lock-free control block + present ring)
   ▼
paced process ── PresentPacer ── FramePacer ── Present()
   (FrameFlow.TestRenderer, FrameFlow.DX12Probe, or FrameFlow.Hook64.dll in an opt-in DX11/DX12 game)
```

The UI never does timing work and the engine never renders. The timing-critical code runs inside the
paced process, on its presenting thread, so pacing cost is one function call before `Present`.

The experimental DX11/DX12 launcher briefly debugs a newly started game to pause at its executable entry
point, detaches, loads `FrameFlow.Hook64.dll`, then lets the game run. For games started normally,
the engine starts the same helper in `--attach` mode after detecting the selected game's process.
The hook patches standard DX11/DXGI import slots and the swap chain's `Present`/`Present1` vtable
slots. A temporary DX11 swap chain also primes common DXGI Present vtables for late attachment.
For DX12,
the `ID3D12CommandQueue` passed during swap-chain creation marks the shared session as DX12. It uses the
same `PresentPacer` and shared session as the test renderer. The module stays pinned until process
exit; runtime detach and broad game compatibility are future work.

The optional mini OSD is a FrameFlow-owned layered, click-through Win32 window on an engine thread.
It follows the selected game's client area and draws FPS, 1% low, frametime and a short graph using
GDI. It hides when the game loses focus or is minimized and never participates in measurement or
pacing. Because it is a desktop window, exclusive fullscreen may cover it.

## Pacing algorithm (native/Timing/FramePacer.cpp)

Deadlines come from an absolute timeline: `deadline[n] = base + round(n × period)` with a fractional
period in QPC ticks. Nothing is accumulated frame to frame, so error never drifts.

* Arrived early → wait until the deadline.
* Arrived late by less than one period → release immediately and keep the grid (average stays exact).
* Arrived more than one period late (loading screen, alt-tab, hitch) → re-anchor the timeline at "now"
  instead of releasing a burst of catch-up frames.

Each wait has three phases:

1. **Timer**: a `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` timer sleeps until `deadline − margin`. No CPU.
   Without high-resolution timers the fallback raises timer resolution with `timeBeginPeriod(1)` for the
   lifetime of that timer object only.
2. **Yield**: `SwitchToThread()` until `deadline − spin`, giving the core to any ready thread.
3. **Spin**: `_mm_pause()` on QPC until the deadline.

The margin adapts: an EWMA of timer lateness (mean + 4 × mean absolute deviation) widens it on noisy
systems, bounded per mode. Modes only change the margins:

| Mode | base timer margin | spin | adaptive cap |
|---|---:|---:|---:|
| Efficiency | 400 µs | 40 µs | 1000 µs |
| Balanced | 700 µs | 100 µs | 2000 µs |
| Precision | 1200 µs | 250 µs | 3000 µs |

## Statistics (native/Telemetry/FrameStatistics.cpp)

From real present-to-present intervals `ft_i` over the telemetry window (30 s by default):

* average FPS = N / Σ ft_i (time-weighted; not the mean of per-frame FPS)
* current FPS = the same over the most recent 500 ms
* 1% low = 1000 / mean(slowest 1% of ft_i); 0.1% low likewise (at least one frame)
* deviation = population standard deviation of ft_i
* graph: the window split into 240 time buckets; each bucket's mean and max frametime

## Game detection (native/ProcessDetection)

Candidates are processes with a visible top-level window. Signals and weights: foreground 25, visible
window 10, fullscreen/borderless 25, D3D11/D3D12/Vulkan 25 (DXGI only 15, DX9 20, OpenGL 10), store
library path 30, launched by a store launcher 20, FrameFlow session 100. Threshold 60, and at least one
game-specific signal (fullscreen, store path, launcher parent, or session) is required, so windowed
desktop apps that happen to use D3D are never picked. A configurable exclusion list is applied first.
Manual selections always win.

## Failure behaviour

* Engine gone → paced process reverts to local settings (off for a hook) within 2 s.
* UI gone → engine keeps pacing; closing the window shuts the engine down until tray support exists.
* All IPC input is validated (FPS range, absolute existing .exe paths, message size, depth-limited JSON).
