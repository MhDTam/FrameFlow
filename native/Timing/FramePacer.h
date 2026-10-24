#pragma once
#include "Timing/WaitableTimer.h"

#include <cstdint>
#include <string_view>

namespace ff::timing {

enum class PacingMode : std::uint8_t { Efficiency = 0, Balanced = 1, Precision = 2 };

std::string_view ToString(PacingMode mode);
bool ParsePacingMode(std::string_view text, PacingMode& out);

constexpr double kMinFps = 10.0;
constexpr double kMaxFps = 1000.0;
double ClampFps(double fps);

// How a single frame wait is split between the three phases (see FramePacer.cpp).
// All values are microseconds before the deadline.
struct PacingThresholds {
    double timerMarginUs;   // coarse timer wakes this long before the deadline (minimum; adaptive may grow it)
    double spinUs;          // final phase: pure busy-spin on QPC
    double maxAdaptiveUs;   // cap for the adaptive margin, bounds CPU cost on noisy systems

    static PacingThresholds For(PacingMode mode);
};

// Absolute frame timeline: deadline[n] = base + n * period.
// Kept separate from the waiting logic so the drift/rebase rules are unit-testable.
class DeadlineTimeline {
public:
    // periodTicks is fractional on purpose: 60 FPS at 10 MHz QPC is 166666.67 ticks.
    void Start(std::int64_t baseTicks, double periodTicks);
    bool Started() const { return started_; }

    // Returns the deadline the caller should wait for, given the current time.
    // If the caller is more than `rebaseAfterFrames` periods late, the timeline is re-anchored
    // at `now` instead of trying to "catch up" with a burst of zero-length frames.
    std::int64_t Next(std::int64_t nowTicks, bool& rebased, double rebaseAfterFrames = 1.0);

    std::int64_t DeadlineAt(std::int64_t index) const;
    std::int64_t Index() const { return index_; }
    double PeriodTicks() const { return period_; }
    std::int64_t Base() const { return base_; }

private:
    bool started_ = false;
    std::int64_t base_ = 0;
    std::int64_t index_ = 0;
    double period_ = 0.0;
};

struct FrameWaitResult {
    std::int64_t deadlineTicks = 0;   // when this frame was supposed to be released
    std::int64_t releaseTicks = 0;    // when WaitForNextFrame actually returned
    std::int64_t frameIndex = 0;
    bool late = false;                // caller arrived after the deadline (render took too long)
    bool rebased = false;             // timeline was re-anchored because of a large miss
};

// Deadline-based frame limiter.
//
// Call WaitForNextFrame() once per frame right before the frame is presented. It blocks until the
// next point on an absolute timeline, so errors never accumulate: a frame that is released 30 us
// late does not push every later frame back by 30 us.
//
// Not thread-safe: one pacer per presenting thread. Setters may be called from the same thread
// between frames; cross-thread configuration goes through PresentPacer, which uses atomics.
class FramePacer {
public:
    explicit FramePacer(double targetFps = 60.0, PacingMode mode = PacingMode::Balanced);

    void SetTargetFps(double fps);
    void SetMode(PacingMode mode);
    void SetThresholds(const PacingThresholds& t);   // advanced override
    void Reset();                                     // start a fresh timeline on the next wait

    FrameWaitResult WaitForNextFrame();

    double TargetFps() const { return targetFps_; }
    double FramePeriodMs() const { return 1000.0 / targetFps_; }
    PacingMode Mode() const { return mode_; }
    const PacingThresholds& Thresholds() const { return thresholds_; }
    double AdaptiveMarginUs() const { return adaptiveMarginUs_; }
    bool UsingHighResolutionTimer() const { return timer_.IsHighResolution(); }

    // Total QPC ticks spent awake inside WaitForNextFrame (yield + spin phases). Timer sleep is
    // excluded because it costs no CPU. activeWaitTicks / wall time = the pacer's CPU overhead.
    std::int64_t ActiveWaitTicks() const { return activeWaitTicks_; }

private:
    void WaitUntil(std::int64_t deadline);
    void UpdateTimerModel(double latenessUs);

    WaitableTimer timer_;
    DeadlineTimeline timeline_;
    double targetFps_;
    PacingMode mode_;
    PacingThresholds thresholds_;

    // Running estimate of how late the OS timer wakes us (EWMA of mean and absolute deviation).
    double timerLateMeanUs_ = 0.0;
    double timerLateDevUs_ = 0.0;
    double adaptiveMarginUs_ = 0.0;
    std::int64_t activeWaitTicks_ = 0;
};

} // namespace ff::timing
