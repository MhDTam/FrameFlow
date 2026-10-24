#pragma once
#include <cstdint>

namespace ff::timing {

// Thin wrapper over QueryPerformanceCounter. QPC is monotonic, invariant across cores on
// every supported Windows 10+ machine, and has sub-microsecond resolution (typically 10 MHz).
class Clock {
public:
    static std::int64_t Now();          // QPC ticks
    static std::int64_t Frequency();    // ticks per second (cached, constant for the boot)

    static double TicksToMs(std::int64_t ticks) { return static_cast<double>(ticks) * 1000.0 / static_cast<double>(Frequency()); }
    static double TicksToUs(std::int64_t ticks) { return static_cast<double>(ticks) * 1'000'000.0 / static_cast<double>(Frequency()); }
    static std::int64_t MsToTicks(double ms) { return static_cast<std::int64_t>(ms * static_cast<double>(Frequency()) / 1000.0); }
    static std::int64_t UsToTicks(double us) { return static_cast<std::int64_t>(us * static_cast<double>(Frequency()) / 1'000'000.0); }
};

} // namespace ff::timing
