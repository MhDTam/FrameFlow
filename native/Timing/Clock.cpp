#include "Timing/Clock.h"

#include <windows.h>

namespace ff::timing {

std::int64_t Clock::Now()
{
    LARGE_INTEGER v;
    ::QueryPerformanceCounter(&v);
    return v.QuadPart;
}

std::int64_t Clock::Frequency()
{
    static const std::int64_t freq = [] {
        LARGE_INTEGER f;
        ::QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    return freq;
}

} // namespace ff::timing
