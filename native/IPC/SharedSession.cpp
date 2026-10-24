#include "IPC/SharedSession.h"
#include "Core/Log.h"

#include <windows.h>

#include <cstring>
#include <new>

namespace ff::ipc {

std::wstring SessionName(std::uint32_t pid)
{
    return L"Local\\FrameFlow.Session." + std::to_wstring(pid);
}

SharedSession::~SharedSession()
{
    if (block_) ::UnmapViewOfFile(block_);
    if (mapping_) ::CloseHandle(mapping_);
}

std::unique_ptr<SharedSession> SharedSession::CreateForCurrentProcess(const char* graphicsApi)
{
    const DWORD pid = ::GetCurrentProcessId();
    const auto name = SessionName(pid);
    constexpr DWORD size = sizeof(SessionBlock);

    HANDLE mapping = ::CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, size, name.c_str());
    if (!mapping) {
        FF_LOG_ERROR("CreateFileMapping for session failed: {}", ::GetLastError());
        return nullptr;
    }
    void* view = ::MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (!view) {
        ::CloseHandle(mapping);
        return nullptr;
    }

    // Fresh pagefile-backed sections are zero-filled; placement-new gives the atomics a defined state.
    auto* block = new (view) SessionBlock{};
    block->version = kSessionVersion;
    block->size = size;
    block->client.pid = pid;
    strncpy_s(block->client.graphicsApi, graphicsApi ? graphicsApi : "unknown", _TRUNCATE);
    LARGE_INTEGER f;
    ::QueryPerformanceFrequency(&f);
    block->client.qpcFrequency = f.QuadPart;
    block->control.targetFpsMilli.store(60000);
    block->control.mode.store(1);
    // Magic last: the engine treats the block as valid only once this is visible.
    std::atomic_thread_fence(std::memory_order_release);
    block->magic = kSessionMagic;

    return std::unique_ptr<SharedSession>(new SharedSession(mapping, block));
}

std::unique_ptr<SharedSession> SharedSession::Open(std::uint32_t pid)
{
    const auto name = SessionName(pid);
    HANDLE mapping = ::OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    if (!mapping) return nullptr;
    void* view = ::MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SessionBlock));
    if (!view) {
        ::CloseHandle(mapping);
        return nullptr;
    }
    auto* block = static_cast<SessionBlock*>(view);
    std::atomic_thread_fence(std::memory_order_acquire);
    if (block->magic != kSessionMagic || block->version != kSessionVersion || block->size != sizeof(SessionBlock) ||
        block->client.pid != pid) {
        FF_LOG_WARN("Session block for pid {} is incompatible (magic={:#x} version={})", pid, block->magic, block->version);
        ::UnmapViewOfFile(view);
        ::CloseHandle(mapping);
        return nullptr;
    }
    return std::unique_ptr<SharedSession>(new SharedSession(mapping, block));
}

void SharedSession::PushPresent(std::int64_t qpc, std::int64_t preWaitQpc,
                                std::int64_t previousAfterPresentQpc,
                                std::int64_t hookEntryQpc)
{
    auto& ring = block_->ring;
    const std::uint64_t idx = ring.writeIndex.load(std::memory_order_relaxed);
    ring.previousAfterPresentQpc[idx & (kRingCapacity - 1)] = previousAfterPresentQpc;
    ring.hookEntryQpc[idx & (kRingCapacity - 1)] = hookEntryQpc;
    ring.preWaitQpc[idx & (kRingCapacity - 1)] = preWaitQpc;
    ring.presentQpc[idx & (kRingCapacity - 1)] = qpc;
    ring.writeIndex.store(idx + 1, std::memory_order_release);
    block_->client.presentCount.store(idx + 1, std::memory_order_relaxed);
}

std::size_t RingReader::ReadNew(const SessionBlock& block, std::vector<double>& out, std::uint64_t minIndex)
{
    const auto& ring = block.ring;
    const std::uint64_t end = ring.writeIndex.load(std::memory_order_acquire);
    if (end < next_) {           // client restarted its ring
        next_ = 0;
        lastQpc_ = 0;
    }
    if (end == next_) return 0;

    std::uint64_t start = next_;
    // Leave a safety margin of 64 slots the producer might be overwriting while we copy.
    if (end - start > kRingCapacity - 64) {
        start = end - (kRingCapacity - 64);
        lastQpc_ = 0;
    }

    const double freq = static_cast<double>(block.client.qpcFrequency > 0 ? block.client.qpcFrequency : 10'000'000);
    std::size_t added = 0;
    for (std::uint64_t i = start; i < end; ++i) {
        const std::int64_t t = ring.presentQpc[i & (kRingCapacity - 1)];
        if (lastQpc_ != 0 && t > lastQpc_ && i >= minIndex) {
            out.push_back(static_cast<double>(t - lastQpc_) * 1000.0 / freq);
            ++added;
        }
        lastQpc_ = t;
    }

    // If the producer lapped us during the copy, the oldest values we read may be torn; drop the batch.
    const std::uint64_t after = ring.writeIndex.load(std::memory_order_acquire);
    if (after - start > kRingCapacity - 64) {
        out.resize(out.size() - added);
        added = 0;
        lastQpc_ = 0;
    }
    next_ = end;
    return added;
}

} // namespace ff::ipc
