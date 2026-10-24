#pragma once
#include <cstdint>

namespace ff::timing {

// Deliberately naive limiter: Sleep() for the remainder of each frame, relative to the previous
// release. Exists ONLY as a baseline in FrameFlow.Benchmark so the report shows what the
// deadline-based pacer buys. Never use this for real pacing.
class NaiveSleepLimiter {
public:
    explicit NaiveSleepLimiter(double fps) : fps_(fps) {}
    std::int64_t WaitForNextFrame(); // returns release time in QPC ticks

private:
    double fps_;
    std::int64_t last_ = 0;
};

} // namespace ff::timing
