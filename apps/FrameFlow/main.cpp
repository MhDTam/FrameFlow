#include <windows.h>
#include <shellapi.h>
#include <filesystem>
#include <string>

namespace {
std::wstring envValue(const wchar_t* name)
{
    const DWORD length = ::GetEnvironmentVariableW(name, nullptr, 0);
    if (length == 0) return {};
    std::wstring value(length, L'\0');
    const DWORD written = ::GetEnvironmentVariableW(name, value.data(), length);
    if (written == 0 || written >= length) return {};
    value.resize(written);
    return value;
}

bool hasDotNet8(const std::filesystem::path& root)
{
    if (root.empty()) return false;
    const auto shared = root / L"shared" / L"Microsoft.NETCore.App";
    std::error_code error;
    if (!std::filesystem::is_directory(shared, error)) return false;
    for (std::filesystem::directory_iterator it(shared, error), end; !error && it != end;
         it.increment(error)) {
        if (it->is_directory(error) && it->path().filename().wstring().starts_with(L"8."))
            return true;
    }
    return false;
}

void selectInstalledDotNet8(const std::filesystem::path& appDir)
{
    // The standalone edition has its own hostfxr. The Lite edition uses an installed .NET 8.
    if (std::filesystem::exists(appDir / L"hostfxr.dll")) return;
    const std::filesystem::path candidates[] = {
        envValue(L"DOTNET_ROOT_X64"),
        envValue(L"DOTNET_ROOT"),
        std::filesystem::path(envValue(L"ProgramFiles")) / L"dotnet",
        std::filesystem::path(envValue(L"LOCALAPPDATA")) / L"Microsoft" / L"dotnet"
    };
    for (const auto& root : candidates) {
        if (hasDotNet8(root)) {
            ::SetEnvironmentVariableW(L"DOTNET_ROOT_X64", root.c_str());
            ::SetEnvironmentVariableW(L"DOTNET_ROOT", root.c_str());
            return;
        }
    }
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int nCmdShow)
{
    wchar_t buf[MAX_PATH * 2]{};
    const DWORD n = ::GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0) return 1;

    std::filesystem::path currentDir = std::filesystem::path(buf).parent_path();
    std::filesystem::path target = currentDir / L"FrameFlow.UI.exe";

    if (!std::filesystem::exists(target)) {
        std::filesystem::path subTarget = currentDir / L"bin" / L"FrameFlow.UI.exe";
        if (std::filesystem::exists(subTarget)) {
            target = subTarget;
            currentDir = currentDir / L"bin";
        } else {
            ::MessageBoxW(nullptr, L"FrameFlow.UI.exe was not found.", L"FrameFlow", MB_OK | MB_ICONERROR);
            return 1;
        }
    }

    std::wstring cmd = L"\"" + target.wstring() + L"\"";
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) {
        cmd += L" \"";
        cmd += argv[i];
        cmd += L"\"";
    }
    if (argv) ::LocalFree(argv);

    selectInstalledDotNet8(currentDir);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = static_cast<WORD>(nCmdShow);
    PROCESS_INFORMATION pi{};

    const BOOL ok = ::CreateProcessW(
        target.c_str(),
        cmd.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        currentDir.c_str(),
        &si,
        &pi);

    if (ok) {
        ::CloseHandle(pi.hProcess);
        ::CloseHandle(pi.hThread);
        return 0;
    }

    ::MessageBoxW(nullptr, L"Failed to launch FrameFlow.UI.exe.", L"FrameFlow", MB_OK | MB_ICONERROR);
    return 1;
}
