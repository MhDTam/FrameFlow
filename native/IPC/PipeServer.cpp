#include "IPC/PipeServer.h"
#include "IPC/Protocol.h"
#include "Core/Log.h"

#include <windows.h>
#include <sddl.h>

#include <memory>
#include <vector>

namespace ff::ipc {

namespace {

// Builds "D:P(A;;GA;;;<current user SID>)(A;;GA;;;SY)" so no other account can open the pipe.
struct PipeSecurity {
    PSECURITY_DESCRIPTOR sd = nullptr;
    SECURITY_ATTRIBUTES sa{};
    ~PipeSecurity() { if (sd) ::LocalFree(sd); }

    bool Init()
    {
        HANDLE token = nullptr;
        if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
        DWORD len = 0;
        ::GetTokenInformation(token, TokenUser, nullptr, 0, &len);
        std::vector<BYTE> buf(len);
        const bool ok = len > 0 && ::GetTokenInformation(token, TokenUser, buf.data(), len, &len);
        ::CloseHandle(token);
        if (!ok) return false;

        LPWSTR sidStr = nullptr;
        if (!::ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid, &sidStr)) return false;
        const std::wstring sddl = L"D:P(A;;GA;;;" + std::wstring(sidStr) + L")(A;;GA;;;SY)";
        ::LocalFree(sidStr);

        if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) return false;
        sa.nLength = sizeof(sa);
        sa.lpSecurityDescriptor = sd;
        sa.bInheritHandle = FALSE;
        return true;
    }
};

} // namespace

PipeServer::PipeServer(std::wstring name, MessageHandler onMessage, ConnectHandler onConnection)
    : name_(std::move(name)), onMessage_(std::move(onMessage)), onConnection_(std::move(onConnection))
{
    stopEvent_ = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    writeEvent_ = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

PipeServer::~PipeServer()
{
    Stop();
    if (stopEvent_) ::CloseHandle(stopEvent_);
    if (writeEvent_) ::CloseHandle(writeEvent_);
}

bool PipeServer::Start()
{
    if (thread_.joinable()) return true;
    ::ResetEvent(stopEvent_);
    thread_ = std::thread([this] { Run(); });
    return true;
}

void PipeServer::Stop()
{
    if (!thread_.joinable()) return;
    ::SetEvent(stopEvent_);
    thread_.join();
}

void PipeServer::Run()
{
    PipeSecurity security;
    const bool secured = security.Init();
    if (!secured) FF_LOG_WARN("Could not build pipe DACL ({}); using default security", ::GetLastError());

    bool first = true;
    while (::WaitForSingleObject(stopEvent_, 0) != WAIT_OBJECT_0) {
        DWORD openMode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED;
        if (first) openMode |= FILE_FLAG_FIRST_PIPE_INSTANCE;
        HANDLE pipe = ::CreateNamedPipeW(name_.c_str(), openMode,
                                         PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                         1, 64 * 1024, 64 * 1024, 0, secured ? &security.sa : nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            FF_LOG_ERROR("CreateNamedPipe failed: {}", ::GetLastError());
            if (::WaitForSingleObject(stopEvent_, 1000) == WAIT_OBJECT_0) break;
            continue;
        }
        first = false;

        OVERLAPPED ov{};
        ov.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        bool connected = false;
        if (::ConnectNamedPipe(pipe, &ov)) {
            connected = true;
        } else {
            const DWORD err = ::GetLastError();
            if (err == ERROR_PIPE_CONNECTED) {
                connected = true;
            } else if (err == ERROR_IO_PENDING) {
                HANDLE waits[2] = {stopEvent_, ov.hEvent};
                const DWORD w = ::WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (w == WAIT_OBJECT_0 + 1) {
                    DWORD dummy = 0;
                    connected = ::GetOverlappedResult(pipe, &ov, &dummy, FALSE) != FALSE;
                } else {
                    ::CancelIoEx(pipe, &ov);
                }
            }
        }
        ::CloseHandle(ov.hEvent);

        if (connected) {
            {
                std::scoped_lock lock(writeMutex_);
                currentPipe_ = pipe;
            }
            connected_ = true;
            FF_LOG_INFO("UI connected");
            if (onConnection_) onConnection_(true);
            ServeClient(pipe);
            {
                std::scoped_lock lock(writeMutex_);
                currentPipe_ = nullptr;
            }
            connected_ = false;
            FF_LOG_INFO("UI disconnected");
            if (onConnection_) onConnection_(false);
            ::FlushFileBuffers(pipe);
            ::DisconnectNamedPipe(pipe);
        }
        ::CloseHandle(pipe);
    }
}

bool PipeServer::ServeClient(void* pipeHandle)
{
    HANDLE pipe = pipeHandle;
    std::string pending;
    std::vector<char> buf(8192);
    OVERLAPPED ov{};
    ov.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::unique_ptr<void, decltype(&::CloseHandle)> evGuard(ov.hEvent, &::CloseHandle);

    while (true) {
        ::ResetEvent(ov.hEvent);
        DWORD read = 0;
        BOOL ok = ::ReadFile(pipe, buf.data(), static_cast<DWORD>(buf.size()), &read, &ov);
        if (!ok) {
            const DWORD err = ::GetLastError();
            if (err != ERROR_IO_PENDING) return err == ERROR_BROKEN_PIPE;
            HANDLE waits[2] = {stopEvent_, ov.hEvent};
            const DWORD w = ::WaitForMultipleObjects(2, waits, FALSE, INFINITE);
            if (w != WAIT_OBJECT_0 + 1) {
                ::CancelIoEx(pipe, &ov);
                DWORD dummy = 0;
                ::GetOverlappedResult(pipe, &ov, &dummy, TRUE);
                return false;
            }
            if (!::GetOverlappedResult(pipe, &ov, &read, FALSE)) return false;
        }
        if (read == 0) continue;

        pending.append(buf.data(), read);
        size_t pos;
        while ((pos = pending.find('\n')) != std::string::npos) {
            HandleLine(std::string_view(pending).substr(0, pos));
            pending.erase(0, pos + 1);
        }
        if (pending.size() > protocol::kMaxMessageBytes) {
            FF_LOG_WARN("Dropping UI connection: message exceeds {} bytes", protocol::kMaxMessageBytes);
            return false;
        }
    }
}

void PipeServer::HandleLine(std::string_view line)
{
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.empty()) return;
    auto msg = core::Json::Parse(line, 16);
    if (!msg || !msg->IsObject()) {
        FF_LOG_WARN("Ignoring malformed IPC message ({} bytes)", line.size());
        Send(protocol::MakeError("BadMessage", "Message is not a JSON object"));
        return;
    }
    const auto v = (*msg)["v"].AsInt(-1);
    if (v != protocol::kVersion) {
        Send(protocol::MakeError("ProtocolVersion", "Unsupported protocol version"));
        return;
    }
    if (onMessage_) onMessage_(*msg);
}

bool PipeServer::Send(const core::Json& message)
{
    std::string line = message.Dump();
    line.push_back('\n');

    std::scoped_lock lock(writeMutex_);
    if (!currentPipe_) return false;
    OVERLAPPED ov{};
    ov.hEvent = writeEvent_;
    ::ResetEvent(writeEvent_);
    DWORD written = 0;
    BOOL ok = ::WriteFile(currentPipe_, line.data(), static_cast<DWORD>(line.size()), &written, &ov);
    if (!ok && ::GetLastError() == ERROR_IO_PENDING) {
        // A UI that stops reading must not stall the engine: give up after 250 ms.
        if (::WaitForSingleObject(writeEvent_, 250) != WAIT_OBJECT_0) {
            ::CancelIoEx(currentPipe_, &ov);
            ::GetOverlappedResult(currentPipe_, &ov, &written, TRUE);
            return false;
        }
        ok = ::GetOverlappedResult(currentPipe_, &ov, &written, FALSE);
    }
    return ok && written == line.size();
}

} // namespace ff::ipc
