#include "TestHarness.h"

#include "Telemetry/FrameStatistics.h"
#include "Timing/Clock.h"
#include "Timing/FramePacer.h"

#include <vector>

using namespace ff::timing;

TEST(DeadlineTimeline_NoDriftOverTenMillionFrames)
{
    // 60 FPS at a 10 MHz clock: 166666.666... ticks per frame. Additive stepping would drift by
    // ~0.33 ticks per frame; the absolute timeline must stay within half a tick forever.
    DeadlineTimeline t;
    const double period = 10'000'000.0 / 60.0;
    t.Start(1000, period);
    const std::int64_t n = 10'000'000;
    const std::int64_t expected = 1000 + static_cast<std::int64_t>(std::llround(static_cast<double>(n) * period));
    CHECK(t.DeadlineAt(n) == expected);
    CHECK(std::llabs(t.DeadlineAt(n) - 1000 - n * 10'000'000 / 60) <= 1);
}

TEST(DeadlineTimeline_SmallLatenessStaysOnGrid)
{
    DeadlineTimeline t;
    t.Start(0, 1000.0);
    bool rebased = false;
    CHECK(t.Next(500, rebased) == 1000);
    CHECK(!rebased);
    // Arrive 300 ticks late for frame 2: still frame 2's deadline, grid unchanged.
    CHECK(t.Next(2300, rebased) == 2000);
    CHECK(!rebased);
    CHECK(t.Next(2400, rebased) == 3000);
}

TEST(DeadlineTimeline_LargeStallRebasesInsteadOfBursting)
{
    DeadlineTimeline t;
    t.Start(0, 1000.0);
    bool rebased = false;
    t.Next(900, rebased);                 // frame 1
    const auto d = t.Next(50'000, rebased); // stalled ~48 frames
    CHECK(rebased);
    CHECK(d == 50'000);
    CHECK(t.Next(50'100, rebased) == 51'000); // new grid anchored at the stall end
    CHECK(!rebased);
}

TEST(ClampFps_RangeAndNonFinite)
{
    CHECK(ClampFps(5.0) == kMinFps);
    CHECK(ClampFps(5000.0) == kMaxFps);
    CHECK(ClampFps(59.94) == 59.94);
    CHECK(ClampFps(std::nan("")) == 60.0);
}

TEST(PacingMode_ParseRoundTrip)
{
    for (auto m : {PacingMode::Efficiency, PacingMode::Balanced, PacingMode::Precision}) {
        PacingMode parsed;
        CHECK(ParsePacingMode(ToString(m), parsed));
        CHECK(parsed == m);
    }
    PacingMode x;
    CHECK(!ParsePacingMode("Turbo", x));
}

TEST(PacingThresholds_OrderedByMode)
{
    const auto e = PacingThresholds::For(PacingMode::Efficiency);
    const auto b = PacingThresholds::For(PacingMode::Balanced);
    const auto p = PacingThresholds::For(PacingMode::Precision);
    CHECK(e.timerMarginUs < b.timerMarginUs && b.timerMarginUs < p.timerMarginUs);
    CHECK(e.spinUs < b.spinUs && b.spinUs < p.spinUs);
}

// Real-time smoke test (2 s). Tolerances are loose on purpose: this proves the pacer works on the
// build machine; FrameFlow.Benchmark is the precision measurement.
TEST(FramePacer_RealTime_100Fps)
{
    FramePacer pacer(100.0, PacingMode::Balanced);
    std::vector<std::int64_t> releases;
    for (int i = 0; i < 20; ++i) pacer.WaitForNextFrame(); // warm-up
    for (int i = 0; i < 200; ++i) releases.push_back(pacer.WaitForNextFrame().releaseTicks);
    std::vector<double> intervals;
    for (size_t i = 1; i < releases.size(); ++i) intervals.push_back(Clock::TicksToMs(releases[i] - releases[i - 1]));
    const auto s = ff::telemetry::ComputeStats(intervals);
    CHECK_NEAR(s.avgFrametimeMs, 10.0, 0.05);   // absolute timeline => mean is exact
    CHECK(ff::telemetry::Percentile(intervals, 50) > 9.8 && ff::telemetry::Percentile(intervals, 50) < 10.2);
}

TEST(FramePacer_FractionalTarget)
{
    FramePacer pacer(59.94);
    CHECK_NEAR(pacer.FramePeriodMs(), 1000.0 / 59.94, 1e-9);
    pacer.SetTargetFps(117.5);
    CHECK_NEAR(pacer.TargetFps(), 117.5, 1e-12);
}

TEST_MAIN()
