#include "Etw/PresentMonitor.h"
#include "Core/Log.h"

#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace ff::etw {

namespace {

constexpr wchar_t kSessionName[] = L"FrameFlow_PresentTrace";

// Microsoft-Windows-DXGI : {CA11C036-0102-4A2D-A6AD-F03CFED5D3C9}
constexpr GUID kDxgiProvider = {0xCA11C036, 0x0102, 0x4A2D, {0xA6, 0xAD, 0xF0, 0x3C, 0xFE, 0xD5, 0xD3, 0xC9}};
// Microsoft-Windows-D3D9  : {783ACA0A-790E-4D7F-8451-AA850511C6B9}
constexpr GUID kD3d9Provider = {0x783ACA0A, 0x790E, 0x4D7F, {0x84, 0x51, 0xAA, 0x85, 0x05, 0x11, 0xC6, 0xB9}};

// Present "start" event ids. DXGI: IDXGISwapChain::Present=42, IDXGISwapChain1::Present1=44.
// D3D9: Present=1. These are the frame-boundary markers; we only use their timestamps.
bool IsPresentStart(const GUID& provider, USHORT id)
{
    if (provider == kDxgiProvider) return id == 42 || id == 44;
    if (provider == kD3d9Provider) return id == 1;
    return false;
}

bool GuidEqual(const GUID& a, const GUID& b) { return std::memcmp(&a, &b, sizeof(GUID)) == 0; }

} // namespace

struct PresentMonitor::Impl {
    TRACEHANDLE session = 0;
    TRACEHANDLE consumer = INVALID_PROCESSTRACE_HANDLE;
    std::thread worker;
    std::vector<BYTE> propsBuffer;
    std::atomic<bool> running{false};

    std::atomic<std::uint32_t> targetPid{0};
    std::mutex mutex;
    std::uint32_t bufferedPid = 0;
    std::int64_t lastQpc = 0;
    std::vector<double> frametimes;
    double qpcFreqMs = 10'000.0; // ticks per ms (QPC is 10 MHz on this hardware; refined at Start)

    static void WINAPI RecordThunk(EVENT_RECORD* record)
    {
        static_cast<Impl*>(record->UserContext)->OnEvent(record);
    }

    void OnEvent(EVENT_RECORD* record)
    {
        const GUID& provider = record->EventHeader.ProviderId;
        if (!GuidEqual(provider, kDxgiProvider) && !GuidEqual(provider, kD3d9Provider)) return;
        if (!IsPresentStart(provider, record->EventHeader.EventDescriptor.Id)) return;

        const std::uint32_t pid = record->EventHeader.ProcessId;
        if (pid != targetPid.load(std::memory_order_relaxed)) return;

        const std::int64_t qpc = record->EventHeader.TimeStamp.QuadPart;
        std::scoped_lock lock(mutex);
        if (pid != bufferedPid) {
            bufferedPid = pid;
            lastQpc = 0;
            frametimes.clear();
        }
        if (lastQpc != 0 && qpc > lastQpc) {
            const double ms = static_cast<double>(qpc - lastQpc) / qpcFreqMs;
            if (ms > 0.0 && ms < 2000.0) frametimes.push_back(ms); // ignore alt-tab-sized gaps
        }
        lastQpc = qpc;
    }
};

PresentMonitor::PresentMonitor() : impl_(new Impl) {}

PresentMonitor::~PresentMonitor()
{
    Stop();
    delete impl_;
}

bool PresentMonitor::Running() const { return impl_ && impl_->running.load(); }

void PresentMonitor::SetTarget(std::uint32_t pid)
{
    if (impl_) impl_->targetPid.store(pid, std::memory_order_relaxed);
}

StartResult PresentMonitor::Start()
{
    if (!impl_ || impl_->running) return StartResult::Ok;

    LARGE_INTEGER freq;
    ::QueryPerformanceFrequency(&freq);
    impl_->qpcFreqMs = static_cast<double>(freq.QuadPart) / 1000.0;

    const size_t propsSize = sizeof(EVENT_TRACE_PROPERTIES) + sizeof(kSessionName);
    impl_->propsBuffer.assign(propsSize, 0);
    auto* props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(impl_->propsBuffer.data());
    props->Wnode.BufferSize = static_cast<ULONG>(propsSize);
    props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    props->Wnode.ClientContext = 1; // QPC timestamps
    props->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

    // A previous crashed run may have left the session; stop it, then start fresh.
    ::ControlTraceW(0, kSessionName, props, EVENT_TRACE_CONTROL_STOP);
    props->Wnode.BufferSize = static_cast<ULONG>(propsSize); // ControlTrace overwrites fields
    props->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

    ULONG rc = ::StartTraceW(&impl_->session, kSessionName, props);
    if (rc == ERROR_ACCESS_DENIED) {
        FF_LOG_INFO("ETW present tracing needs administrator rights; real-game measurement disabled");
        return StartResult::NeedElevation;
    }
    if (rc != ERROR_SUCCESS) {
        FF_LOG_WARN("StartTrace failed: {}", rc);
        return StartResult::Failed;
    }

    for (const GUID& provider : {kDxgiProvider, kD3d9Provider}) {
        // matchAnyKeyword = 0 → all events from the provider; we filter to Present in the callback.
        ULONG er = ::EnableTraceEx2(impl_->session, &provider, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                                    TRACE_LEVEL_INFORMATION, 0, 0, 0, nullptr);
        if (er != ERROR_SUCCESS) FF_LOG_WARN("EnableTraceEx2 failed for a provider: {}", er);
    }

    EVENT_TRACE_LOGFILEW log{};
    log.LoggerName = const_cast<LPWSTR>(kSessionName);
    log.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    log.EventRecordCallback = &Impl::RecordThunk;
    log.Context = impl_;
    impl_->consumer = ::OpenTraceW(&log);
    if (impl_->consumer == INVALID_PROCESSTRACE_HANDLE) {
        FF_LOG_WARN("OpenTrace failed: {}", ::GetLastError());
        ::ControlTraceW(impl_->session, kSessionName, props, EVENT_TRACE_CONTROL_STOP);
        impl_->session = 0;
        return StartResult::Failed;
    }

    impl_->running = true;
    impl_->worker = std::thread([this] {
        // ProcessTrace blocks until the session is stopped from Stop().
        const ULONG er = ::ProcessTrace(&impl_->consumer, 1, nullptr, nullptr);
        if (er != ERROR_SUCCESS && er != ERROR_CANCELLED)
            FF_LOG_WARN("ProcessTrace ended with {}", er);
    });
    FF_LOG_INFO("ETW present monitor started");
    return StartResult::Ok;
}

void PresentMonitor::Stop()
{
    if (!impl_ || !impl_->running) return;
    impl_->running = false;

    auto* props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(impl_->propsBuffer.data());
    ::ControlTraceW(impl_->session, kSessionName, props, EVENT_TRACE_CONTROL_STOP);
    if (impl_->consumer != INVALID_PROCESSTRACE_HANDLE) {
        ::CloseTrace(impl_->consumer); // unblocks ProcessTrace
        impl_->consumer = INVALID_PROCESSTRACE_HANDLE;
    }
    if (impl_->worker.joinable()) impl_->worker.join();
    impl_->session = 0;
}

std::size_t PresentMonitor::Drain(std::uint32_t pid, std::vector<double>& outMs)
{
    if (!impl_) return 0;
    std::scoped_lock lock(impl_->mutex);
    if (pid != impl_->bufferedPid || impl_->frametimes.empty()) return 0;
    const std::size_t n = impl_->frametimes.size();
    outMs.insert(outMs.end(), impl_->frametimes.begin(), impl_->frametimes.end());
    impl_->frametimes.clear();
    return n;
}

} // namespace ff::etw
