#include "Timing/WaitableTimer.h"
#include "Core/Log.h"

#include <windows.h>
#include <timeapi.h>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace ff::timing {

WaitableTimer::WaitableTimer()
{
    handle_ = ::CreateWaitableTimerExW(nullptr, nullptr,
                                       CREATE_WAITABLE_TIMER_MANUAL_RESET | CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                       TIMER_ALL_ACCESS);
    if (handle_) {
        highResolution_ = true;
        return;
    }

    handle_ = ::CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_MANUAL_RESET, TIMER_ALL_ACCESS);
    if (handle_) {
        // Without the high-resolution flag the timer follows the global tick (15.6 ms by default),
        // which is useless for frame pacing. Scope the resolution change to this object.
        raisedTimerResolution_ = (::timeBeginPeriod(1) == TIMERR_NOERROR);
        FF_LOG_WARN("High-resolution waitable timer unavailable; using timeBeginPeriod(1) fallback");
    } else {
        FF_LOG_ERROR("CreateWaitableTimerExW failed: {}", ::GetLastError());
    }
}

WaitableTimer::~WaitableTimer()
{
    if (handle_) ::CloseHandle(handle_);
    if (raisedTimerResolution_) ::timeEndPeriod(1);
}

bool WaitableTimer::Wait(std::int64_t microseconds)
{
    if (!handle_ || microseconds <= 0) return false;
    LARGE_INTEGER due;
    due.QuadPart = -(microseconds * 10); // relative time, 100 ns units
    if (!::SetWaitableTimerEx(handle_, &due, 0, nullptr, nullptr, nullptr, 0)) return false;
    return ::WaitForSingleObject(handle_, INFINITE) == WAIT_OBJECT_0;
}

} // namespace ff::timing
