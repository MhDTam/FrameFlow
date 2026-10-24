#include "Graphics/PresentPacer.h"
#include "Timing/Clock.h"

namespace ff::graphics {

namespace {
constexpr double kEngineStaleSeconds = 2.0;
constexpr double kSyncIntervalMs = 50.0; // config poll rate; atomics are cheap but no need per frame
} // namespace

PresentPacer::PresentPacer(const char* graphicsApi)
    : session_(ipc::SharedSession::CreateForCurrentProcess(graphicsApi))
{
}

void PresentPacer::SetLocal(bool enabled, double fps, timing::PacingMode mode)
{
    localEnabled_ = enabled;
    localFps_ = timing::ClampFps(fps);
    localMode_ = mode;
    lastSyncQpc_ = 0; // force re-sync on next frame
}

void PresentPacer::SyncConfig()
{
    const std::int64_t now = timing::Clock::Now();
    if (lastSyncQpc_ != 0 && timing::Clock::TicksToMs(now - lastSyncQpc_) < kSyncIntervalMs) return;
    lastSyncQpc_ = now;

    bool enabled = localEnabled_;
    double fps = localFps_;
    auto mode = localMode_;
    engineControlled_ = false;

    if (session_) {
        auto& c = session_->Block().control;
        const bool attached = c.engineAttached.load(std::memory_order_acquire) != 0;
        const std::int64_t hb = c.engineHeartbeatQpc.load(std::memory_order_relaxed);
        const bool fresh = hb != 0 && timing::Clock::TicksToMs(now - hb) < kEngineStaleSeconds * 1000.0;
        if (attached && fresh) {
            engineControlled_ = true;
            enabled = c.pacingEnabled.load(std::memory_order_relaxed) != 0;
            fps = static_cast<double>(c.targetFpsMilli.load(std::memory_order_relaxed)) / 1000.0;
            const int m = c.mode.load(std::memory_order_relaxed);
            mode = (m >= 0 && m <= 2) ? static_cast<timing::PacingMode>(m) : timing::PacingMode::Balanced;
        }
    }

    const bool changed = enabled != active_ || timing::ClampFps(fps) != pacer_.TargetFps() || mode != pacer_.Mode();
    if (enabled && !active_) pacer_.Reset(); // start a clean timeline, no catch-up from stale state
    active_ = enabled;
    pacer_.SetTargetFps(fps);
    pacer_.SetMode(mode);
    if (changed && session_) {
        // Next pushed frame straddles the change; the one after it is the first clean interval.
        const auto next = session_->Block().ring.writeIndex.load(std::memory_order_relaxed);
        session_->Block().client.configStartIndex.store(next + 1, std::memory_order_release);
    }

    if (session_) {
        auto& cl = session_->Block().client;
        cl.pacingActive.store(active_ ? 1 : 0, std::memory_order_relaxed);
        cl.appliedFpsMilli.store(static_cast<std::int64_t>(pacer_.TargetFps() * 1000.0 + 0.5), std::memory_order_relaxed);
        cl.heartbeatQpc.store(now, std::memory_order_relaxed);
    }
}

void PresentPacer::BeforePresent(std::int64_t hookEntryQpc)
{
    const auto preWait = timing::Clock::Now();
    if (hookEntryQpc == 0) hookEntryQpc = preWait;
    SyncConfig();
    if (active_) pacer_.WaitForNextFrame();
    if (session_) session_->PushPresent(timing::Clock::Now(), preWait,
                                         lastAfterPresentQpc_.load(std::memory_order_acquire),
                                         hookEntryQpc);
}

void PresentPacer::AfterPresent()
{
    lastAfterPresentQpc_.store(timing::Clock::Now(), std::memory_order_release);
}

} // namespace ff::graphics
