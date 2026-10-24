// FrameFlow.TestRenderer — minimal DX11 renderer used to validate pacing without commercial games.
//
// It draws a scrolling bar whose position is derived from the QPC clock, so any uneven frame
// delivery shows up as visible judder. Pacing goes through ff::graphics::PresentPacer — exactly
// the code path the future DXGI hook will run inside games — which means the engine can detect it,
// attach to it and control it like a real game.
//
//   FrameFlow.TestRenderer.exe                      uncapped until FrameFlow.Engine takes control
//   FrameFlow.TestRenderer.exe --fps 60             standalone pacing at 60 FPS
//   FrameFlow.TestRenderer.exe --fps 144 --seconds 20 --report out.txt
//   Options: --mode Efficiency|Balanced|Precision  --load-ms <avg render cost>  --vsync

#include "Core/Log.h"
#include "Core/Strings.h"
#include "Graphics/PresentPacer.h"
#include "IPC/SharedSession.h"
#include "Telemetry/FrameStatistics.h"
#include "Timing/Clock.h"

#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_5.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {

template <class T>
struct ComPtr {
    T* p = nullptr;
    ~ComPtr() { Reset(); }
    void Reset() { if (p) { p->Release(); p = nullptr; } }
    T** operator&() { Reset(); return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

struct Options {
    double fps = 0.0;            // 0 = no local limit
    ff::timing::PacingMode mode = ff::timing::PacingMode::Balanced;
    double seconds = 0.0;        // 0 = run until closed
    double loadMs = 0.0;
    bool vsync = false;
    bool external = false;       // pacing comes only from FrameFlow.Hook64.dll
    double externalTestFps = 0.0; // test-only: drive the hook session without the engine
    std::wstring report;
    int width = 1280;
    int height = 720;
};

Options ParseArgs()
{
    Options o;
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) {
        const std::wstring a = argv[i];
        auto next = [&]() -> std::wstring { return (i + 1 < argc) ? std::wstring(argv[++i]) : std::wstring(); };
        if (a == L"--fps") o.fps = _wtof(next().c_str());
        else if (a == L"--seconds") o.seconds = _wtof(next().c_str());
        else if (a == L"--load-ms") o.loadMs = _wtof(next().c_str());
        else if (a == L"--vsync") o.vsync = true;
        else if (a == L"--external") o.external = true;
        else if (a == L"--external-test-fps") { o.external = true; o.externalTestFps = _wtof(next().c_str()); }
        else if (a == L"--report") o.report = next();
        else if (a == L"--width") o.width = _wtoi(next().c_str());
        else if (a == L"--height") o.height = _wtoi(next().c_str());
        else if (a == L"--mode") ff::timing::ParsePacingMode(ff::core::WideToUtf8(next()), o.mode);
    }
    if (argv) ::LocalFree(argv);
    o.width = std::max(320, o.width);
    o.height = std::max(240, o.height);
    return o;
}

bool g_resized = false;
bool g_quit = false;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SIZE: g_resized = true; return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) ::DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY: g_quit = true; ::PostQuitMessage(0); return 0;
    default: return ::DefWindowProcW(hwnd, msg, wp, lp);
    }
}

void BusyWork(double ms)
{
    if (ms <= 0.0) return;
    const std::int64_t end = ff::timing::Clock::Now() + ff::timing::Clock::MsToTicks(ms);
    volatile double x = 1.0;
    while (ff::timing::Clock::Now() < end) x = x * 1.0000001 + 0.5;
}

class Renderer {
public:
    bool Init(HWND hwnd, bool vsync)
    {
        hwnd_ = hwnd;
        vsync_ = vsync;
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        ComPtr<ID3D11DeviceContext> ctx;
        HRESULT hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                         levels, 2, D3D11_SDK_VERSION, &device_.p, nullptr, &ctx.p);
        if (FAILED(hr)) return false;
        if (FAILED(ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&ctx1_.p)))) return false;

        ComPtr<IDXGIDevice> dxgiDevice;
        device_->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice.p));
        ComPtr<IDXGIAdapter> adapter;
        dxgiDevice->GetAdapter(&adapter.p);
        ComPtr<IDXGIFactory2> factory;
        adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory.p));

        ComPtr<IDXGIFactory5> f5;
        if (SUCCEEDED(factory->QueryInterface(__uuidof(IDXGIFactory5), reinterpret_cast<void**>(&f5.p)))) {
            BOOL allow = FALSE;
            if (SUCCEEDED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow))))
                tearing_ = allow != FALSE;
        }

        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.Flags = tearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
        if (FAILED(factory->CreateSwapChainForHwnd(device_.p, hwnd, &sd, nullptr, nullptr, &swap_.p))) return false;
        factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        return CreateTarget();
    }

    bool Resize()
    {
        rtv_.Reset();
        const UINT flags = tearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
        if (FAILED(swap_->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, flags))) return false;
        return CreateTarget();
    }

    void Draw(double seconds)
    {
        RECT rc{};
        ::GetClientRect(hwnd_, &rc);
        const LONG w = std::max<LONG>(1, rc.right);
        const LONG h = std::max<LONG>(1, rc.bottom);

        const float bg[4] = {0.89f, 0.99f, 0.99f, 1.0f};  // #E3FDFD
        ctx1_->ClearView(rtv_.p, bg, nullptr, 0);

        // Grid of soft columns for a stable reference.
        const float col[4] = {0.80f, 0.945f, 0.96f, 1.0f}; // #CBF1F5
        for (LONG x = 0; x < w; x += 80) {
            D3D11_RECT r{x, 0, x + 2, h};
            ctx1_->ClearView(rtv_.p, col, &r, 1);
        }

        // Moving bar: position from wall-clock time (not frame count), 0.5 screen widths per second.
        const double speed = 0.5 * static_cast<double>(w);
        const LONG barW = std::max<LONG>(24, w / 40);
        const LONG x = static_cast<LONG>(std::fmod(seconds * speed, static_cast<double>(w + barW))) - barW;
        const float bar[4] = {0.443f, 0.788f, 0.808f, 1.0f}; // #71C9CE
        D3D11_RECT r{x, h / 6, x + barW, h - h / 6};
        ctx1_->ClearView(rtv_.p, bar, &r, 1);

        // A second, thin fast bar makes micro-stutter easier to see.
        const LONG x2 = static_cast<LONG>(std::fmod(seconds * speed * 2.0, static_cast<double>(w)));
        const float ink[4] = {0.118f, 0.204f, 0.282f, 1.0f};
        D3D11_RECT r2{x2, h - h / 8, x2 + 6, h - h / 8 + 24};
        ctx1_->ClearView(rtv_.p, ink, &r2, 1);
    }

    HRESULT Present()
    {
        const UINT interval = vsync_ ? 1u : 0u;
        const UINT flags = (!vsync_ && tearing_) ? DXGI_PRESENT_ALLOW_TEARING : 0u;
        return swap_->Present(interval, flags);
    }

private:
    bool CreateTarget()
    {
        ComPtr<ID3D11Texture2D> back;
        if (FAILED(swap_->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back.p)))) return false;
        return SUCCEEDED(device_->CreateRenderTargetView(back.p, nullptr, &rtv_.p));
    }

    HWND hwnd_ = nullptr;
    bool vsync_ = false;
    bool tearing_ = false;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext1> ctx1_;
    ComPtr<IDXGISwapChain1> swap_;
    ComPtr<ID3D11RenderTargetView> rtv_;
};

void WriteReport(const Options& o, const std::vector<double>& ft, const ff::graphics::PresentPacer& pacer)
{
    // Drop the first second: window creation and swap-chain warm-up are not steady state.
    std::size_t skip = 0;
    for (double acc = 0; skip < ft.size() && acc < 1000.0; ++skip) acc += ft[skip];
    const std::vector<double> steady(ft.begin() + static_cast<std::ptrdiff_t>(skip), ft.end());
    const auto s = ff::telemetry::ComputeStats(steady);

    const double target = pacer.PacingActive() ? 1000.0 / pacer.AppliedFps() : 0.0;
    std::vector<double> dev;
    dev.reserve(steady.size());
    for (double v : steady) dev.push_back(std::fabs(v - target));

    char buf[1024];
    std::snprintf(buf, sizeof(buf),
                  "FrameFlow.TestRenderer report\n"
                  "Pacing: %s (%s), target %.3f FPS, controlled by %s\n"
                  "Frames: %zu over %.2f s (first second excluded)\n"
                  "Average FPS: %.3f   1%% low: %.2f   0.1%% low: %.2f\n"
                  "Frametime avg %.4f ms  min %.4f  max %.4f  stddev %.4f\n"
                  "Deviation from target: P95 %.4f ms  P99 %.4f ms  max %.4f ms\n",
                  pacer.PacingActive() ? "on" : "off", std::string(ff::timing::ToString(pacer.AppliedMode())).c_str(),
                  pacer.PacingActive() ? pacer.AppliedFps() : 0.0, pacer.EngineControlled() ? "engine" : "local",
                  s.frames, s.windowSeconds, s.averageFps, s.low1Fps, s.low01Fps, s.avgFrametimeMs, s.minFrametimeMs,
                  s.maxFrametimeMs, s.stddevMs, target > 0 ? ff::telemetry::Percentile(dev, 95) : 0.0,
                  target > 0 ? ff::telemetry::Percentile(dev, 99) : 0.0,
                  target > 0 ? *std::max_element(dev.begin(), dev.end()) : 0.0);
    FF_LOG_INFO("{}", buf);
    if (!o.report.empty()) {
        std::ofstream out(std::filesystem::path(o.report), std::ios::trunc);
        out << buf;
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show)
{
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ff::core::Log::Initialize(L"testrenderer");
    const Options opt = ParseArgs();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = ::LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"FrameFlowTestRenderer";
    ::RegisterClassExW(&wc);

    RECT r{0, 0, opt.width, opt.height};
    ::AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = ::CreateWindowExW(0, wc.lpszClassName, L"FrameFlow Test Renderer", WS_OVERLAPPEDWINDOW,
                                  CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr,
                                  inst, nullptr);
    if (!hwnd) return 1;

    Renderer renderer;
    if (!renderer.Init(hwnd, opt.vsync)) {
        ::MessageBoxW(hwnd, L"Could not initialize Direct3D 11.", L"FrameFlow Test Renderer", MB_ICONERROR);
        return 1;
    }
    ::ShowWindow(hwnd, show);

    std::unique_ptr<ff::graphics::PresentPacer> pacer;
    std::unique_ptr<ff::ipc::SharedSession> externalSession;
    if (!opt.external) {
        pacer = std::make_unique<ff::graphics::PresentPacer>("DX11");
        pacer->SetLocal(opt.fps > 0.0, opt.fps > 0.0 ? opt.fps : 60.0, opt.mode);
    }

    std::mt19937 rng(1234);
    std::normal_distribution<double> jitter(0.0, 0.25);
    std::vector<double> frametimes;
    frametimes.reserve(1 << 16);

    const std::int64_t start = ff::timing::Clock::Now();
    std::int64_t lastPresent = 0;
    std::int64_t lastTitle = start;
    std::size_t framesSinceTitle = 0;

    MSG msg{};
    while (!g_quit) {
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        if (g_quit) break;
        if (g_resized) {
            g_resized = false;
            renderer.Resize();
        }

        const std::int64_t now = ff::timing::Clock::Now();
        const double t = ff::timing::Clock::TicksToMs(now - start) / 1000.0;
        if (opt.seconds > 0.0 && t >= opt.seconds) break;

        BusyWork(opt.loadMs > 0.0 ? std::max(0.0, opt.loadMs + jitter(rng) * opt.loadMs) : 0.0);
        renderer.Draw(t);

        if (pacer) pacer->BeforePresent();
        renderer.Present();
        if (pacer) pacer->AfterPresent();
        const std::int64_t presentAt = ff::timing::Clock::Now();
        if (opt.external && !externalSession) externalSession = ff::ipc::SharedSession::Open(::GetCurrentProcessId());
        if (externalSession && opt.externalTestFps > 0.0) {
            auto& c = externalSession->Block().control;
            c.targetFpsMilli.store(static_cast<std::int64_t>(opt.externalTestFps * 1000.0));
            c.mode.store(static_cast<int>(opt.mode));
            c.pacingEnabled.store(1);
            c.engineHeartbeatQpc.store(presentAt);
            c.engineAttached.store(1);
        }

        if (lastPresent != 0) frametimes.push_back(ff::timing::Clock::TicksToMs(presentAt - lastPresent));
        if (frametimes.size() > (1u << 20)) frametimes.erase(frametimes.begin(), frametimes.begin() + (1 << 19));
        lastPresent = presentAt;

        ++framesSinceTitle;
        const double sinceTitle = ff::timing::Clock::TicksToMs(presentAt - lastTitle);
        if (sinceTitle >= 500.0) {
            wchar_t title[200];
            const wchar_t* state = opt.external ? L"external DX11 hook"
                : pacer->PacingActive()
                    ? (pacer->EngineControlled() ? L"paced by FrameFlow" : L"paced (standalone)")
                    : (pacer->EngineControlled() ? L"uncapped, FrameFlow pacing off" : L"uncapped");
            swprintf_s(title, L"FrameFlow Test Renderer — %.1f FPS — %s", framesSinceTitle * 1000.0 / sinceTitle, state);
            ::SetWindowTextW(hwnd, title);
            lastTitle = presentAt;
            framesSinceTitle = 0;
        }
    }

    if (!opt.external && (opt.seconds > 0.0 || !opt.report.empty())) WriteReport(opt, frametimes, *pacer);
    if (opt.external && !opt.report.empty()) {
        std::size_t skip = 0;
        for (double elapsed = 0; skip < frametimes.size() && elapsed < 1000.0; ++skip)
            elapsed += frametimes[skip];
        const std::vector<double> steady(frametimes.begin() + static_cast<std::ptrdiff_t>(skip), frametimes.end());
        const auto stats = ff::telemetry::ComputeStats(steady);
        std::ofstream out(std::filesystem::path(opt.report), std::ios::trunc);
        out << "External DX11 hook session: " << (externalSession ? "yes" : "no") << '\n';
        if (externalSession) {
            out << "Present count: " << externalSession->Block().client.presentCount.load() << '\n';
            out << "Pacing active: " << externalSession->Block().client.pacingActive.load() << '\n';
            out << "Applied FPS: " << externalSession->Block().client.appliedFpsMilli.load() / 1000.0 << '\n';
        }
        out << "Measured FPS after first second: " << stats.averageFps << " over " << stats.frames << " intervals\n";
        out << "Frametime stddev ms: " << stats.stddevMs << '\n';
    }
    ff::core::Log::Shutdown();
    return 0;
}
