#include "EngineApp.h"

#include "Core/Log.h"

#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <string_view>

// FrameFlow.Engine is a windowless background process. It is started by FrameFlow.UI and lives
// until the UI sends Shutdown (or the user logs off). Pass --console to see logs in a console.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    bool console = false;
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) {
        std::wstring_view arg = argv[i];
        if (arg == L"--console") {
            console = true;
        } else if (arg == L"--portable") {
            ::SetEnvironmentVariableW(L"FRAMEFLOW_PORTABLE", L"1");
        } else if (arg == L"--data-dir" && i + 1 < argc) {
            ::SetEnvironmentVariableW(L"FRAMEFLOW_DATA_DIR", argv[++i]);
        }
    }
    if (argv) ::LocalFree(argv);

    if (console && ::AllocConsole()) {
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    }

    // One engine per user session.
    HANDLE instance = ::CreateMutexW(nullptr, TRUE, L"Local\\FrameFlow.Engine.Instance");
    if (!instance || ::GetLastError() == ERROR_ALREADY_EXISTS) {
        if (instance) ::CloseHandle(instance);
        return 0;
    }

    ff::core::Log::Initialize(L"engine", ff::core::LogLevel::Info, console);
    int rc = 1;
    try {
        ff::engine::EngineApp app;
        rc = app.Run();
    } catch (const std::exception& e) {
        FF_LOG_ERROR("Fatal: {}", e.what());
    }
    ff::core::Log::Shutdown();
    ::ReleaseMutex(instance);
    ::CloseHandle(instance);
    return rc;
}
