#include "Core/Log.h"
#include "Core/Paths.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <fstream>
#include <mutex>

namespace ff::core {

namespace {

constexpr std::uintmax_t kMaxLogBytes = 8ull * 1024 * 1024;

struct LogState {
    std::mutex mutex;
    std::ofstream file;
    bool console = false;
};

LogState& State()
{
    static LogState state;
    return state;
}

std::atomic<int> g_level{static_cast<int>(LogLevel::Info)};

} // namespace

std::string_view ToString(LogLevel level)
{
    switch (level) {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Warning: return "WARN";
    case LogLevel::Error: return "ERROR";
    }
    return "?";
}

bool ParseLogLevel(std::string_view text, LogLevel& out)
{
    for (int i = 0; i <= static_cast<int>(LogLevel::Error); ++i) {
        const auto lvl = static_cast<LogLevel>(i);
        std::string_view name = ToString(lvl);
        if (text.size() == name.size()) {
            bool eq = true;
            for (size_t k = 0; k < name.size(); ++k) {
                char c = text[k];
                if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
                if (c != name[k]) { eq = false; break; }
            }
            if (eq) { out = lvl; return true; }
        }
    }
    if (text == "warning" || text == "Warning") { out = LogLevel::Warning; return true; }
    return false;
}

void Log::Initialize(std::wstring_view component, LogLevel level, bool alsoConsole)
{
    auto& s = State();
    std::scoped_lock lock(s.mutex);
    g_level = static_cast<int>(level);
    s.console = alsoConsole;

    const auto path = LogsDir() / (std::wstring(component) + L".log");
    std::error_code ec;
    const bool rotate = std::filesystem::exists(path, ec) && std::filesystem::file_size(path, ec) > kMaxLogBytes;
    if (rotate) {
        auto old = path;
        old += L".old";
        std::filesystem::remove(old, ec);
        std::filesystem::rename(path, old, ec);
    }
    s.file.open(path, std::ios::out | std::ios::app);
}

void Log::Shutdown()
{
    auto& s = State();
    std::scoped_lock lock(s.mutex);
    if (s.file.is_open()) s.file.close();
}

void Log::SetLevel(LogLevel level) { g_level = static_cast<int>(level); }
LogLevel Log::Level() { return static_cast<LogLevel>(g_level.load(std::memory_order_relaxed)); }

void Log::Write(LogLevel level, std::string_view message)
{
    SYSTEMTIME t{};
    ::GetLocalTime(&t);
    const std::string line = std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03} [{}] [{}] {}\n",
                                         t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
                                         t.wMilliseconds, ToString(level), ::GetCurrentThreadId(), message);
    auto& s = State();
    std::scoped_lock lock(s.mutex);
    if (s.file.is_open()) {
        s.file << line;
        s.file.flush();
    }
    if (s.console) {
        std::fputs(line.c_str(), level >= LogLevel::Warning ? stderr : stdout);
    }
}

} // namespace ff::core
