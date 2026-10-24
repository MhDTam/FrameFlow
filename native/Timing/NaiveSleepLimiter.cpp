#include "Timing/NaiveSleepLimiter.h"
#include "Timing/Clock.h"

#include <windows.h>

namespace ff::timing {

std::int64_t NaiveSleepLimiter::WaitForNextFrame()
{
    const std::int64_t now = Clock::Now();
    if (last_ != 0) {
        const double elapsedMs = Clock::TicksToMs(now - last_);
        const double remainingMs = 1000.0 / fps_ - elapsedMs;
        if (remainingMs >= 1.0) ::Sleep(static_cast<DWORD>(remainingMs));
    }
    last_ = Clock::Now();
    return last_;
}

} // namespace ff::timing
