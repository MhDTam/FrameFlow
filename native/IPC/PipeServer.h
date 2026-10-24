#pragma once
#include "Core/Json.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace ff::ipc {

// Single-client named pipe server (the UI). Newline-delimited JSON, overlapped I/O so Stop()
// never hangs on a blocked read. The pipe DACL admits only the current user and SYSTEM, and
// remote clients are rejected.
class PipeServer {
public:
    using MessageHandler = std::function<void(const core::Json&)>;
    using ConnectHandler = std::function<void(bool connected)>;

    PipeServer(std::wstring name, MessageHandler onMessage, ConnectHandler onConnection);
    ~PipeServer();
    PipeServer(const PipeServer&) = delete;
    PipeServer& operator=(const PipeServer&) = delete;

    bool Start();
    void Stop();

    // Thread-safe. Returns false if no client is connected or the write failed.
    bool Send(const core::Json& message);
    bool Connected() const { return connected_.load(); }

private:
    void Run();
    bool ServeClient(void* pipe);
    void HandleLine(std::string_view line);

    std::wstring name_;
    MessageHandler onMessage_;
    ConnectHandler onConnection_;
    std::thread thread_;
    void* stopEvent_ = nullptr;
    std::atomic<bool> connected_{false};
    std::atomic<bool> firstInstanceFailed_{false};

    std::mutex writeMutex_;
    void* currentPipe_ = nullptr;   // guarded by writeMutex_
    void* writeEvent_ = nullptr;
};

} // namespace ff::ipc
