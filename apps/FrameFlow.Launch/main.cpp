// Opt-in x64 DX11/DX12 launcher. The hook is loaded before the game's entry point.
#include "Core/Strings.h"
#include "ProcessDetection/AntiCheatDetector.h"
#include "ProcessDetection/HookCompatibility.h"

#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::wstring Quote(const std::wstring& arg)
{
    std::wstring out = L"\"";
    unsigned slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'"') out.append(slashes * 2 + 1, L'\\');
        else out.append(slashes, L'\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    return out + L'"';
}

HMODULE RemoteModule(DWORD pid, const wchar_t* name)
{
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return nullptr;
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    HMODULE found = nullptr;
    if (::Module32FirstW(snap, &entry)) {
        do {
            if (_wcsicmp(entry.szModule, name) == 0) { found = entry.hModule; break; }
        } while (::Module32NextW(snap, &entry));
    }
    ::CloseHandle(snap);
    return found;
}

std::vector<std::string> RunningNames()
{
    std::vector<std::string> names;
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return names;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (::Process32FirstW(snap, &entry)) {
        do { names.push_back(ff::core::WideToUtf8(entry.szExeFile)); }
        while (::Process32NextW(snap, &entry));
    }
    ::CloseHandle(snap);
    return names;
}

bool Inject(HANDLE process, DWORD pid, const std::wstring& dll)
{
    const auto localLoad = ::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HMODULE localOwner = nullptr;
    if (!localLoad || !::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(localLoad), &localOwner)) return false;
    wchar_t ownerPath[MAX_PATH]{};
    if (!::GetModuleFileNameW(localOwner, ownerPath, MAX_PATH)) return false;
    const std::wstring ownerName = std::filesystem::path(ownerPath).filename().wstring();
    HMODULE remoteOwner = nullptr;
    for (int i = 0; i < 250 && !remoteOwner; ++i) {
        remoteOwner = RemoteModule(pid, ownerName.c_str());
        if (!remoteOwner) ::Sleep(20);
    }
    if (!remoteOwner) return false;
    const auto offset = reinterpret_cast<std::uintptr_t>(localLoad) - reinterpret_cast<std::uintptr_t>(localOwner);
    auto remoteLoad = reinterpret_cast<LPTHREAD_START_ROUTINE>(reinterpret_cast<std::uintptr_t>(remoteOwner) + offset);

    const SIZE_T bytes = (dll.size() + 1) * sizeof(wchar_t);
    void* remotePath = ::VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remotePath) return false;
    bool ok = false;
    bool finished = false;
    if (::WriteProcessMemory(process, remotePath, dll.c_str(), bytes, nullptr)) {
        HANDLE thread = ::CreateRemoteThread(process, nullptr, 0, remoteLoad, remotePath, 0, nullptr);
        if (thread) {
            finished = ::WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0;
            if (finished)
                ok = RemoteModule(pid, L"FrameFlow.Hook64.dll") != nullptr;
            ::CloseHandle(thread);
        }
    }
    // On timeout the thread may still read the path, so leave the tiny allocation in the process.
    if (finished) ::VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
    if (ok) {
        const std::wstring readyName = L"Local\\FrameFlow.HookReady." + std::to_wstring(pid);
        HANDLE ready = nullptr;
        for (int i = 0; i < 250 && !ready; ++i) {
            ready = ::OpenEventW(SYNCHRONIZE, FALSE, readyName.c_str());
            if (!ready) ::Sleep(20);
        }
        ok = ready && ::WaitForSingleObject(ready, 5000) == WAIT_OBJECT_0;
        if (ready) ::CloseHandle(ready);
    }
    return ok;
}

// Start as a debugger so the game stops at its executable entry point after loader initialization.
// The debugger is detached before loading the hook; games are never run under a persistent debugger.
bool PauseAtEntry(const PROCESS_INFORMATION& child)
{
    std::uintptr_t entry = 0;
    BYTE saved = 0;
    bool armed = false;
    bool paused = false;
    const ULONGLONG until = ::GetTickCount64() + 15000;
    while (::GetTickCount64() < until) {
        DEBUG_EVENT event{};
        if (!::WaitForDebugEvent(&event, 500)) continue;
        DWORD disposition = DBG_CONTINUE;
        if (event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT) {
            if (event.u.CreateProcessInfo.hFile) ::CloseHandle(event.u.CreateProcessInfo.hFile);
            auto* base = static_cast<std::byte*>(event.u.CreateProcessInfo.lpBaseOfImage);
            IMAGE_DOS_HEADER dos{};
            IMAGE_NT_HEADERS64 nt{};
            SIZE_T read = 0;
            if (base && ::ReadProcessMemory(child.hProcess, base, &dos, sizeof(dos), &read) &&
                dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 && dos.e_lfanew < 4096 &&
                ::ReadProcessMemory(child.hProcess, base + dos.e_lfanew, &nt, sizeof(nt), &read) &&
                nt.Signature == IMAGE_NT_SIGNATURE && nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
                entry = reinterpret_cast<std::uintptr_t>(base) + nt.OptionalHeader.AddressOfEntryPoint;
                DWORD old = 0;
                if (::ReadProcessMemory(child.hProcess, reinterpret_cast<void*>(entry), &saved, 1, &read) &&
                    ::VirtualProtectEx(child.hProcess, reinterpret_cast<void*>(entry), 1, PAGE_EXECUTE_READWRITE, &old)) {
                    const BYTE trap = 0xCC;
                    SIZE_T written = 0;
                    armed = ::WriteProcessMemory(child.hProcess, reinterpret_cast<void*>(entry), &trap, 1, &written) &&
                        written == 1;
                    ::FlushInstructionCache(child.hProcess, reinterpret_cast<void*>(entry), 1);
                    DWORD ignored = 0;
                    ::VirtualProtectEx(child.hProcess, reinterpret_cast<void*>(entry), 1, old, &ignored);
                }
            }
        } else if (event.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT) {
            if (event.u.LoadDll.hFile) ::CloseHandle(event.u.LoadDll.hFile);
        } else if (event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
            const auto& exception = event.u.Exception.ExceptionRecord;
            if (exception.ExceptionCode == EXCEPTION_BREAKPOINT) {
                CONTEXT context{};
                context.ContextFlags = CONTEXT_CONTROL;
                if (armed && event.dwThreadId == child.dwThreadId &&
                    ::GetThreadContext(child.hThread, &context) && context.Rip == entry + 1) {
                    DWORD old = 0;
                    if (::VirtualProtectEx(child.hProcess, reinterpret_cast<void*>(entry), 1,
                                           PAGE_EXECUTE_READWRITE, &old)) {
                        SIZE_T written = 0;
                        ::WriteProcessMemory(child.hProcess, reinterpret_cast<void*>(entry), &saved, 1, &written);
                        ::FlushInstructionCache(child.hProcess, reinterpret_cast<void*>(entry), 1);
                        DWORD ignored = 0;
                        ::VirtualProtectEx(child.hProcess, reinterpret_cast<void*>(entry), 1, old, &ignored);
                    }
                    context.Rip = entry;
                    if (::SetThreadContext(child.hThread, &context) &&
                        ::SuspendThread(child.hThread) != static_cast<DWORD>(-1)) paused = true;
                    armed = false;
                } else {
                    // The Windows loader's initial breakpoint is expected and should be consumed.
                    disposition = DBG_CONTINUE;
                }
            } else disposition = DBG_EXCEPTION_NOT_HANDLED;
        } else if (event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
            ::ContinueDebugEvent(event.dwProcessId, event.dwThreadId, disposition);
            break;
        }
        ::ContinueDebugEvent(event.dwProcessId, event.dwThreadId, disposition);
        if (paused) break;
    }
    // If startup did not reach the entry point, restore the breakpoint before detaching.
    if (!paused && armed) {
        DWORD old = 0;
        if (::VirtualProtectEx(child.hProcess, reinterpret_cast<void*>(entry), 1, PAGE_EXECUTE_READWRITE, &old)) {
            ::WriteProcessMemory(child.hProcess, reinterpret_cast<void*>(entry), &saved, 1, nullptr);
            ::FlushInstructionCache(child.hProcess, reinterpret_cast<void*>(entry), 1);
            DWORD ignored = 0;
            ::VirtualProtectEx(child.hProcess, reinterpret_cast<void*>(entry), 1, old, &ignored);
        }
    }
    ::DebugActiveProcessStop(child.dwProcessId);
    return paused;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    bool allowProtected = false;
    bool diagnoseUnsafe = false;
    int first = 1;
    while (first < argc) {
        if (std::wcscmp(argv[first], L"--allow-protected") == 0 && !allowProtected) allowProtected = true;
        else if (std::wcscmp(argv[first], L"--diagnose-unsafe") == 0 && !diagnoseUnsafe) diagnoseUnsafe = true;
        else break;
        ++first;
    }
    const bool attach = argc > first && std::wcscmp(argv[first], L"--attach") == 0;
    const int exeArg = attach ? first + 2 : first;
    if (argc <= exeArg || (diagnoseUnsafe && !attach)) {
        std::wcerr << L"Usage: FrameFlow.Launch.exe [--allow-protected] <game.exe> [arguments...]\n"
                   << L"       FrameFlow.Launch.exe [--allow-protected] [--diagnose-unsafe] --attach <pid> <game.exe>\n";
        return 2;
    }
    const std::filesystem::path exe = std::filesystem::absolute(argv[exeArg]);
    if (!std::filesystem::exists(exe) || _wcsicmp(exe.extension().c_str(), L".exe") != 0) {
        std::wcerr << L"Game executable not found.\n";
        return 2;
    }
    if (ff::process::IsKnownUnsafeHookTarget(ff::core::WideToUtf8(exe.filename().wstring())) && !diagnoseUnsafe) {
        std::wcerr << L"FrameFlow's DXGI hook is disabled for this game after repeated crashes. "
                      L"Start the game from Steam and choose the NVIDIA limiter manually if available. "
                      L"A diagnostic attach requires --diagnose-unsafe.\n";
        return 8;
    }
    if (diagnoseUnsafe)
        std::wcerr << L"Diagnostic attach: this game has crashed with FrameFlow before.\n";
    const auto hit = ff::process::DetectAntiCheat(ff::core::WideToUtf8(exe.filename().wstring()), {},
        RunningNames(), ff::process::CollectNearbyFiles(exe.wstring()));
    if (hit && !allowProtected) {
        if (attach) {
            std::wcerr << L"Protected game: automatic attachment requires an explicit per-game risk choice.\n";
            return 7;
        }
        // A protected game may still be started normally. Never debug or inject into it.
        std::wstring command = Quote(exe.wstring());
        for (int i = exeArg + 1; i < argc; ++i) command += L" " + Quote(argv[i]);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION child{};
        const std::wstring cwd = exe.parent_path().wstring();
        if (!::CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
                              nullptr, cwd.c_str(), &startup, &child)) {
            std::wcerr << L"Could not start protected game (Windows error " << ::GetLastError() << L").\n";
            return 5;
        }
        ::CloseHandle(child.hThread);
        ::CloseHandle(child.hProcess);
        std::wcerr << L"Game started normally. FrameFlow limiting is unavailable because "
                   << ff::core::Utf8ToWide(hit->product) << L" protects this game.\n";
        return 7;
    }
    wchar_t self[MAX_PATH]{};
    if (!::GetModuleFileNameW(nullptr, self, MAX_PATH)) return 4;
    const std::filesystem::path dll = std::filesystem::path(self).parent_path() / L"FrameFlow.Hook64.dll";
    if (!std::filesystem::exists(dll)) {
        std::wcerr << L"FrameFlow.Hook64.dll is missing next to the launcher.\n";
        return 4;
    }

    if (attach) {
        wchar_t* end = nullptr;
        const unsigned long requested = std::wcstoul(argv[first + 1], &end, 10);
        if (!end || *end || requested == 0) return 2;
        const DWORD pid = static_cast<DWORD>(requested);
        HANDLE process = ::OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_LIMITED_INFORMATION |
                                       PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
        if (!process) return 5;
        wchar_t actual[32768]{};
        DWORD length = static_cast<DWORD>(std::size(actual));
        const bool sameImage = ::QueryFullProcessImageNameW(process, 0, actual, &length) &&
                               _wcsicmp(actual, exe.c_str()) == 0;
        if (sameImage && diagnoseUnsafe && RemoteModule(pid, L"gameoverlayrenderer64.dll")) {
            ::CloseHandle(process);
            std::wcerr << L"Steam Overlay is still loaded in this game. Disable it for Euro Truck "
                          L"in Steam properties and restart the game before diagnostic attachment.\n";
            return 9;
        }
        HANDLE diagnostics = nullptr;
        if (sameImage && diagnoseUnsafe) {
            const std::wstring name = L"Local\\FrameFlow.HookDiagnostics." + std::to_wstring(pid);
            diagnostics = ::CreateEventW(nullptr, TRUE, FALSE, name.c_str());
            if (!diagnostics) {
                ::CloseHandle(process);
                std::wcerr << L"Could not enable hook diagnostics.\n";
                return 5;
            }
        }
        const bool injected = sameImage && Inject(process, pid, dll.wstring());
        if (diagnostics) ::CloseHandle(diagnostics);
        ::CloseHandle(process);
        if (!injected) {
            std::wcerr << L"Could not attach to the running game. It may reject external hooks.\n";
            return 6;
        }
        return 0;
    }

    std::wstring command = Quote(exe.wstring());
    for (int i = exeArg + 1; i < argc; ++i) command += L" " + Quote(argv[i]);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    const std::wstring cwd = exe.parent_path().wstring();
    ::DebugSetProcessKillOnExit(FALSE);
    if (!::CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, DEBUG_ONLY_THIS_PROCESS,
                          nullptr, cwd.c_str(), &startup, &child)) {
        std::wcerr << L"Could not start game (Windows error " << ::GetLastError() << L").\n";
        return 5;
    }

    const bool paused = PauseAtEntry(child);
    const bool injected = paused && Inject(child.hProcess, child.dwProcessId, dll.wstring());
    if (paused) {
        ::ResumeThread(child.hThread);
    }
    ::CloseHandle(child.hThread);
    ::CloseHandle(child.hProcess);
    if (!injected) {
        std::wcerr << L"Game started, but the DXGI hook did not load. Check FrameFlow's reported limit method.\n";
        return 6;
    }
    std::wcout << L"DXGI hook loaded into process " << child.dwProcessId << L".\n";
    return 0;
}
