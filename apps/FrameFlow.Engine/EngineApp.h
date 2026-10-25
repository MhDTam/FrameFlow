#pragma once
#include "EngineSettings.h"

#include "Core/Json.h"
#include "Display/DisplayInfo.h"
#include "Etw/PresentMonitor.h"
#include "IPC/PipeServer.h"
#include "IPC/SharedSession.h"
#include "ProcessDetection/AntiCheatDetector.h"
#include "ProcessDetection/ProcessScanner.h"
#include "Profiles/ProfileStore.h"
#include "MiniOverlay.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace ff::engine {

// How FrameFlow is limiting the current game.
enum class Integration {
    None,          // no game
    Active,        // limiting now through the in-process pacer
    Standby,       // connected/able to limit, but the user switched pacing off
    NotAvailable,  // no FrameFlow session can limit this game
    Protected,     // anti-cheat detected: integration disabled on purpose
    AccessDenied,  // game is elevated, FrameFlow is not
};

// Which mechanism applies the limit.
enum class LimitMethod {
    None,
    FramePacer,    // in-process deadline pacer (FrameFlow-aware renderer / future hook)
};

struct CurrentGame {
    std::uint32_t pid = 0;
    std::string exeName;
    std::string exePath;
    std::string api;
    std::vector<std::string> reasons;
    profiles::GameProfile profile;
    std::optional<process::AntiCheatHit> antiCheat;
    bool accessible = true;
    void* hwnd = nullptr;

    LimitMethod method = LimitMethod::None;
    std::wstring exePathW;          // used for launch and nearby-file detection

    std::unique_ptr<ipc::SharedSession> session;
    ipc::RingReader reader;
    std::deque<double> frametimes;  // ms, oldest first, bounded by telemetrySeconds
    double frametimeTotalMs = 0.0;
    std::uint64_t configEpoch = 0;  // client's configStartIndex we last saw
    bool attachAttempted = false;
};

class EngineApp {
public:
    EngineApp();
    ~EngineApp();

    int Run();              // blocks until Shutdown is requested
    void RequestShutdown();

private:
    // threads
    void DetectionLoop();
    void TelemetryLoop();

    // detection
    void DetectOnce();
    void SwitchToGame(const process::Candidate& c);
    void CloseCurrentGame(const char* reason);
    void TryAttachSession();
    void TryAutoAttach();
    void ChooseLimitMethod();       // connects the selected FrameFlow pacer when available
    void ApplyProfile();            // pushes the profile to whichever method is active

    // IPC
    void OnMessage(const core::Json& msg);
    void OnConnection(bool connected);
    void SendStatus(bool force);
    void Reply(const core::Json& msg, bool ok, std::string_view error = {});

    core::Json BuildStatusLocked() const;
    Integration IntegrationLocked() const;
    void SaveProfileLocked();
    process::ExclusionList BuildExclusions() const;

    EngineSettings settings_;
    profiles::ProfileStore profiles_;
    ipc::PipeServer pipe_;
    process::ProcessScanner scanner_;
    etw::PresentMonitor presentMon_;
    MiniOverlay miniOverlay_;
    etw::StartResult measurement_ = etw::StartResult::Failed; // ETW session state

    mutable std::mutex mutex_;                 // guards everything below
    std::unique_ptr<CurrentGame> game_;
    std::vector<process::Candidate> lastCandidates_;
    display::MonitorInfo monitor_;
    std::string lastStatusDump_;

    std::atomic<bool> stop_{false};
    std::mutex waitMutex_;
    std::condition_variable wake_;
    std::thread detection_;
    std::thread telemetry_;
};

} // namespace ff::engine
