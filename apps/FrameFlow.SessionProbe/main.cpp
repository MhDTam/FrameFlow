// Read-only live diagnostics for a FrameFlow shared session.
#include "IPC/SharedSession.h"
#include "Telemetry/FrameStatistics.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "Usage: FrameFlow.SessionProbe.exe <pid> [seconds]\n";
        return 2;
    }
    const auto pid = static_cast<std::uint32_t>(std::strtoul(argv[1], nullptr, 10));
    const int seconds = argc > 2 ? std::clamp(std::atoi(argv[2]), 1, 60) : 10;
    auto session = ff::ipc::SharedSession::Open(pid);
    if (!session) {
        std::cerr << "No FrameFlow session for PID " << pid << "\n";
        return 3;
    }
    const auto& block = session->Block();
    const auto& ring = block.ring;
    std::uint64_t next = ring.writeIndex.load(std::memory_order_acquire);
    std::int64_t lastRelease = 0;
    std::int64_t firstRelease = 0;
    const double freq = static_cast<double>(block.client.qpcFrequency);
    std::vector<double> intervals;
    std::vector<double> presentBlocks;
    std::vector<double> gameWork;
    std::vector<double> hookOverheads;
    std::vector<double> limiterWaits;
    std::vector<double> elapsedSeconds;
    std::vector<bool> splitValid;
    intervals.reserve(static_cast<std::size_t>(seconds) * 1000);
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    auto collect = [&] {
        const std::uint64_t published = ring.writeIndex.load(std::memory_order_acquire);
        if (published < next) { next = published; lastRelease = 0; }
        if (published - next > ff::ipc::kRingCapacity - 64) {
            next = published - (ff::ipc::kRingCapacity - 64);
            lastRelease = 0;
        }
        for (std::uint64_t i = next; i < published; ++i) {
            const auto slot = i & (ff::ipc::kRingCapacity - 1);
            const auto previousAfterPresent = ring.previousAfterPresentQpc[slot];
            const auto hookEntry = ring.hookEntryQpc[slot];
            const auto entry = ring.preWaitQpc[slot];
            const auto release = ring.presentQpc[slot];
            if (firstRelease == 0) firstRelease = release;
            if (lastRelease > 0 && entry >= lastRelease && release >= entry) {
                intervals.push_back((release - lastRelease) * 1000.0 / freq);
                elapsedSeconds.push_back((release - firstRelease) / freq);
                const bool valid = previousAfterPresent >= lastRelease &&
                    previousAfterPresent <= hookEntry && hookEntry <= entry;
                splitValid.push_back(valid);
                presentBlocks.push_back(valid ? (previousAfterPresent - lastRelease) * 1000.0 / freq : 0.0);
                gameWork.push_back(valid ? (hookEntry - previousAfterPresent) * 1000.0 / freq : 0.0);
                hookOverheads.push_back(valid ? (entry - hookEntry) * 1000.0 / freq : 0.0);
                limiterWaits.push_back((release - entry) * 1000.0 / freq);
            }
            lastRelease = release;
        }
        next = published;
    };
    while (std::chrono::steady_clock::now() < end) {
        collect();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    collect();
    if (intervals.empty()) {
        std::cerr << "No present intervals observed.\n";
        return 4;
    }
    const auto stats = ff::telemetry::ComputeStats(intervals);
    const double targetFps = block.client.appliedFpsMilli.load() / 1000.0;
    const double targetMs = targetFps > 0 ? 1000.0 / targetFps : 0;
    std::vector<double> sorted = intervals;
    std::sort(sorted.begin(), sorted.end());
    const auto percentile = [&](double p) {
        const auto index = std::min(sorted.size() - 1,
            static_cast<std::size_t>(std::ceil(p * sorted.size())) - 1);
        return sorted[index];
    };
    const auto over = [&](double multiplier) {
        return std::count_if(intervals.begin(), intervals.end(), [&](double ms) {
            return targetMs > 0 && ms > multiplier * targetMs;
        });
    };
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "API: " << block.client.graphicsApi << ", active: "
              << block.client.pacingActive.load() << ", target: " << targetFps << " FPS\n";
    std::cout << "Samples: " << intervals.size() << ", average: " << stats.averageFps
              << " FPS, 1% low: " << stats.low1Fps << " FPS\n";
    std::cout << "Frametime ms: median " << percentile(0.5) << ", P95 " << percentile(0.95)
              << ", P99 " << percentile(0.99) << ", max " << sorted.back()
              << ", stddev " << stats.stddevMs << '\n';
    std::cout << "Intervals >1.5x target: " << over(1.5)
              << ", >2x target: " << over(2.0) << '\n';
    std::size_t gameSpikes = 0;
    std::size_t presentSpikes = 0;
    std::size_t hookSpikes = 0;
    std::size_t waitSpikes = 0;
    std::size_t unknownSpikes = 0;
    for (std::size_t i = 0; i < intervals.size(); ++i) {
        if (targetMs > 0 && intervals[i] > 1.5 * targetMs) {
            if (!splitValid[i]) ++unknownSpikes;
            else {
                const double largest = std::max({gameWork[i], presentBlocks[i], hookOverheads[i], limiterWaits[i]});
                if (largest == limiterWaits[i]) ++waitSpikes;
                else if (largest == hookOverheads[i]) ++hookSpikes;
                else if (largest == presentBlocks[i]) ++presentSpikes;
                else ++gameSpikes;
            }
        }
    }
    std::cout << "Long intervals dominated by game work: " << gameSpikes
              << ", previous DXGI Present: " << presentSpikes
              << ", hook overhead: " << hookSpikes
              << ", FrameFlow wait: " << waitSpikes
              << ", unclassified: " << unknownSpikes << '\n';
    std::cout << "Max game work: " << *std::max_element(gameWork.begin(), gameWork.end())
              << " ms, max DXGI Present: " << *std::max_element(presentBlocks.begin(), presentBlocks.end())
              << " ms, max hook overhead: " << *std::max_element(hookOverheads.begin(), hookOverheads.end())
              << " ms, max FrameFlow wait: " << *std::max_element(limiterWaits.begin(), limiterWaits.end()) << " ms\n";
    std::vector<std::size_t> largest(intervals.size());
    for (std::size_t i = 0; i < largest.size(); ++i) largest[i] = i;
    const auto count = std::min<std::size_t>(10, largest.size());
    std::partial_sort(largest.begin(), largest.begin() + count, largest.end(),
        [&](std::size_t a, std::size_t b) { return intervals[a] > intervals[b]; });
    std::cout << "Largest intervals (seconds from start: total / game / DXGI Present / hook / FrameFlow wait, ms):\n";
    for (std::size_t n = 0; n < count; ++n) {
        const auto i = largest[n];
        std::cout << "  " << elapsedSeconds[i] << "s: " << intervals[i] << " / "
                  << gameWork[i] << " / " << presentBlocks[i] << " / "
                  << hookOverheads[i] << " / "
                  << limiterWaits[i] << (splitValid[i] ? "" : " (split unavailable)") << '\n';
    }
    return 0;
}
