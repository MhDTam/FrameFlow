#pragma once
#include <cstddef>
#include <span>
#include <vector>

namespace ff::telemetry {

// Statistics over a window of consecutive frame times (milliseconds, oldest first).
//
// Definitions (all computed from real per-frame intervals, never from a 1-second counter):
//   averageFps      = N / sum(ft_i)                      — time-weighted, not mean(1000/ft_i)
//   currentFps      = same formula over the most recent `currentWindowMs` of frames
//   avgFrametimeMs  = sum(ft_i) / N
//   stddevMs        = sqrt( sum((ft_i - avg)^2) / N )
//   low1Fps         = 1000 / mean(slowest 1% of frame times)     ("1% low average")
//   low01Fps        = 1000 / mean(slowest 0.1% of frame times)
//   The slowest-k set always contains at least one frame, so short windows degrade gracefully.
struct FrameStats {
    std::size_t frames = 0;
    double windowSeconds = 0.0;
    double currentFps = 0.0;
    double averageFps = 0.0;
    double lastFrametimeMs = 0.0;
    double avgFrametimeMs = 0.0;
    double minFrametimeMs = 0.0;
    double maxFrametimeMs = 0.0;
    double stddevMs = 0.0;
    double low1Fps = 0.0;
    double low01Fps = 0.0;
};

FrameStats ComputeStats(std::span<const double> frametimesMs, double currentWindowMs = 500.0);

// Downsamples the frame times of the last `historySeconds` into `buckets` equal time slices for
// the UI graph. Each bucket reports the mean and the max frame time inside it; empty buckets
// (no frame finished in that slice, e.g. a long hitch) carry forward the previous value.
struct GraphSeries {
    std::vector<double> meanMs;
    std::vector<double> maxMs;
};
GraphSeries BuildGraph(std::span<const double> frametimesMs, double historySeconds, std::size_t buckets);

// Nearest-rank percentile (p in [0,100]) of an unsorted sample. Copies internally.
double Percentile(std::span<const double> values, double p);

} // namespace ff::telemetry
