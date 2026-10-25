# Hooks (FrameFlow.Hook64.dll)

Experimental x64 DX11/DX12 in-game module. `FrameFlow.Launch.exe` loads it either at startup or
into a selected running game. It hooks standard DX11/DXGI imports and best-effort existing DXGI
Present vtables to discover swap chains, then calls
`PresentPacer::BeforePresent()` at the front edge of `Present` / `Present1`.

Hard rules:
* Protected games are skipped unless the user accepts the risk for that specific game profile.
* No stealth: no module hiding, no manual mapping, no anti-cheat interaction of any kind.
* No C++ exceptions across hook boundaries; every entry point is `noexcept` and fails open (calls the original).
* The module is pinned for the game process lifetime because import and vtable entries point into it.
  Runtime detach is not supported yet.
* Talks to the engine only through `IPC/SharedSession` — never loads paths or DLLs received from outside.

Euro Truck Simulator 2 is blocked from automatic attachment after repeated crashes. To investigate
it, enable Windows Error Reporting minidumps with `tools/ets2-crash-dump.ps1 -Action Enable`,
disable Steam Overlay for this game in Steam properties, launch the game normally through Steam,
then run `tools/diagnose-ets2.ps1` while it is running. The launcher refuses the diagnostic
attach if the Steam overlay module is still loaded, because a captured call stack showed it
recursively calling FrameFlow's DXGI Present hook until the game crashed.
The diagnostic script explicitly opts into the known-unsafe late attach; it does not change the
normal game's profile or bypass protected-game checks. After testing, run
`tools/ets2-crash-dump.ps1 -Action Disable`. Dumps are written under `%LOCALAPPDATA%\FrameFlow\CrashDumps\`
and can contain private process data; do not upload them with a public issue.
