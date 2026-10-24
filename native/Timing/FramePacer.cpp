#include "Timing/FramePacer.h"
#include "Timing/Clock.h"

#include <windows.h>
#include <immintrin.h>

#include <algorithm>
#include <cmath>

namespace ff::timing {

std::string_view ToString(PacingMode mode)
{
    switch (mode) {
    case PacingMode::Efficiency: return "Efficiency";
    case PacingMode::Balanced: return "Balanced";
    case PacingMode::Precision: return "Precision";
    }
    return "Balanced";
}

bool ParsePacingMode(std::string_view text, PacingMode& out)
{
    if (text == "Efficiency") { out = PacingMode::Efficiency; return true; }
    if (text == "Balanced") { out = PacingMode::Balanced; return true; }
    if (text == "Precision") { out = PacingMode::Precision; return true; }
    return false;
}

double ClampFps(double fps)
{
    if (!std::isfinite(fps)) return 60.0;
    return std::clamp(fps, kMinFps, kMaxFps);
}

// Mode defaults. The timer margin is the part of every frame we spend awake-but-waiting, so it is
// the main CPU cost knob; the spin window is the part we refuse to give back to the scheduler at all.
// These are starting points — the adaptive margin grows them when the OS timer proves noisier.
// Values chosen from FrameFlow.Benchmark runs; re-run it after changing them (docs/benchmark-report.md).
PacingThresholds PacingThresholds::For(PacingMode mode)
{
    switch (mode) {
    case PacingMode::Efficiency: return {400.0, 40.0, 1000.0};
    case PacingMode::Precision: return {1200.0, 250.0, 3000.0};
    case PacingMode::Balanced:
    default: return {700.0, 100.0, 2000.0};
    }
}

// ---------------------------------------------------------------------------------------------
// DeadlineTimeline

void DeadlineTimeline::Start(std::int64_t baseTicks, double periodTicks)
{
    started_ = true;
    base_ = baseTicks;
    index_ = 0;
    period_ = periodTicks;
}

std::int64_t DeadlineTimeline::DeadlineAt(std::int64_t index) const
{
    // Multiplying (instead of repeatedly adding) keeps rounding error bounded to half a tick
    // no matter how many frames have elapsed. A double holds index*period exactly enough for
    // ~10^9 frames at 10 MHz QPC.
    return base_ + static_cast<std::int64_t>(std::llround(static_cast<double>(index) * period_));
}

std::int64_t DeadlineTimeline::Next(std::int64_t nowTicks, bool& rebased, double rebaseAfterFrames)
{
    rebased = false;
    ++index_;
    const std::int64_t deadline = DeadlineAt(index_);
    const double lateTicks = static_cast<double>(nowTicks - deadline);
    if (lateTicks > rebaseAfterFrames * period_) {
        // The application stalled (loading screen, alt-tab, hitch). Catching up would release a
        // burst of frames back to back, which is exactly the stutter we exist to prevent.
        base_ = nowTicks;
        index_ = 0;
        rebased = true;
        return nowTicks;
    }
    return deadline;
}

// ---------------------------------------------------------------------------------------------
// FramePacer

FramePacer::FramePacer(double targetFps, PacingMode mode)
    : targetFps_(ClampFps(targetFps)), mode_(mode), thresholds_(PacingThresholds::For(mode))
{
    adaptiveMarginUs_ = thresholds_.timerMarginUs;
}

void FramePacer::SetTargetFps(double fps)
{
    const double clamped = ClampFps(fps);
    if (clamped == targetFps_) return;
    targetFps_ = clamped;
    Reset();
}

void FramePacer::SetMode(PacingMode mode)
{
    if (mode == mode_) return;
    mode_ = mode;
    thresholds_ = PacingThresholds::For(mode);
    adaptiveMarginUs_ = std::max(adaptiveMarginUs_, thresholds_.timerMarginUs);
}

void FramePacer::SetThresholds(const PacingThresholds& t)
{
    thresholds_ = t;
    adaptiveMarginUs_ = t.timerMarginUs;
}

void FramePacer::Reset() { timeline_ = DeadlineTimeline{}; }

FrameWaitResult FramePacer::WaitForNextFrame()
{
    FrameWaitResult r;
    const std::int64_t now = Clock::Now();
    const double periodTicks = static_cast<double>(Clock::Frequency()) / targetFps_;

    if (!timeline_.Started()) {
        // First frame on a new timeline is released immediately and becomes the anchor.
        timeline_.Start(now, periodTicks);
        r.deadlineTicks = now;
        r.releaseTicks = now;
        r.frameIndex = 0;
        return r;
    }

    bool rebased = false;
    const std::int64_t deadline = timeline_.Next(now, rebased);
    r.deadlineTicks = deadline;
    r.frameIndex = timeline_.Index();
    r.rebased = rebased;

    if (now >= deadline) {
        // Late but within one period: release now and stay on the grid so the average stays exact.
        r.late = now > deadline;
        r.releaseTicks = now;
        return r;
    }

    WaitUntil(deadline);
    r.releaseTicks = Clock::Now();
    return r;
}

// Three-phase wait:
//   1. Coarse: sleep on the high-resolution waitable timer until (deadline - margin). Costs no CPU.
//   2. Yield:  SwitchToThread() until (deadline - spinUs). Gives the core to any ready thread,
//              otherwise returns immediately; this absorbs timer wake-up jitter cheaply.
//   3. Spin:   _mm_pause() on QPC until the deadline. Sub-microsecond release accuracy.
// The margin adapts to the measured timer lateness so a noisy system does not cause misses.
void FramePacer::WaitUntil(std::int64_t deadline)
{
    const double freq = static_cast<double>(Clock::Frequency());
    auto usUntil = [&](std::int64_t t) { return static_cast<double>(deadline - t) * 1e6 / freq; };

    const std::int64_t start = Clock::Now();
    const double remainingUs = usUntil(start);
    const double margin = adaptiveMarginUs_;

    // Timer granularity is ~500 us even in high-res mode; asking for less than that just returns late.
    constexpr double kMinUsefulTimerUs = 250.0;
    if (remainingUs - margin > kMinUsefulTimerUs && timer_.Valid()) {
        const double sleepUs = remainingUs - margin;
        const std::int64_t intendedWake = start + static_cast<std::int64_t>(sleepUs * freq / 1e6);
        if (timer_.Wait(static_cast<std::int64_t>(sleepUs))) {
            const double latenessUs = static_cast<double>(Clock::Now() - intendedWake) * 1e6 / freq;
            UpdateTimerModel(latenessUs);
        }
    }

    const std::int64_t awake = Clock::Now();
    const double spinUs = thresholds_.spinUs;
    while (usUntil(Clock::Now()) > spinUs) {
        ::SwitchToThread();
    }
    std::int64_t t = Clock::Now();
    while (t < deadline) {
        _mm_pause();
        t = Clock::Now();
    }
    activeWaitTicks_ += t - awake;
}

void FramePacer::UpdateTimerModel(double latenessUs)
{
    // EWMA with alpha = 1/16: reacts within ~a quarter second at 60 FPS, ignores single outliers.
    constexpr double kAlpha = 1.0 / 16.0;
    const double late = std::max(0.0, latenessUs);
    timerLateMeanUs_ += kAlpha * (late - timerLateMeanUs_);
    timerLateDevUs_ += kAlpha * (std::fabs(late - timerLateMeanUs_) - timerLateDevUs_);

    // Wake early enough to cover mean + 4 deviations of observed lateness (roughly a p99 bound
    // for the long-tailed distribution Windows timers show), plus the configured base margin.
    const double wanted = thresholds_.timerMarginUs * 0.5 + timerLateMeanUs_ + 4.0 * timerLateDevUs_;
    adaptiveMarginUs_ = std::clamp(wanted, thresholds_.timerMarginUs, thresholds_.maxAdaptiveUs);
}

} // namespace ff::timing
