#include "Display/DisplayInfo.h"
#include "Core/Strings.h"

#include <windows.h>

#include <cmath>

namespace ff::display {

namespace {

// QueryDisplayConfig reports the refresh rate as a rational (60000/1001 for 59.94 Hz) which
// DEVMODE rounds away. Look up the exact value for a GDI device name when possible.
double ExactRefresh(const std::wstring& gdiName, double fallback)
{
    UINT32 pathCount = 0, modeCount = 0;
    if (::GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) return fallback;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (::QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS)
        return fallback;
    for (UINT32 i = 0; i < pathCount; ++i) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME src{};
        src.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        src.header.size = sizeof(src);
        src.header.adapterId = paths[i].sourceInfo.adapterId;
        src.header.id = paths[i].sourceInfo.id;
        if (::DisplayConfigGetDeviceInfo(&src.header) != ERROR_SUCCESS) continue;
        if (gdiName != src.viewGdiDeviceName) continue;
        const auto& r = paths[i].targetInfo.refreshRate;
        if (r.Denominator == 0) return fallback;
        const double hz = static_cast<double>(r.Numerator) / static_cast<double>(r.Denominator);
        return std::round(hz * 1000.0) / 1000.0;
    }
    return fallback;
}

MonitorInfo Describe(HMONITOR mon)
{
    MonitorInfo out;
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!::GetMonitorInfoW(mon, &mi)) return out;
    out.deviceName = core::WideToUtf8(mi.szDevice);
    out.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (::EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) {
        out.width = static_cast<int>(dm.dmPelsWidth);
        out.height = static_cast<int>(dm.dmPelsHeight);
        out.refreshHz = static_cast<double>(dm.dmDisplayFrequency);
    } else {
        out.width = mi.rcMonitor.right - mi.rcMonitor.left;
        out.height = mi.rcMonitor.bottom - mi.rcMonitor.top;
    }
    out.refreshHz = ExactRefresh(mi.szDevice, out.refreshHz);
    return out;
}

BOOL CALLBACK EnumProc(HMONITOR mon, HDC, LPRECT, LPARAM lp)
{
    reinterpret_cast<std::vector<MonitorInfo>*>(lp)->push_back(Describe(mon));
    return TRUE;
}

} // namespace

std::vector<MonitorInfo> EnumerateMonitors()
{
    std::vector<MonitorInfo> out;
    ::EnumDisplayMonitors(nullptr, nullptr, EnumProc, reinterpret_cast<LPARAM>(&out));
    return out;
}

MonitorInfo MonitorForWindow(void* hwnd)
{
    HMONITOR mon = hwnd ? ::MonitorFromWindow(static_cast<HWND>(hwnd), MONITOR_DEFAULTTOPRIMARY)
                        : ::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    return Describe(mon);
}

} // namespace ff::display
