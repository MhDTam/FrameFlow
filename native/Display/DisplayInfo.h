#pragma once
#include <string>
#include <vector>

namespace ff::display {

struct MonitorInfo {
    std::string deviceName;   // "\\.\DISPLAY1"
    int width = 0;
    int height = 0;
    double refreshHz = 0.0;   // exact when QueryDisplayConfig is available (e.g. 59.94)
    bool primary = false;
    std::string vrr = "Unknown"; // VRR capability cannot be queried reliably from user mode on all drivers
};

std::vector<MonitorInfo> EnumerateMonitors();

// Monitor containing the given window, or the primary monitor when hwnd is null.
MonitorInfo MonitorForWindow(void* hwnd);

} // namespace ff::display
