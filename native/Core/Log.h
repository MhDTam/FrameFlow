#pragma once
#include <format>
#include <string>
#include <string_view>

namespace ff::core {

enum class LogLevel : int { Trace = 0, Debug = 1, Info = 2, Warning = 3, Error = 4 };

class Log {
public:
    // Opens %LOCALAPPDATA%\FrameFlow\Logs\<component>.log. Safe to call once per process.
    static void Initialize(std::wstring_view component, LogLevel level = LogLevel::Info, bool alsoConsole = false);
    static void Shutdown();

    static void SetLevel(LogLevel level);
    static LogLevel Level();
    static bool Enabled(LogLevel level) { return static_cast<int>(level) >= static_cast<int>(Level()); }

    static void Write(LogLevel level, std::string_view message);

    template <class... Args>
    static void Print(LogLevel level, std::format_string<Args...> fmt, Args&&... args)
    {
        if (!Enabled(level)) return;
        Write(level, std::format(fmt, std::forward<Args>(args)...));
    }
};

std::string_view ToString(LogLevel level);
bool ParseLogLevel(std::string_view text, LogLevel& out);

} // namespace ff::core

#define FF_LOG_TRACE(...) ::ff::core::Log::Print(::ff::core::LogLevel::Trace, __VA_ARGS__)
#define FF_LOG_DEBUG(...) ::ff::core::Log::Print(::ff::core::LogLevel::Debug, __VA_ARGS__)
#define FF_LOG_INFO(...)  ::ff::core::Log::Print(::ff::core::LogLevel::Info, __VA_ARGS__)
#define FF_LOG_WARN(...)  ::ff::core::Log::Print(::ff::core::LogLevel::Warning, __VA_ARGS__)
#define FF_LOG_ERROR(...) ::ff::core::Log::Print(::ff::core::LogLevel::Error, __VA_ARGS__)
