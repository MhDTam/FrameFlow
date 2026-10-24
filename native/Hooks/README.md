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
