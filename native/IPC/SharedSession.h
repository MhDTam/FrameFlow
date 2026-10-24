#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ff::ipc {

// Shared-memory channel between the engine and the paced process.
//
// The paced process (FrameFlow.TestRenderer today, FrameFlow.Hook64.dll inside a game later)
// creates the section "Local\FrameFlow.Session.<pid>". The engine opens it once it has decided that
// process is the current game. Control flows engine -> client through `control`; frame timestamps
// flow client -> engine through a single-producer/single-consumer ring.
//
// Everything in the block is either immutable after creation or a lock-free std::atomic, so a crash
// on either side can never leave the other side blocked.

constexpr std::uint32_t kSessionMagic = 0x31534646; // "FFS1"
constexpr std::uint32_t kSessionVersion = 5;
constexpr std::uint32_t kRingCapacity = 16384;      // power of two; ~68 s at 240 FPS
static_assert((kRingCapacity & (kRingCapacity - 1)) == 0);

struct SessionControl {                   // written by engine
    std::atomic<std::uint32_t> generation;  // bumped after every change so the client re-reads cheaply
    std::atomic<std::int32_t> pacingEnabled;
    std::atomic<std::int64_t> targetFpsMilli; // FPS * 1000, supports 59.94 etc.
    std::atomic<std::int32_t> mode;           // ff::timing::PacingMode
    std::atomic<std::int32_t> engineAttached;
    std::atomic<std::int64_t> engineHeartbeatQpc;
};

struct SessionClientInfo {                // written by client
    std::uint32_t pid;
    char graphicsApi[16];
    std::int64_t qpcFrequency;
    std::atomic<std::int64_t> heartbeatQpc;
    std::atomic<std::int32_t> pacingActive;     // what the client is actually applying right now
    std::atomic<std::int64_t> appliedFpsMilli;
    std::atomic<std::uint64_t> presentCount;
    // Ring index of the first frame whose interval belongs to the current configuration. The frame
    // that straddles a configuration change is excluded, so telemetry never mixes two settings.
    std::atomic<std::uint64_t> configStartIndex;
};

struct SessionRing {
    std::atomic<std::uint64_t> writeIndex;  // total number of timestamps ever written
    std::int64_t previousAfterPresentQpc[kRingCapacity]; // completion of the preceding Present call
    std::int64_t hookEntryQpc[kRingCapacity]; // entry into the Present hook, before hook bookkeeping
    std::int64_t preWaitQpc[kRingCapacity]; // entry to FrameFlow's BeforePresent, before its wait
    std::int64_t presentQpc[kRingCapacity];
};

struct SessionBlock {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t size;
    std::uint32_t reserved;
    SessionControl control;
    SessionClientInfo client;
    SessionRing ring;
};

static_assert(std::atomic<std::int64_t>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

std::wstring SessionName(std::uint32_t pid);

class SharedSession {
public:
    ~SharedSession();
    SharedSession(const SharedSession&) = delete;
    SharedSession& operator=(const SharedSession&) = delete;

    // Client side: create and initialize the block for the current process.
    static std::unique_ptr<SharedSession> CreateForCurrentProcess(const char* graphicsApi);
    // Engine side: open an existing block. Returns nullptr if absent or incompatible.
    static std::unique_ptr<SharedSession> Open(std::uint32_t pid);

    SessionBlock& Block() { return *block_; }
    const SessionBlock& Block() const { return *block_; }

    // Client: publish one present timestamp (wait-free).
    void PushPresent(std::int64_t qpc, std::int64_t preWaitQpc = 0,
                     std::int64_t previousAfterPresentQpc = 0,
                     std::int64_t hookEntryQpc = 0);

private:
    SharedSession(void* mapping, SessionBlock* block) : mapping_(mapping), block_(block) {}
    void* mapping_ = nullptr;
    SessionBlock* block_ = nullptr;
};

// Engine-side incremental reader for the ring. Converts timestamps into frame times (ms).
class RingReader {
public:
    // Appends new frame times to `out`. Returns the number appended. Handles overruns by
    // skipping to the oldest still-valid entry (a gap in telemetry, never corrupted data).
    // Intervals ending before `minIndex` are skipped (see SessionClientInfo::configStartIndex).
    std::size_t ReadNew(const SessionBlock& block, std::vector<double>& out, std::uint64_t minIndex = 0);
    void Reset() { next_ = 0; lastQpc_ = 0; }
    // Ignore everything already in the ring (e.g. frames rendered before FrameFlow attached).
    void SkipToEnd(const SessionBlock& block)
    {
        next_ = block.ring.writeIndex.load(std::memory_order_acquire);
        lastQpc_ = 0;
    }

private:
    std::uint64_t next_ = 0;
    std::int64_t lastQpc_ = 0;
};

} // namespace ff::ipc
