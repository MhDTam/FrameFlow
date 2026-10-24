# DXGI (shared by DX11 and DX12)

The experimental DX11/DX12 hook lives in `native/Hooks/DxgiHook.cpp`. It discovers swap chains through
standard DX11/DXGI imports, intercepts `IDXGISwapChain::Present` and `Present1`, and calls the same
`PresentPacer` used by `FrameFlow.TestRenderer`. It selects one visible swap chain and can switch after
the selected chain stops presenting for two seconds. Compatibility across fullscreen transitions,
device loss and dynamically loaded graphics APIs still needs real-game testing. The module stays
loaded until game exit; runtime detach is not supported.
