#pragma once
#include "IPC/SharedSession.h"
#include "Timing/FramePacer.h"

#include <cstdint>
#include <atomic>
#include <memory>

namespace ff::graphics {

// The in-process half of FrameFlow: runs inside whatever presents frames (the test renderer now,
// the DXGI hook later). It owns the shared session, applies the engine's settings to a FramePacer
// right before Present, and publishes present timestamps for telemetry.
//
// Engine control has priority over local settings while the engine heartbeat is fresh. If the
// engine disappears (crash, exit), pacing falls back to local settings within ~2 s — for a hook
// the local setting is "off", so a dead engine can never leave a game limited.
class PresentPacer {
public:
    explicit PresentPacer(const char* graphicsApi);

    void SetLocal(bool enabled, double fps, timing::PacingMode mode);

    void BeforePresent(std::int64_t hookEntryQpc = 0);  // may block until the frame deadline
    void AfterPresent();

    bool EngineControlled() const { return engineControlled_; }
    bool PacingActive() const { return active_; }
    double AppliedFps() const { return pacer_.TargetFps(); }
    timing::PacingMode AppliedMode() const { return pacer_.Mode(); }
    bool HasSession() const { return session_ != nullptr; }

private:
    void SyncConfig();

    std::unique_ptr<ipc::SharedSession> session_;
    timing::FramePacer pacer_;
    std::uint32_t lastGeneration_ = ~0u;
    bool engineControlled_ = false;
    bool active_ = false;

    bool localEnabled_ = false;
    double localFps_ = 60.0;
    timing::PacingMode localMode_ = timing::PacingMode::Balanced;
    std::int64_t lastSyncQpc_ = 0;
    std::atomic<std::int64_t> lastAfterPresentQpc_{0};
};

} // namespace ff::graphics
