#include "EngineApp.h"

#include "Core/Log.h"
#include "Core/Paths.h"
#include "Core/Strings.h"
#include "Display/FpsPresets.h"
#include "IPC/Protocol.h"
#include "ProcessDetection/AntiCheatDetector.h"
#include "ProcessDetection/HookCompatibility.h"
#include "Telemetry/FrameStatistics.h"
#include "Timing/Clock.h"
#include "Timing/FramePacer.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace ff::engine {

namespace proto = ipc::protocol;
using core::Json;

namespace {

constexpr auto kDetectionInterval = std::chrono::milliseconds(1000);
constexpr auto kTelemetryInterval = std::chrono::milliseconds(66);  // ~15 Hz to the UI
constexpr std::size_t kGraphBuckets = 240;
constexpr std::size_t kMaxCandidatesInStatus = 8;

std::filesystem::path EngineSettingsPath() { return core::AppDataRoot() / L"engine-settings.json"; }

std::wstring QuoteArg(std::wstring_view value)
{
    std::wstring out = L"\"";
    for (wchar_t c : value) {
        if (c == L'\"') out += L'\\';
        out += c;
    }
    return out + L'\"';
}

const char* ToString(Integration i)
{
    switch (i) {
    case Integration::None: return "None";
    case Integration::Active: return "Active";
    case Integration::Standby: return "Standby";
    case Integration::NotAvailable: return "NotAvailable";
    case Integration::Protected: return "Protected";
    case Integration::AccessDenied: return "AccessDenied";
    }
    return "None";
}

timing::PacingMode ModeFrom(const std::string& s)
{
    timing::PacingMode m = timing::PacingMode::Balanced;
    timing::ParsePacingMode(s, m);
    return m;
}

Json StatsToJson(const telemetry::FrameStats& s)
{
    auto j = Json::MakeObject();
    j.Set("frames", static_cast<std::uint64_t>(s.frames));
    j.Set("windowSeconds", s.windowSeconds);
    j.Set("currentFps", s.currentFps);
    j.Set("averageFps", s.averageFps);
    j.Set("frametimeMs", s.currentFps > 0 ? 1000.0 / s.currentFps : 0.0);
    j.Set("lastFrametimeMs", s.lastFrametimeMs);
    j.Set("avgFrametimeMs", s.avgFrametimeMs);
    j.Set("minFrametimeMs", s.minFrametimeMs);
    j.Set("maxFrametimeMs", s.maxFrametimeMs);
    j.Set("stddevMs", s.stddevMs);
    j.Set("low1Fps", s.low1Fps);
    j.Set("low01Fps", s.low01Fps);
    return j;
}

} // namespace

EngineApp::EngineApp()
    : settings_(EngineSettings::Load(EngineSettingsPath())),
      profiles_(core::ProfilesDir()),
      pipe_(proto::kPipeName,
            [this](const Json& m) { OnMessage(m); },
            [this](bool c) { OnConnection(c); })
{
    core::LogLevel lvl = core::LogLevel::Info;
    if (core::ParseLogLevel(settings_.logLevel, lvl)) core::Log::SetLevel(lvl);
    monitor_ = display::MonitorForWindow(nullptr);
}

EngineApp::~EngineApp() { RequestShutdown(); }

int EngineApp::Run()
{
    FF_LOG_INFO("FrameFlow.Engine {} starting (QPC {} Hz, display {}x{} @ {} Hz)", FF_VERSION_STRING,
                timing::Clock::Frequency(), monitor_.width, monitor_.height, monitor_.refreshHz);
    if (nvapi_.Available())
        FF_LOG_INFO("NVIDIA driver limiter available: {}", nvapi_.Info().gpuName);
    // Real-game frametime measurement via ETW. Needs admin; if unavailable, the driver limiter still
    // works and the UI explains that measurement needs elevation.
    measurement_ = presentMon_.Start();
    pipe_.Start();
    detection_ = std::thread([this] { DetectionLoop(); });
    telemetry_ = std::thread([this] { TelemetryLoop(); });

    {
        std::unique_lock lock(waitMutex_);
        wake_.wait(lock, [this] { return stop_.load(); });
    }

    if (detection_.joinable()) detection_.join();
    if (telemetry_.joinable()) telemetry_.join();
    {
        std::scoped_lock lock(mutex_);
        CloseCurrentGame("engine shutdown");
    }
    pipe_.Stop();
    FF_LOG_INFO("FrameFlow.Engine stopped");
    return 0;
}

void EngineApp::RequestShutdown()
{
    {
        std::scoped_lock lock(waitMutex_);
        stop_ = true;
    }
    wake_.notify_all();
}

// ---------------------------------------------------------------------------------------------
// Detection

void EngineApp::DetectionLoop()
{
    while (!stop_) {
        try {
            DetectOnce();
        } catch (const std::exception& e) {
            FF_LOG_ERROR("Detection cycle failed: {}", e.what());
        }
        std::unique_lock lock(waitMutex_);
        wake_.wait_for(lock, kDetectionInterval, [this] { return stop_.load(); });
    }
}

process::ExclusionList EngineApp::BuildExclusions() const
{
    process::ExclusionList ex;
    for (const auto& e : settings_.extraExclusions) ex.Add(e);
    return ex;
}

void EngineApp::DetectOnce()
{
    process::ExclusionList exclusions;
    {
        std::scoped_lock lock(mutex_);
        exclusions = BuildExclusions();
    }
    auto candidates = scanner_.Scan(exclusions);

    std::vector<Json> events;
    {
        std::scoped_lock lock(mutex_);
        lastCandidates_ = candidates;
        if (lastCandidates_.size() > kMaxCandidatesInStatus) lastCandidates_.resize(kMaxCandidatesInStatus);

        if (game_ && !process::ProcessScanner::IsProcessAlive(game_->pid)) {
            auto ev = proto::Make(proto::type::GameClosed);
            ev.Set("exe", game_->exeName);
            ev.Set("name", game_->profile.game);
            events.push_back(std::move(ev));
            CloseCurrentGame("process exited");
        }

        if (game_) {
            // Modules load lazily (a game may create its D3D device seconds after its window).
            for (const auto& c : candidates) {
                if (c.signals.pid != game_->pid) continue;
                if (!game_->session) game_->api = std::string(process::InferGraphicsApi(c.signals));
                game_->hwnd = c.hwnd;
                if (!game_->antiCheat) {
                    game_->antiCheat = process::DetectAntiCheat(game_->exeName, c.moduleNames,
                        scanner_.RunningProcessNames(), process::CollectNearbyFiles(game_->exePathW));
                    if (game_->antiCheat && !game_->profile.allowProtected) {
                        FF_LOG_WARN("Protection appeared after detection: {}. Disabling pacing.",
                                    game_->antiCheat->product);
                        ChooseLimitMethod();
                    }
                }
                break;
            }
            if (!game_->session) {
                TryAttachSession();
                if (game_->session) ChooseLimitMethod(); // a FrameFlow-aware renderer just appeared
                else TryAutoAttach();
            }
        } else {
            const process::Candidate* chosen = nullptr;
            for (const auto& c : candidates) {
                const auto id = profiles::MakeProfileId(c.signals.exeName, c.signals.exePath);
                if (std::find(settings_.manualProfileIds.begin(), settings_.manualProfileIds.end(), id) !=
                    settings_.manualProfileIds.end()) {
                    chosen = &c;
                    break;
                }
            }
            if (!chosen && settings_.autoDetect) {
                for (const auto& c : candidates) {
                    if (!c.score.excluded && c.score.score >= process::kGameScoreThreshold) {
                        chosen = &c;
                        break;
                    }
                }
            }
            if (chosen) {
                SwitchToGame(*chosen);
                auto ev = proto::Make(proto::type::GameDetected);
                ev.Set("exe", game_->exeName);
                ev.Set("name", game_->profile.game);
                ev.Set("pid", game_->pid);
                events.push_back(std::move(ev));
            }
        }
        monitor_ = display::MonitorForWindow(game_ ? game_->hwnd : nullptr);
    }
    for (const auto& e : events) pipe_.Send(e);
    SendStatus(false);
}

void EngineApp::SwitchToGame(const process::Candidate& c)
{
    auto g = std::make_unique<CurrentGame>();
    g->pid = c.signals.pid;
    g->exeName = c.signals.exeName;
    g->exePath = c.signals.exePath;
    g->exePathW = core::Utf8ToWide(g->exePath);
    g->api = std::string(process::InferGraphicsApi(c.signals));
    g->reasons = c.score.reasons;
    g->accessible = c.signals.accessible;
    g->hwnd = c.hwnd;
    g->profile = profiles_.LoadOrCreate(g->exeName, g->exePath, settings_.defaultFps, settings_.defaultMode);
    // Detection and saved preferences never activate limiting for a new process.
    g->profile.framePacing = false;

    g->antiCheat = process::DetectAntiCheat(g->exeName, c.moduleNames, scanner_.RunningProcessNames(),
                                            process::CollectNearbyFiles(core::Utf8ToWide(g->exePath)));
    FF_LOG_INFO("Game detected: {} ({}, pid {}, api {}, score {})", g->profile.game, g->exeName, g->pid, g->api,
                c.score.score);
    if (g->antiCheat) {
        FF_LOG_WARN("Protected game: {} ({}). Integration disabled for this process.", g->antiCheat->product,
                    g->antiCheat->evidence);
    }
    game_ = std::move(g);
    TryAttachSession();
    ChooseLimitMethod();
}

void EngineApp::TryAutoAttach()
{
    if (!game_ || game_->session || game_->attachAttempted || !game_->accessible ||
        !game_->profile.framePacing || game_->profile.limitMethod != "frameflow" ||
        (game_->antiCheat && !game_->profile.allowProtected) ||
        (game_->api != "DX11" && game_->api != "DX12" && game_->api != "DXGI")) return;
    game_->attachAttempted = true;
    wchar_t self[MAX_PATH]{};
    if (!::GetModuleFileNameW(nullptr, self, MAX_PATH)) return;
    const auto launcher = std::filesystem::path(self).parent_path() / L"FrameFlow.Launch.exe";
    if (!std::filesystem::exists(launcher)) return;
    std::wstring command = QuoteArg(launcher.wstring()) + L" ";
    if (game_->antiCheat) command += L"--allow-protected ";
    if (process::IsKnownUnsafeHookTarget(game_->exeName)) command += L"--detour-present ";
    command += L"--attach " + std::to_wstring(game_->pid) + L" " + QuoteArg(game_->exePathW);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if (::CreateProcessW(launcher.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                         nullptr, launcher.parent_path().c_str(), &startup, &child)) {
        ::CloseHandle(child.hThread);
        ::CloseHandle(child.hProcess);
        FF_LOG_INFO("User-requested attach for {} (pid {})", game_->exeName, game_->pid);
    } else {
        FF_LOG_WARN("User-requested attach failed to start for {}: {}", game_->exeName, ::GetLastError());
    }
}

void EngineApp::CloseCurrentGame(const char* reason)
{
    if (!game_) return;
    FF_LOG_INFO("Releasing {} ({})", game_->exeName, reason);
    if (game_->session) {
        auto& c = game_->session->Block().control;
        // Detach first so the client falls back to its local (off) configuration immediately.
        c.engineAttached.store(0, std::memory_order_release);
        c.pacingEnabled.store(0, std::memory_order_release);
        c.generation.fetch_add(1, std::memory_order_acq_rel);
    }
    if (game_->method == LimitMethod::NvidiaDriver) {
        nvapi_.ClearGameLimit(game_->exePathW);
    }
    if (presentMon_.Running()) presentMon_.SetTarget(0);
    miniOverlay_.Clear();
    game_.reset();
}

void EngineApp::TryAttachSession()
{
    if (!game_ || game_->session || (game_->antiCheat && !game_->profile.allowProtected)) return;
    game_->session = ipc::SharedSession::Open(game_->pid);
    if (!game_->session) return;
    FF_LOG_INFO("Attached to FrameFlow session in {} (api {})", game_->exeName, game_->session->Block().client.graphicsApi);
    game_->api = game_->session->Block().client.graphicsApi;
    if (process::IsKnownUnsafeHookTarget(game_->exeName) && game_->profile.limitMethod == "auto") {
        // A known-unsafe game can only reach this point through an explicit diagnostic attach.
        // Select the connected method for this process, but leave pacing disabled until the user enables it.
        game_->profile.limitMethod = "frameflow";
        FF_LOG_INFO("Diagnostic session selected FrameFlow for {}; pacing remains off", game_->exeName);
    }
    game_->reader.SkipToEnd(game_->session->Block());
}

// Only an explicitly chosen method can limit a game. Detection alone is read-only.
void EngineApp::ChooseLimitMethod()
{
    if (!game_) return;
    const auto oldMethod = game_->method;
    const bool allowed = !game_->antiCheat || game_->profile.allowProtected;
    const bool canHook = game_->session && allowed;
    const bool canDriver = allowed && game_->accessible && nvapi_.Available();
    const auto& preference = game_->profile.limitMethod;
    if (preference == "frameflow") game_->method = canHook ? LimitMethod::FramePacer : LimitMethod::None;
    else if (preference == "driver") game_->method = canDriver ? LimitMethod::NvidiaDriver : LimitMethod::None;
    else game_->method = LimitMethod::None;

    if (oldMethod == LimitMethod::NvidiaDriver && game_->method != oldMethod) {
        nvapi_.ClearGameLimit(game_->exePathW);
    }
    if (oldMethod == LimitMethod::FramePacer && game_->method != oldMethod && game_->session) {
        auto& c = game_->session->Block().control;
        c.pacingEnabled.store(0, std::memory_order_relaxed);
        c.engineAttached.store(0, std::memory_order_release);
        c.generation.fetch_add(1, std::memory_order_acq_rel);
    }

    if (presentMon_.Running()) presentMon_.SetTarget(game_->pid);
    ApplyProfile();
}

void EngineApp::ApplyProfile()
{
    if (!game_) return;
    // New settings start a fresh measurement window: old-limit frames would skew averages and the graph.
    game_->frametimes.clear();
    game_->frametimeTotalMs = 0.0;

    if (game_->method == LimitMethod::FramePacer && game_->session) {
        auto& c = game_->session->Block().control;
        c.targetFpsMilli.store(static_cast<std::int64_t>(game_->profile.fpsLimit * 1000.0 + 0.5), std::memory_order_relaxed);
        c.mode.store(static_cast<int>(ModeFrom(game_->profile.pacingMode)), std::memory_order_relaxed);
        c.pacingEnabled.store(game_->profile.framePacing ? 1 : 0, std::memory_order_relaxed);
        c.engineHeartbeatQpc.store(timing::Clock::Now(), std::memory_order_relaxed);
        c.engineAttached.store(1, std::memory_order_release);
        c.generation.fetch_add(1, std::memory_order_acq_rel);
    } else if (game_->method == LimitMethod::NvidiaDriver) {
        if (game_->profile.framePacing)
            nvapi_.SetGameLimit(game_->exePathW, static_cast<std::uint32_t>(game_->profile.fpsLimit + 0.5));
        else {
            nvapi_.ClearGameLimit(game_->exePathW);
        }
    }
}

void EngineApp::SaveProfileLocked()
{
    if (!game_) return;
    profiles::GameProfile toSave = game_->profile;
    profiles_.Save(toSave);
}

// ---------------------------------------------------------------------------------------------
// Telemetry

void EngineApp::TelemetryLoop()
{
    std::vector<double> scratch;
    scratch.reserve(16384);
    auto lastEmpty = std::chrono::steady_clock::time_point{};

    while (!stop_) {
        std::optional<Json> msg;
        {
            std::scoped_lock lock(mutex_);
            bool haveFrames = false;
            bool pacingActive = false;
            double appliedFps = 0.0;

            if (game_ && game_->method == LimitMethod::FramePacer && game_->session) {
                auto& block = game_->session->Block();
                block.control.engineHeartbeatQpc.store(timing::Clock::Now(), std::memory_order_relaxed);

                scratch.clear();
                const std::uint64_t epoch = block.client.configStartIndex.load(std::memory_order_acquire);
                if (epoch != game_->configEpoch) {
                    game_->configEpoch = epoch;
                    game_->frametimes.clear();
                    game_->frametimeTotalMs = 0.0;
                }
                game_->reader.ReadNew(block, scratch, epoch);

                // Until the client has actually applied the requested settings (it polls every ~50 ms),
                // its frames belong to the previous configuration: read them, but do not record them.
                const bool wantOn = block.control.pacingEnabled.load(std::memory_order_relaxed) != 0;
                const bool isOn = block.client.pacingActive.load(std::memory_order_relaxed) != 0;
                const bool fpsMatches = block.client.appliedFpsMilli.load(std::memory_order_relaxed) ==
                                        block.control.targetFpsMilli.load(std::memory_order_relaxed);
                if (wantOn != isOn || (wantOn && !fpsMatches)) scratch.clear();

                pacingActive = block.client.pacingActive.load() != 0;
                appliedFps = static_cast<double>(block.client.appliedFpsMilli.load()) / 1000.0;
                haveFrames = true;
            } else if (game_ && presentMon_.Running()) {
                // Measure real presents even when hooking is unavailable; useful for the mini OSD.
                scratch.clear();
                presentMon_.Drain(game_->pid, scratch);
                pacingActive = game_->method == LimitMethod::NvidiaDriver && game_->profile.framePacing;
                appliedFps = game_->profile.fpsLimit;
                haveFrames = true;
            }

            if (haveFrames) {
                for (double ft : scratch) {
                    if (ft > 2000.0) continue; // alt-tab-sized gaps are not frames
                    game_->frametimes.push_back(ft);
                    game_->frametimeTotalMs += ft;
                }
                const double limitMs = settings_.telemetrySeconds * 1000.0;
                while (!game_->frametimes.empty() && game_->frametimeTotalMs > limitMs) {
                    game_->frametimeTotalMs -= game_->frametimes.front();
                    game_->frametimes.pop_front();
                }

                std::vector<double> ft(game_->frametimes.begin(), game_->frametimes.end());
                const auto stats = telemetry::ComputeStats(ft);
                const auto graph = telemetry::BuildGraph(ft, settings_.telemetrySeconds, kGraphBuckets);

                if (settings_.miniOverlay)
                    miniOverlay_.Update(static_cast<HWND>(game_->hwnd), game_->pid,
                                        stats.currentFps, stats.lastFrametimeMs, stats.low1Fps,
                                        game_->profile.fpsLimit > 0 ? 1000.0 / game_->profile.fpsLimit : 0,
                                        game_->frametimes);
                else
                    miniOverlay_.Clear();

                Json m = proto::Make(proto::type::TelemetryUpdate);
                m.Set("source", ft.empty() ? "waiting" : "live");
                m.Set("pid", game_->pid);
                m.Set("method", game_->method == LimitMethod::NvidiaDriver ? "driver" : "frameflow");
                m.Set("pacingActive", pacingActive);
                m.Set("appliedFps", appliedFps);
                m.Set("stats", StatsToJson(stats));
                auto mean = Json::MakeArray();
                auto mx = Json::MakeArray();
                for (double v : graph.meanMs) mean.Push(std::round(v * 1000.0) / 1000.0);
                for (double v : graph.maxMs) mx.Push(std::round(v * 1000.0) / 1000.0);
                auto gj = Json::MakeObject();
                gj.Set("historySeconds", settings_.telemetrySeconds);
                gj.Set("mean", std::move(mean));
                gj.Set("max", std::move(mx));
                m.Set("graph", std::move(gj));
                msg = std::move(m);
            } else {
                if (settings_.miniOverlay && game_)
                    miniOverlay_.Update(static_cast<HWND>(game_->hwnd), game_->pid,
                                        0, 0, 0, game_->profile.fpsLimit > 0 ? 1000.0 / game_->profile.fpsLimit : 0,
                                        game_->frametimes);
                else
                    miniOverlay_.Clear();
                const auto now = std::chrono::steady_clock::now();
                if (now - lastEmpty > std::chrono::seconds(1)) {
                    lastEmpty = now;
                    Json m = proto::Make(proto::type::TelemetryUpdate);
                    m.Set("source", "none");
                    msg = std::move(m);
                }
            }
        }
        if (msg && pipe_.Connected()) pipe_.Send(*msg);

        std::unique_lock lock(waitMutex_);
        wake_.wait_for(lock, kTelemetryInterval, [this] { return stop_.load(); });
    }
}

// ---------------------------------------------------------------------------------------------
// Status

Integration EngineApp::IntegrationLocked() const
{
    if (!game_) return Integration::None;
    if (game_->antiCheat && !game_->profile.allowProtected) return Integration::Protected;
    if (game_->method == LimitMethod::FramePacer || game_->method == LimitMethod::NvidiaDriver)
        return game_->profile.framePacing ? Integration::Active : Integration::Standby;
    if (!game_->accessible) return Integration::AccessDenied;
    return Integration::NotAvailable;
}

Json EngineApp::BuildStatusLocked() const
{
    auto s = proto::Make(proto::type::EngineStatus);
    s.Set("version", FF_VERSION_STRING);
    auto es = Json::MakeObject();
    es.Set("autoDetect", settings_.autoDetect);
    es.Set("miniOverlay", settings_.miniOverlay);
    es.Set("telemetrySeconds", settings_.telemetrySeconds);
    es.Set("defaultFps", settings_.defaultFps);
    es.Set("defaultMode", settings_.defaultMode);
    es.Set("logLevel", settings_.logLevel);
    auto ex = Json::MakeArray();
    for (const auto& e : settings_.extraExclusions) ex.Push(e);
    es.Set("extraExclusions", std::move(ex));
    auto builtIn = Json::MakeArray();
    for (const auto& e : process::ExclusionList::Defaults()) builtIn.Push(e);
    es.Set("builtInExclusions", std::move(builtIn));
    es.Set("profilesDir", core::WideToUtf8(profiles_.Directory().wstring()));
    es.Set("logsDir", core::WideToUtf8(core::LogsDir().wstring()));
    s.Set("settings", std::move(es));

    const double fps = game_ ? game_->profile.fpsLimit : settings_.defaultFps;
    const std::string mode = game_ ? game_->profile.pacingMode : settings_.defaultMode;
    s.Set("fpsLimit", fps);
    s.Set("pacingMode", mode);
    s.Set("pacingEnabled", game_ ? game_->profile.framePacing : false);

    auto disp = Json::MakeObject();
    disp.Set("device", monitor_.deviceName);
    disp.Set("width", monitor_.width);
    disp.Set("height", monitor_.height);
    disp.Set("refreshHz", monitor_.refreshHz);
    disp.Set("vrr", monitor_.vrr);
    s.Set("display", std::move(disp));
    auto presets = Json::MakeArray();
    for (double p : display::SmartPresets(monitor_.refreshHz)) presets.Push(p);
    s.Set("presets", std::move(presets));
    s.Set("suggestedFps", display::SuggestedLimit(monitor_.refreshHz));

    // How limiting is done and whether real-game metrics are available.
    const LimitMethod method = game_ ? game_->method : LimitMethod::None;
    const char* methodStr = method == LimitMethod::NvidiaDriver ? "driver"
                            : method == LimitMethod::FramePacer ? "frameflow"
                                                                : "none";
    s.Set("limitMethod", methodStr);
    s.Set("limitMethodPreference", game_ ? game_->profile.limitMethod : "auto");
    auto nv = Json::MakeObject();
    nv.Set("available", nvapi_.Available());
    nv.Set("gpu", nvapi_.Info().gpuName);
    nv.Set("driverVersion", nvapi_.Info().driverVersion);
    s.Set("nvidia", std::move(nv));
    const char* measurement = presentMon_.Running() ? "live"
                              : (measurement_ == etw::StartResult::NeedElevation ? "needsAdmin" : "off");
    s.Set("measurement", measurement);

    const bool driver = method == LimitMethod::NvidiaDriver;
    const Integration integ = IntegrationLocked();
    auto ij = Json::MakeObject();
    ij.Set("status", ToString(integ));
    if (game_ && game_->antiCheat) {
        ij.Set("antiCheat", game_->antiCheat->product);
        ij.Set("riskAccepted", game_->profile.allowProtected);
    }
    ij.Set("hookBlocked", false); // ETS2 uses the opt-in Present detour instead of the vtable hook.
    switch (integ) {
    case Integration::None:
        ij.Set("title", "No game detected");
        ij.Set("message", "Start a game, or choose one manually.");
        break;
    case Integration::Active:
        ij.Set("title", driver ? "Driver frame limit active" : "Frame pacing active");
        ij.Set("message", driver ? "Limited through the NVIDIA driver. Restart the game if it was already running when the limit changed."
                                 : "");
        break;
    case Integration::Standby:
        ij.Set("title", driver ? "Driver frame limit off" : "Frame pacing off");
        ij.Set("message", driver ? "FrameFlow is not limiting this game." : "FrameFlow is connected but not limiting this game.");
        break;
    case Integration::NotAvailable:
        ij.Set("title", "Limiting not available");
        ij.Set("message", game_ && game_->attachAttempted
                              ? "Attaching on request. If this persists, the game may reject hooks or use an unsupported presentation path."
                              : game_ && game_->profile.limitMethod == "frameflow"
                              ? (game_->profile.framePacing ? "Waiting for the DX11/DX12 hook to attach." : "Select Frame Pacing to enable the DX11/DX12 hook for this run.")
                              : game_ && game_->profile.limitMethod == "driver"
                              ? "NVIDIA driver limiting is unavailable for this game."
                              : "Game detected. Select a limiter and enable Frame Pacing to start.");
        break;
    case Integration::Protected:
        ij.Set("title", "Protected game detected");
        ij.Set("message", "FrameFlow has disabled integration for this process.");
        break;
    case Integration::AccessDenied:
        ij.Set("title", "Game is running as administrator");
        ij.Set("message", "Restart FrameFlow as administrator to manage this game.");
        break;
    }
    s.Set("integration", std::move(ij));

    if (game_) {
        auto g = Json::MakeObject();
        g.Set("pid", game_->pid);
        g.Set("name", game_->profile.game);
        g.Set("exe", game_->exeName);
        g.Set("exePath", game_->exePath);
        g.Set("api", game_->api);
        g.Set("profileId", game_->profile.profileId);
        const bool manual = std::find(settings_.manualProfileIds.begin(), settings_.manualProfileIds.end(),
                                      game_->profile.profileId) != settings_.manualProfileIds.end();
        g.Set("manual", manual);
        auto reasons = Json::MakeArray();
        for (const auto& r : game_->reasons) reasons.Push(r);
        g.Set("reasons", std::move(reasons));
        s.Set("game", std::move(g));
    } else {
        s.Set("game", Json());
    }

    auto cands = Json::MakeArray();
    for (const auto& c : lastCandidates_) {
        if (c.score.excluded || c.score.score <= 0) continue;
        auto cj = Json::MakeObject();
        cj.Set("pid", c.signals.pid);
        cj.Set("exe", c.signals.exeName);
        cj.Set("title", c.signals.windowTitle);
        cj.Set("exePath", c.signals.exePath);
        cj.Set("score", c.score.score);
        cands.Push(std::move(cj));
    }
    s.Set("candidates", std::move(cands));
    return s;
}

void EngineApp::SendStatus(bool force)
{
    Json status;
    {
        std::scoped_lock lock(mutex_);
        status = BuildStatusLocked();
        std::string dump = status.Dump();
        if (!force && dump == lastStatusDump_) return;
        lastStatusDump_ = std::move(dump);
    }
    pipe_.Send(status);
}

void EngineApp::Reply(const Json& msg, bool ok, std::string_view error)
{
    if (const Json* id = msg.Find("id"); id && id->IsNumber()) pipe_.Send(proto::MakeAck(id->AsInt(), ok, error));
}

void EngineApp::OnConnection(bool connected)
{
    if (connected) {
        std::scoped_lock lock(mutex_);
        lastStatusDump_.clear();
    }
}

// ---------------------------------------------------------------------------------------------
// Requests from the UI. Every field is validated; nothing here trusts the client.

void EngineApp::OnMessage(const Json& msg)
{
    const std::string& t = msg["type"].AsString();
    FF_LOG_DEBUG("IPC <- {}", t);

    if (t == proto::type::Hello || t == proto::type::GetState) {
        Reply(msg, true);
        SendStatus(true);
        return;
    }

    if (t == proto::type::EnablePacing || t == proto::type::DisablePacing) {
        const bool enable = (t == proto::type::EnablePacing);
        bool ok = false;
        {
            std::scoped_lock lock(mutex_);
            if (game_) {
                game_->profile.framePacing = enable;
                // Activation is per run. Keep the FPS and method preference, not auto-start.
                game_->profile.startWithGame = false;
                SaveProfileLocked();
                ApplyProfile();
                if (enable) TryAutoAttach();
                ok = true;
                FF_LOG_INFO("Frame pacing {} for {}", enable ? "enabled" : "disabled", game_->exeName);
            }
        }
        Reply(msg, ok, ok ? "" : "No game detected");
        SendStatus(true);
        return;
    }

    if (t == proto::type::SetFpsLimit) {
        const auto fps = proto::ValidateFps(msg);
        if (!fps) {
            Reply(msg, false, "FPS must be between 10 and 1000");
            return;
        }
        {
            std::scoped_lock lock(mutex_);
            if (game_) {
                game_->profile.fpsLimit = *fps;
                SaveProfileLocked();
                ApplyProfile();
            } else {
                settings_.defaultFps = *fps;
                settings_.Save(EngineSettingsPath());
            }
            FF_LOG_INFO("FPS limit set to {:.3f}", *fps);
        }
        Reply(msg, true);
        SendStatus(true);
        return;
    }

    if (t == proto::type::SetPacingMode) {
        timing::PacingMode mode;
        const std::string requested = msg["mode"].AsString("");
        if (!timing::ParsePacingMode(requested, mode)) {
            Reply(msg, false, "Unknown pacing mode");
            return;
        }
        {
            std::scoped_lock lock(mutex_);
            if (game_) {
                game_->profile.pacingMode = requested;
                SaveProfileLocked();
                ApplyProfile();
            } else {
                settings_.defaultMode = requested;
                settings_.Save(EngineSettingsPath());
            }
        }
        Reply(msg, true);
        SendStatus(true);
        return;
    }

    if (t == proto::type::SetLimitMethod) {
        const std::string requested = msg["method"].AsString("");
        if (requested != "auto" && requested != "frameflow" && requested != "driver") {
            Reply(msg, false, "Unknown limit method");
            return;
        }
        bool ok = false;
        {
            std::scoped_lock lock(mutex_);
            if (game_) {
                game_->profile.limitMethod = requested;
                SaveProfileLocked();
                ChooseLimitMethod();
                ok = true;
            }
        }
        Reply(msg, ok, ok ? "" : "No game detected");
        SendStatus(true);
        return;
    }

    if (t == proto::type::SetProtectedRisk) {
        const auto* choice = msg.Find("allow");
        bool ok = false;
        if (choice && choice->IsBool()) {
            std::scoped_lock lock(mutex_);
            if (game_ && game_->antiCheat) {
                game_->profile.allowProtected = choice->AsBool();
                SaveProfileLocked();
                if (!choice->AsBool()) {
                    ChooseLimitMethod();
                } else {
                    game_->attachAttempted = false;
                    TryAttachSession();
                    ChooseLimitMethod();
                    TryAutoAttach();
                }
                ok = true;
            }
        }
        Reply(msg, ok, ok ? "" : "No protected game is selected");
        SendStatus(true);
        return;
    }

    if (t == proto::type::SelectGame) {
        const auto path = proto::ValidateExePath(msg);
        if (!path) {
            Reply(msg, false, "Not a valid executable path");
            return;
        }
        {
            std::scoped_lock lock(mutex_);
            const std::string pathUtf8 = core::WideToUtf8(*path);
            const std::string exe = core::WideToUtf8(std::filesystem::path(*path).filename().wstring());
            auto profile = profiles_.LoadOrCreate(exe, pathUtf8, settings_.defaultFps, settings_.defaultMode);
            profile.manual = true;
            profiles_.Save(profile);
            if (std::find(settings_.manualProfileIds.begin(), settings_.manualProfileIds.end(), profile.profileId) ==
                settings_.manualProfileIds.end()) {
                settings_.manualProfileIds.push_back(profile.profileId);
                settings_.Save(EngineSettingsPath());
            }
            // If it is running right now, switch immediately; otherwise it activates on launch.
            if (!game_ || game_->profile.profileId != profile.profileId) {
                for (const auto& c : lastCandidates_) {
                    if (profiles::MakeProfileId(c.signals.exeName, c.signals.exePath) == profile.profileId) {
                        CloseCurrentGame("manual selection");
                        SwitchToGame(c);
                        break;
                    }
                }
            }
            FF_LOG_INFO("Manual game selected: {}", pathUtf8);
        }
        Reply(msg, true);
        SendStatus(true);
        return;
    }

    if (t == proto::type::ClearManualGame) {
        {
            std::scoped_lock lock(mutex_);
            const std::string id = msg["profileId"].AsString(game_ ? game_->profile.profileId : "");
            auto& v = settings_.manualProfileIds;
            v.erase(std::remove(v.begin(), v.end(), id), v.end());
            settings_.Save(EngineSettingsPath());
        }
        Reply(msg, true);
        SendStatus(true);
        return;
    }

    if (t == proto::type::SetEngineSettings) {
        {
            std::scoped_lock lock(mutex_);
            if (const auto* v = msg.Find("autoDetect"); v && v->IsBool()) settings_.autoDetect = v->AsBool();
            if (const auto* v = msg.Find("miniOverlay"); v && v->IsBool()) {
                settings_.miniOverlay = v->AsBool();
                if (!settings_.miniOverlay) miniOverlay_.Clear();
            }
            if (const auto* v = msg.Find("telemetrySeconds"); v && v->IsNumber())
                settings_.telemetrySeconds = std::clamp(v->AsNumber(), 10.0, 60.0);
            if (const auto* v = msg.Find("defaultFps"); v && v->IsNumber()) {
                auto tmp = proto::Make(proto::type::SetFpsLimit);
                tmp.Set("fps", v->AsNumber());
                if (auto fps = proto::ValidateFps(tmp)) settings_.defaultFps = *fps;
            }
            if (const auto* v = msg.Find("defaultMode"); v && v->IsString()) {
                timing::PacingMode m;
                if (timing::ParsePacingMode(v->AsString(), m)) settings_.defaultMode = v->AsString();
            }
            if (const auto* v = msg.Find("extraExclusions"); v && v->IsArray() && v->Items().size() <= 256) {
                settings_.extraExclusions.clear();
                for (const auto& e : v->Items()) {
                    if (!e.IsString()) continue;
                    const std::string name = core::ToLowerAscii(e.AsString());
                    if (name.size() < 5 || name.size() > 260 || name.find_first_of("\\/:*?\"<>|") != std::string::npos) continue;
                    if (std::find(settings_.extraExclusions.begin(), settings_.extraExclusions.end(), name) ==
                        settings_.extraExclusions.end())
                        settings_.extraExclusions.push_back(name);
                }
                // A newly excluded current game is released immediately.
                if (game_ && std::find(settings_.extraExclusions.begin(), settings_.extraExclusions.end(),
                                       core::ToLowerAscii(game_->exeName)) != settings_.extraExclusions.end())
                    CloseCurrentGame("excluded by user");
            }
            settings_.Save(EngineSettingsPath());
        }
        Reply(msg, true);
        SendStatus(true);
        return;
    }

    if (t == proto::type::SetLogLevel) {
        core::LogLevel lvl;
        if (!core::ParseLogLevel(msg["level"].AsString(""), lvl)) {
            Reply(msg, false, "Unknown log level");
            return;
        }
        core::Log::SetLevel(lvl);
        {
            std::scoped_lock lock(mutex_);
            settings_.logLevel = std::string(core::ToString(lvl));
            settings_.Save(EngineSettingsPath());
        }
        Reply(msg, true);
        return;
    }

    if (t == proto::type::Shutdown) {
        Reply(msg, true);
        RequestShutdown();
        return;
    }

    FF_LOG_WARN("Unknown IPC message type '{}'", t.substr(0, 64));
    Reply(msg, false, "Unknown message type");
}

} // namespace ff::engine
