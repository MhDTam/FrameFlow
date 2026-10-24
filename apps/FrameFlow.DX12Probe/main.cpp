// Minimal DX12 Present smoke target. It intentionally does not call PresentPacer directly;
// FrameFlow.Launch must discover its DXGI swap chain and pace it through FrameFlow.Hook64.dll.
#include "IPC/SharedSession.h"
#include "Telemetry/FrameStatistics.h"
#include "Timing/Clock.h"

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <shellapi.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

template<class T> struct ComPtr {
    T* ptr = nullptr;
    ~ComPtr() { if (ptr) ptr->Release(); }
    T** operator&() { return &ptr; }
    T* operator->() const { return ptr; }
    explicit operator bool() const { return ptr != nullptr; }
};

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_DESTROY) { ::PostQuitMessage(0); return 0; }
    return ::DefWindowProcW(hwnd, message, wp, lp);
}

struct Options {
    double fps = 0.0; // test-only control of the hook session
    double seconds = 6.0;
    std::wstring report;
};

Options ParseOptions()
{
    Options result;
    int argc = 0;
    wchar_t** argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--fps" && i + 1 < argc) result.fps = _wtof(argv[++i]);
        else if (arg == L"--seconds" && i + 1 < argc) result.seconds = _wtof(argv[++i]);
        else if (arg == L"--report" && i + 1 < argc) result.report = argv[++i];
    }
    if (argv) ::LocalFree(argv);
    return result;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
    const Options options = ParseOptions();
    WNDCLASSW wc{};
    wc.hInstance = instance;
    wc.lpfnWndProc = WindowProc;
    wc.lpszClassName = L"FrameFlowDX12Probe";
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    if (!::RegisterClassW(&wc)) return 1;
    HWND hwnd = ::CreateWindowExW(0, wc.lpszClassName, L"FrameFlow DX12 Probe", WS_OVERLAPPEDWINDOW,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 960, 540, nullptr, nullptr, instance, nullptr);
    if (!hwnd) return 2;

    ComPtr<IDXGIFactory4> factory;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain1> chain;
    if (FAILED(::CreateDXGIFactory2(0, IID_PPV_ARGS(&factory.ptr)))) return 3;
    if (FAILED(::D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.ptr)))) return 4;
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue.ptr)))) return 5;
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferCount = 2;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (FAILED(factory->CreateSwapChainForHwnd(queue.ptr, hwnd, &desc, nullptr, nullptr, &chain.ptr))) return 6;
    ::ShowWindow(hwnd, show);

    std::unique_ptr<ff::ipc::SharedSession> session;
    std::vector<double> frametimes;
    const auto start = ff::timing::Clock::Now();
    std::int64_t lastPresent = 0;
    bool running = true;
    while (running) {
        MSG msg{};
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        if (!running || ff::timing::Clock::TicksToMs(ff::timing::Clock::Now() - start) >= options.seconds * 1000.0) break;
        if (FAILED(chain->Present(0, 0))) return 7;
        const auto now = ff::timing::Clock::Now();
        if (lastPresent) frametimes.push_back(ff::timing::Clock::TicksToMs(now - lastPresent));
        lastPresent = now;
        if (!session) session = ff::ipc::SharedSession::Open(::GetCurrentProcessId());
        if (session && options.fps > 0.0) {
            auto& c = session->Block().control;
            c.targetFpsMilli.store(static_cast<std::int64_t>(options.fps * 1000.0));
            c.mode.store(1);
            c.pacingEnabled.store(1);
            c.engineHeartbeatQpc.store(now);
            c.engineAttached.store(1);
        }
    }

    if (!options.report.empty()) {
        std::size_t skip = 0;
        for (double elapsed = 0; skip < frametimes.size() && elapsed < 1000.0; ++skip)
            elapsed += frametimes[skip];
        const std::vector<double> steady(frametimes.begin() + static_cast<std::ptrdiff_t>(skip), frametimes.end());
        const auto stats = ff::telemetry::ComputeStats(steady);
        std::ofstream out(std::filesystem::path(options.report), std::ios::trunc);
        out << "DX12 hook session: " << (session ? "yes" : "no") << '\n';
        if (session) {
            out << "Graphics API: " << session->Block().client.graphicsApi << '\n';
            out << "Present count: " << session->Block().client.presentCount.load() << '\n';
            out << "Pacing active: " << session->Block().client.pacingActive.load() << '\n';
            out << "Applied FPS: " << session->Block().client.appliedFpsMilli.load() / 1000.0 << '\n';
        }
        out << "Measured FPS after first second: " << stats.averageFps << " over " << stats.frames << " intervals\n";
        out << "Frametime stddev ms: " << stats.stddevMs << '\n';
    }
    ::DestroyWindow(hwnd);
    return 0;
}
