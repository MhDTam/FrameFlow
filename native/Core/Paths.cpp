#include "Core/Paths.h"

#include <windows.h>
#include <shlobj.h>

namespace ff::core {

namespace {

std::filesystem::path EnsureDir(std::filesystem::path p)
{
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p;
}

bool CheckDirForPortable(const std::filesystem::path& dir)
{
    if (dir.empty()) return false;
    std::error_code ec;
    if (std::filesystem::exists(dir / L".portable", ec) ||
        std::filesystem::exists(dir / L"portable", ec) ||
        std::filesystem::exists(dir / L"portable.txt", ec) ||
        std::filesystem::is_directory(dir / L"data", ec)) {
        return true;
    }
    return false;
}

} // namespace

std::filesystem::path ExecutableDir()
{
    wchar_t buf[MAX_PATH * 2]{};
    const DWORD n = ::GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0) return std::filesystem::current_path();
    return std::filesystem::path(buf).parent_path();
}

std::filesystem::path ModuleDir()
{
    HMODULE hMod = nullptr;
    if (::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             reinterpret_cast<LPCWSTR>(&AppDataRoot), &hMod) && hMod) {
        wchar_t buf[MAX_PATH * 2]{};
        const DWORD n = ::GetModuleFileNameW(hMod, buf, static_cast<DWORD>(std::size(buf)));
        if (n > 0) return std::filesystem::path(buf).parent_path();
    }
    return ExecutableDir();
}

bool IsPortableMode()
{
    // 1. Explicit environment variable
    wchar_t envPortable[16]{};
    if (::GetEnvironmentVariableW(L"FRAMEFLOW_PORTABLE", envPortable, static_cast<DWORD>(std::size(envPortable))) > 0) {
        if (envPortable[0] != L'0' && _wcsicmp(envPortable, L"false") != 0) {
            return true;
        }
    }

    // 2. Marker in Executable directory
    if (CheckDirForPortable(ExecutableDir())) {
        return true;
    }

    // 3. Marker in Module directory (e.g. for Hook64.dll loaded into game process)
    const auto modDir = ModuleDir();
    if (modDir != ExecutableDir() && CheckDirForPortable(modDir)) {
        return true;
    }

    return false;
}

std::filesystem::path AppDataRoot()
{
    // FRAMEFLOW_DATA_DIR lets tests, tools, and parent processes override the location.
    wchar_t overrideDir[MAX_PATH]{};
    if (::GetEnvironmentVariableW(L"FRAMEFLOW_DATA_DIR", overrideDir, MAX_PATH) > 0 && overrideDir[0] != L'\0') {
        return EnsureDir(overrideDir);
    }

    if (IsPortableMode()) {
        std::filesystem::path target;
        const auto modDir = ModuleDir();
        if (CheckDirForPortable(modDir)) {
            target = modDir / L"data";
        } else {
            target = ExecutableDir() / L"data";
        }
        ::SetEnvironmentVariableW(L"FRAMEFLOW_DATA_DIR", target.c_str());
        ::SetEnvironmentVariableW(L"FRAMEFLOW_PORTABLE", L"1");
        return EnsureDir(target);
    }

    PWSTR raw = nullptr;
    std::filesystem::path base;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw)) && raw) {
        base = raw;
    }
    ::CoTaskMemFree(raw);
    if (base.empty()) base = std::filesystem::temp_directory_path();
    return EnsureDir(base / L"FrameFlow");
}

std::filesystem::path ProfilesDir() { return EnsureDir(AppDataRoot() / L"Profiles"); }
std::filesystem::path LogsDir() { return EnsureDir(AppDataRoot() / L"Logs"); }
std::filesystem::path SettingsFile() { return AppDataRoot() / L"settings.json"; }

} // namespace ff::core
