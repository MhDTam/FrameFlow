#include "TestHarness.h"

#include "Telemetry/FrameStatistics.h"

#include <vector>

using namespace ff::telemetry;

TEST(Stats_ConstantFrametimes)
{
    std::vector<double> ft(600, 16.6667);
    const auto s = ComputeStats(ft);
    CHECK(s.frames == 600);
    CHECK_NEAR(s.averageFps, 60.0, 0.01);
    CHECK_NEAR(s.avgFrametimeMs, 16.6667, 1e-9);
    CHECK_NEAR(s.stddevMs, 0.0, 1e-9);
    CHECK_NEAR(s.low1Fps, 60.0, 0.01);
    CHECK_NEAR(s.currentFps, 60.0, 0.01);
}

TEST(Stats_AverageFpsIsTimeWeighted)
{
    // One 10 ms frame and one 30 ms frame: 2 frames in 40 ms = 50 FPS (not mean(100, 33.3) = 66.7).
    std::vector<double> ft = {10.0, 30.0};
    CHECK_NEAR(ComputeStats(ft).averageFps, 50.0, 1e-9);
}

TEST(Stats_OnePercentLow)
{
    // 990 frames at 10 ms and 10 frames at 50 ms: slowest 1% (10 frames) average 50 ms -> 20 FPS.
    std::vector<double> ft(990, 10.0);
    ft.insert(ft.end(), 10, 50.0);
    const auto s = ComputeStats(ft);
    CHECK_NEAR(s.low1Fps, 20.0, 1e-9);
    CHECK_NEAR(s.low01Fps, 20.0, 1e-9);   // slowest 0.1% = 1 frame of 50 ms
    CHECK_NEAR(s.maxFrametimeMs, 50.0, 1e-12);
    CHECK_NEAR(s.minFrametimeMs, 10.0, 1e-12);
}

TEST(Stats_CurrentFpsUsesRecentWindow)
{
    std::vector<double> ft(100, 20.0);            // 2 s at 50 FPS
    ft.insert(ft.end(), 60, 10.0);                // then 0.6 s at 100 FPS
    CHECK_NEAR(ComputeStats(ft, 500.0).currentFps, 100.0, 1e-9);
}

TEST(Stats_Empty)
{
    const auto s = ComputeStats({});
    CHECK(s.frames == 0);
    CHECK(s.averageFps == 0.0);
}

TEST(Percentile_NearestRank)
{
    std::vector<double> v = {5, 1, 4, 2, 3};
    CHECK(Percentile(v, 0) == 1);
    CHECK(Percentile(v, 50) == 3);
    CHECK(Percentile(v, 100) == 5);
}

TEST(Graph_BucketsCoverHistory)
{
    std::vector<double> ft(1800, 16.6667);        // 30 s at 60 FPS
    const auto g = BuildGraph(ft, 30.0, 300);
    CHECK(g.meanMs.size() == 300);
    for (double v : g.meanMs) CHECK_NEAR(v, 16.6667, 1e-6);
}

TEST(Graph_SpikeShowsInMax)
{
    std::vector<double> ft(600, 10.0);
    ft.push_back(80.0);
    ft.insert(ft.end(), 100, 10.0);
    const auto g = BuildGraph(ft, 7.0, 70);
    double mx = 0;
    for (double v : g.maxMs) mx = std::max(mx, v);
    CHECK_NEAR(mx, 80.0, 1e-9);
}

TEST_MAIN()
