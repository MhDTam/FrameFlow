#pragma once
#include <cstdint>

namespace ff::timing {

// RAII wrapper around a waitable timer used for the coarse phase of frame waits.
//
// On Windows 10 1803+ we request CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, which gives
// ~0.5 ms wake granularity without touching the global timer resolution. If that flag is
// unavailable we fall back to a normal timer and raise the process timer resolution with
// timeBeginPeriod(1) for the lifetime of this object only (released in the destructor).
class WaitableTimer {
public:
    WaitableTimer();
    ~WaitableTimer();
    WaitableTimer(const WaitableTimer&) = delete;
    WaitableTimer& operator=(const WaitableTimer&) = delete;

    bool Valid() const { return handle_ != nullptr; }
    bool IsHighResolution() const { return highResolution_; }

    // Blocks the calling thread for approximately `microseconds`. Returns false on failure,
    // in which case the caller should fall back to its precision phase.
    bool Wait(std::int64_t microseconds);

private:
    void* handle_ = nullptr;
    bool highResolution_ = false;
    bool raisedTimerResolution_ = false;
};

} // namespace ff::timing
