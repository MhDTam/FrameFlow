# FrameFlow IPC protocol — version 1

Two channels connect the three FrameFlow processes.

```
FrameFlow.UI.exe  ── named pipe (JSON lines) ──  FrameFlow.Engine.exe  ── shared memory ──  paced process
   (WinUI 3)          \\.\pipe\FrameFlow.Engine.v1       (C++20)         Local\FrameFlow.Session.<pid>   (TestRenderer today,
                                                                                                          Hook64.dll in games later)
```

## 1. UI ⇄ engine: named pipe

* Pipe: `\\.\pipe\FrameFlow.Engine.v1`, byte mode, one client at a time.
* DACL: current user + SYSTEM only. Remote clients are rejected (`PIPE_REJECT_REMOTE_CLIENTS`).
* Framing: UTF-8 JSON, one object per line, `\n` terminated. Lines over 64 KiB close the connection.
* Every message carries `"v": 1` and `"type"`. Requests may carry an integer `"id"`; the engine answers
  those with `Ack`.
* The engine treats all input as untrusted: FPS must be a number in 10–1000, paths must be absolute
  local `.exe` files that exist, exclusion names are validated, and unknown types are rejected.

### UI → engine

| type | payload | effect |
|---|---|---|
| `Hello` | `client`, `version` | engine replies with `EngineStatus` |
| `GetState` | — | engine replies with `EngineStatus` |
| `SetFpsLimit` | `fps` (number, 10–1000, 3 decimals kept) | current game's profile, or the default when no game |
| `EnablePacing` / `DisablePacing` | — | toggles pacing for the current game immediately |
| `SetPacingMode` | `mode`: `Efficiency` \| `Balanced` \| `Precision` | per game, or default |
| `SetLimitMethod` | `method`: `auto` \| `frameflow` \| `driver` | saves the current game's limiter preference; FrameFlow requires an attached session |
| `SelectGame` | `path` (absolute .exe) | pins a game manually; switches now if it is running |
| `ClearManualGame` | `profileId` (optional) | removes a manual pin |
| `SetProtectedRisk` | `allow` (boolean) | per-game consent for a detected protected game; defaults to false |
| `SetEngineSettings` | any of `autoDetect`, `miniOverlay`, `telemetrySeconds` (10–60), `defaultFps`, `defaultMode`, `extraExclusions` (string[]) | persisted to `engine-settings.json` |
| `SetLogLevel` | `level`: `TRACE` … `ERROR` | runtime + persisted |
| `Shutdown` | — | releases all games, then exits |

### Engine → UI

| type | when | payload |
|---|---|---|
| `Ack` | reply to a request with `id` | `id`, `ok`, `error?` |
| `EngineStatus` | on connect and on any state change | `version`, `fpsLimit`, `pacingMode`, `pacingEnabled`, `limitMethod`, `limitMethodPreference`, `display{device,width,height,refreshHz,vrr}`, `presets[]`, `suggestedFps`, `integration{status,title,message,antiCheat?,riskAccepted?}`, `game{pid,name,exe,exePath,api,profileId,manual,reasons[]}` or `null`, `candidates[]`, `settings{…}` |
| `GameDetected` / `GameClosed` | game lifecycle | `exe`, `name`, `pid?` |
| `TelemetryUpdate` | ~15 Hz while attached, 1 Hz otherwise | `source` (`live` \| `none`), `pacingActive`, `appliedFps`, `stats{currentFps, averageFps, frametimeMs, avgFrametimeMs, minFrametimeMs, maxFrametimeMs, stddevMs, low1Fps, low01Fps, frames, windowSeconds}`, `graph{historySeconds, mean[], max[]}` |
| `Error` | malformed input | `code`, `message` |

`integration.status` values: `None`, `Active`, `Standby`, `NotAvailable`, `Protected`, `AccessDenied`.

## 2. Engine ⇄ paced process: shared memory

Section `Local\FrameFlow.Session.<pid>`, created by the paced process, opened by the engine. Layout
(`native/IPC/SharedSession.h`, version 5, magic `FFS1`):

* `control` (engine writes): `generation`, `pacingEnabled`, `targetFpsMilli`, `mode`, `engineAttached`,
  `engineHeartbeatQpc`.
* `client` (paced process writes): `pid`, `graphicsApi`, `qpcFrequency`, `heartbeatQpc`, `pacingActive`,
  `appliedFpsMilli`, `presentCount`.
* `ring`: single-producer/single-consumer ring of 16384 sets of `previousAfterPresentQpc`,
  `hookEntryQpc`, `preWaitQpc`, and `presentQpc` timestamps (QPC ticks). They separate the preceding
  DXGI `Present` call, time outside the hook, hook bookkeeping, and FrameFlow's wait.

All shared fields are lock-free atomics, so neither side can block the other. If the engine heartbeat
is older than 2 s the paced process falls back to its local configuration — for a hook that is
"pacing off", so a crashed engine can never leave a game limited.
