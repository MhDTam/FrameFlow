# Hooks (FrameFlow.Hook64.dll)

Experimental x64 DX11/DX12 in-game module. `FrameFlow.Launch.exe` loads it either at startup or
into a selected running game. It hooks standard DX11/DXGI imports and best-effort existing DXGI
Present vtables to discover swap chains, then calls
`PresentPacer::BeforePresent()` at the front edge of `Present` / `Present1`.
Euro Truck Simulator 2 uses a MinHook function detour for Present instead of rewriting its
vtable slot, after the user selects FrameFlow and enables Frame Pacing. The diagnostic script
adds `--diagnose-unsafe` to capture the first few Present calls; ordinary attachment does not.

Hard rules:
* Protected games are skipped unless the user accepts the risk for that specific game profile.
* No stealth: no module hiding, no manual mapping, no anti-cheat interaction of any kind.
* No C++ exceptions across hook boundaries; every entry point is `noexcept` and fails open (calls the original).
* The module is pinned for the game process lifetime because import and vtable entries point into it.
  Runtime detach is not supported yet.
* Talks to the engine only through `IPC/SharedSession` — never loads paths or DLLs received from outside.

The default vtable hook repeatedly crashed Euro Truck Simulator 2. A captured stack showed
Steam Overlay recursively calling that hook. The alternate Present detour remained connected
with Steam Overlay enabled during manual and user-triggered automatic attachment tests at
70 FPS. One attached run exited unexpectedly without a new Windows crash report; another
remained open through more than two minutes of driving. The cause of the exit is unresolved.
To collect diagnostics, enable
Windows Error Reporting minidumps with `tools/ets2-crash-dump.ps1 -Action Enable`, launch the
game normally through Steam, then run `tools/diagnose-ets2.ps1` while it is running.
The diagnostic script explicitly opts into the known-unsafe late attach; it does not change the
normal game's profile or bypass protected-game checks. After testing, run
`tools/ets2-crash-dump.ps1 -Action Disable`. Dumps are written under `%LOCALAPPDATA%\FrameFlow\CrashDumps\`
and can contain private process data; do not upload them with a public issue.
