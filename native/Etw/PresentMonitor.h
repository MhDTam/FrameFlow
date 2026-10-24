#pragma once
#include <cstdint>
#include <vector>

namespace ff::etw {

// Measures a game's real present cadence with ETW — the same event source PresentMon uses — without
// injecting anything into the game. It subscribes to the Microsoft-Windows-DXGI (DX10/11/12) and
// Microsoft-Windows-D3D9 providers, filters Present events by the target process, and turns their
// timestamps into frame times. Works for any game, anti-cheat or not, because it only observes
// system trace events.
//
// A real-time ETW session requires administrator rights (a Windows restriction that PresentMon shares).
// Without them Start() returns NeedElevation and no measurement happens; the driver limiter still works.

enum class StartResult : std::uint8_t {
    Ok,
    NeedElevation,   // ERROR_ACCESS_DENIED: run FrameFlow as administrator to measure
    Unsupported,     // ETW present tracing not available on this system
    Failed,
};

class PresentMonitor {
public:
    PresentMonitor();
    ~PresentMonitor();
    PresentMonitor(const PresentMonitor&) = delete;
    PresentMonitor& operator=(const PresentMonitor&) = delete;

    StartResult Start();
    void Stop();
    bool Running() const;

    // Only frames from this process are recorded. Setting a new pid discards buffered frames.
    void SetTarget(std::uint32_t pid);

    // Appends present-to-present intervals (ms) captured for `pid` since the last drain and returns
    // the count. Returns 0 if pid does not match the current target.
    std::size_t Drain(std::uint32_t pid, std::vector<double>& outMs);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace ff::etw
