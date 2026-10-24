#include "ProcessDetection/ProcessScanner.h"
#include "Core/Strings.h"
#include "IPC/SharedSession.h"

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <unordered_set>

namespace ff::process {

namespace {

constexpr auto kModuleRefresh = std::chrono::seconds(5);

struct WindowInfo {
    HWND hwnd = nullptr;
    long area = 0;
    bool coversMonitor = false;
    std::wstring title;
};

struct EnumContext {
    std::unordered_map<DWORD, WindowInfo>* windows;
};

bool IsCandidateWindow(HWND hwnd)
{
    if (!::IsWindowVisible(hwnd) || ::IsIconic(hwnd)) return false;
    if (::GetWindow(hwnd, GW_OWNER) != nullptr) return false;
    const LONG_PTR ex = ::GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return false;
    RECT r{};
    if (!::GetWindowRect(hwnd, &r)) return false;
    return (r.right - r.left) >= 320 && (r.bottom - r.top) >= 200;
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lp)
{
    auto* ctx = reinterpret_cast<EnumContext*>(lp);
    if (!IsCandidateWindow(hwnd)) return TRUE;
    DWORD pid = 0;
    ::GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return TRUE;

    RECT r{};
    ::GetWindowRect(hwnd, &r);
    const long area = (r.right - r.left) * (r.bottom - r.top);

    auto& best = (*ctx->windows)[pid];
    if (area <= best.area) return TRUE;

    best.hwnd = hwnd;
    best.area = area;
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (::GetMonitorInfoW(::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
        best.coversMonitor = r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top &&
                             r.right >= mi.rcMonitor.right && r.bottom >= mi.rcMonitor.bottom;
    }
    wchar_t title[256]{};
    ::GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
    best.title = title;
    return TRUE;
}

std::vector<std::string> ListModules(HANDLE process, bool& ok)
{
    std::vector<std::string> out;
    HMODULE mods[1024];
    DWORD needed = 0;
    ok = ::EnumProcessModulesEx(process, mods, sizeof(mods), &needed, LIST_MODULES_64BIT) != FALSE;
    if (!ok) return out;
    const DWORD count = std::min<DWORD>(needed / sizeof(HMODULE), static_cast<DWORD>(std::size(mods)));
    out.reserve(count);
    wchar_t name[MAX_PATH];
    for (DWORD i = 0; i < count; ++i) {
        if (::GetModuleBaseNameW(process, mods[i], name, MAX_PATH)) {
            out.push_back(core::ToLowerAscii(core::WideToUtf8(name)));
        }
    }
    return out;
}

bool HasModule(const std::vector<std::string>& mods, std::string_view name)
{
    return std::find(mods.begin(), mods.end(), name) != mods.end();
}

} // namespace

bool ProcessScanner::IsProcessAlive(std::uint32_t pid)
{
    HANDLE h = ::OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (h) {
        const bool alive = ::WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
        ::CloseHandle(h);
        return alive;
    }
    // A higher-integrity game can deny OpenProcess. Its process-table entry still tells us
    // whether it exists, so do not repeatedly report a still-running game as closed.
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return true; // unknown: preserve current selection
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    for (BOOL ok = ::Process32FirstW(snap, &entry); ok; ok = ::Process32NextW(snap, &entry)) {
        if (entry.th32ProcessID == pid) { found = true; break; }
    }
    ::CloseHandle(snap);
    return found;
}

std::vector<Candidate> ProcessScanner::Scan(const ExclusionList& exclusions)
{
    // 1. Process table: names and parents for every process.
    std::unordered_map<DWORD, std::pair<std::string, DWORD>> table; // pid -> (exe, parent)
    running_.clear();
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        for (BOOL ok = ::Process32FirstW(snap, &pe); ok; ok = ::Process32NextW(snap, &pe)) {
            std::string exe = core::WideToUtf8(pe.szExeFile);
            running_.push_back(core::ToLowerAscii(exe));
            table[pe.th32ProcessID] = {std::move(exe), pe.th32ParentProcessID};
        }
        ::CloseHandle(snap);
    }

    // 2. Visible top-level windows, best (largest) per process.
    std::unordered_map<DWORD, WindowInfo> windows;
    EnumContext ctx{&windows};
    ::EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&ctx));

    DWORD fgPid = 0;
    if (HWND fg = ::GetForegroundWindow()) ::GetWindowThreadProcessId(fg, &fgPid);
    const DWORD selfPid = ::GetCurrentProcessId();

    std::vector<Candidate> result;
    std::unordered_set<std::uint32_t> seen;
    const auto now = std::chrono::steady_clock::now();

    for (const auto& [pid, win] : windows) {
        if (pid == selfPid) continue;
        const auto it = table.find(pid);
        if (it == table.end()) continue;

        Candidate c;
        c.hwnd = win.hwnd;
        auto& s = c.signals;
        s.pid = pid;
        s.exeName = it->second.first;
        s.windowTitle = core::WideToUtf8(win.title);
        s.hasVisibleWindow = true;
        s.isForeground = (pid == fgPid);
        s.coversMonitor = win.coversMonitor;
        const auto parent = table.find(it->second.second);
        if (parent != table.end()) s.parentExeName = parent->second.first;

        if (exclusions.Contains(s.exeName)) continue; // never open excluded processes
        seen.insert(pid);

        // Query the executable path separately: VM_READ is often denied for elevated games,
        // while PROCESS_QUERY_LIMITED_INFORMATION may still be allowed.
        HANDLE pathHandle = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (pathHandle) {
            wchar_t path[MAX_PATH * 2];
            DWORD len = static_cast<DWORD>(std::size(path));
            if (::QueryFullProcessImageNameW(pathHandle, 0, path, &len))
                s.exePath = core::WideToUtf8(std::wstring_view(path, len));
            ::CloseHandle(pathHandle);
        }
        HANDLE h = ::OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        s.accessible = h != nullptr;
        if (h) {
            auto& cache = moduleCache_[pid];
            if (cache.refreshed.time_since_epoch().count() == 0 || now - cache.refreshed > kModuleRefresh) {
                bool ok = false;
                cache.modules = ListModules(h, ok);
                cache.accessible = ok;
                cache.refreshed = now;
            }
            c.moduleNames = cache.modules;
            ::CloseHandle(h);
        }

        const auto& m = c.moduleNames;
        s.usesDxgi = HasModule(m, "dxgi.dll");
        s.usesD3D11 = HasModule(m, "d3d11.dll");
        s.usesD3D12 = HasModule(m, "d3d12.dll") || HasModule(m, "d3d12core.dll");
        s.usesD3D9 = HasModule(m, "d3d9.dll");
        s.usesVulkan = HasModule(m, "vulkan-1.dll");
        s.usesOpenGL = HasModule(m, "opengl32.dll");

        if (HANDLE sess = ::OpenFileMappingW(FILE_MAP_READ, FALSE, ipc::SessionName(pid).c_str())) {
            s.hasFrameFlowSession = true;
            ::CloseHandle(sess);
        }

        c.score = ScoreProcess(s, exclusions);
        result.push_back(std::move(c));
    }

    // Drop cache entries for processes that are gone.
    for (auto it = moduleCache_.begin(); it != moduleCache_.end();) {
        it = seen.count(it->first) ? std::next(it) : moduleCache_.erase(it);
    }

    std::sort(result.begin(), result.end(), [](const Candidate& a, const Candidate& b) { return a.score.score > b.score.score; });
    return result;
}

} // namespace ff::process
