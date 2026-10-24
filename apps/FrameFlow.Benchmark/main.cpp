// FrameFlow.Benchmark — measures the real accuracy and CPU cost of ff::timing::FramePacer.
//
// Every number in the report is measured on this machine; nothing is estimated.
//
//   FrameFlow.Benchmark.exe                         all FPS targets, Balanced, 10 s each
//   FrameFlow.Benchmark.exe --modes all --seconds 5 --baseline --out report.md
//   Options: --fps 30,60,120  --modes Efficiency,Balanced,Precision|all  --work 0.5  --quick

#include "Core/Log.h"
#include "Core/Strings.h"
#include "Telemetry/FrameStatistics.h"
#include "Timing/Clock.h"
#include "Timing/FramePacer.h"
#include "Timing/NaiveSleepLimiter.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace ff;

namespace {

struct Config {
    std::vector<double> fps = {30, 60, 120, 144, 165, 240};
    std::vector<std::string> modes = {"Balanced"};
    double seconds = 10.0;
    double workFraction = 0.0;   // simulated render work per frame, as a fraction of the period (random 0..f)
    bool baseline = false;
    std::string out;
};

struct Result {
    std::string label;
    double targetFps = 0;
    double targetMs = 0;
    std::size_t frames = 0;
    double avgMs = 0, medianMs = 0, stddevMs = 0;
    double p95Dev = 0, p99Dev = 0, maxDev = 0;
    double avgFps = 0;
    double releaseLateP99Us = 0, releaseLateMaxUs = 0;  // release time - deadline (pacer only)
    std::size_t lateFrames = 0;
    double cpuPercentOneCore = 0;   // whole loop, cycle-based (includes simulated work)
    double cpuPercentSystem = 0;
    double activeWaitPercent = 0;   // pacer's own awake-waiting time / wall time
    double finalMarginUs = 0;
};

std::vector<std::string> Split(const std::string& s)
{
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) if (!item.empty()) out.push_back(item);
    return out;
}

Config Parse(int argc, char** argv)
{
    Config c;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--fps") {
            c.fps.clear();
            for (const auto& f : Split(next())) c.fps.push_back(std::stod(f));
        } else if (a == "--modes") {
            const std::string m = next();
            c.modes = (m == "all") ? std::vector<std::string>{"Efficiency", "Balanced", "Precision"} : Split(m);
        } else if (a == "--seconds") {
            c.seconds = std::max(1.0, std::stod(next()));
        } else if (a == "--work") {
            c.workFraction = std::clamp(std::stod(next()), 0.0, 0.9);
        } else if (a == "--baseline") {
            c.baseline = true;
        } else if (a == "--quick") {
            c.seconds = 3.0;
        } else if (a == "--out") {
            c.out = next();
        }
    }
    return c;
}

// CPU cost is measured in thread cycles (QueryThreadCycleTime), which counts actual cycles
// executed. GetProcessTimes is tick-sampled on Windows 10 and badly misreports short bursts.
std::uint64_t ThreadCycles()
{
    ULONG64 c = 0;
    ::QueryThreadCycleTime(::GetCurrentThread(), &c);
    return c;
}

// Cycles per second for this thread, calibrated against QPC with a 100 ms busy loop.
double CyclesPerSecond()
{
    static const double rate = [] {
        const std::uint64_t c0 = ThreadCycles();
        const std::int64_t t0 = timing::Clock::Now();
        const std::int64_t end = t0 + timing::Clock::MsToTicks(100);
        volatile double x = 1.0;
        while (timing::Clock::Now() < end) x = x * 1.0000001 + 0.5;
        const double secs = timing::Clock::TicksToMs(timing::Clock::Now() - t0) / 1000.0;
        return static_cast<double>(ThreadCycles() - c0) / secs;
    }();
    return rate;
}

void SimulatedWork(std::mt19937& rng, double periodMs, double fraction)
{
    if (fraction <= 0.0) return;
    std::uniform_real_distribution<double> d(0.0, fraction * periodMs);
    const std::int64_t end = timing::Clock::Now() + timing::Clock::MsToTicks(d(rng));
    volatile double x = 1.0;
    while (timing::Clock::Now() < end) x = x * 1.0000001 + 0.5;
}

Result Summarize(std::string label, double fps, const std::vector<std::int64_t>& releases,
                 const std::vector<double>& lateUs, std::size_t lateFrames, double cpuSeconds, double wallSeconds)
{
    Result r;
    r.label = std::move(label);
    r.targetFps = fps;
    r.targetMs = 1000.0 / fps;
    std::vector<double> intervals;
    intervals.reserve(releases.size());
    for (std::size_t i = 1; i < releases.size(); ++i) intervals.push_back(timing::Clock::TicksToMs(releases[i] - releases[i - 1]));
    const auto s = telemetry::ComputeStats(intervals);
    r.frames = intervals.size();
    r.avgMs = s.avgFrametimeMs;
    r.stddevMs = s.stddevMs;
    r.avgFps = s.averageFps;
    r.medianMs = telemetry::Percentile(intervals, 50);
    std::vector<double> dev;
    dev.reserve(intervals.size());
    for (double v : intervals) dev.push_back(std::fabs(v - r.targetMs));
    r.p95Dev = telemetry::Percentile(dev, 95);
    r.p99Dev = telemetry::Percentile(dev, 99);
    r.maxDev = dev.empty() ? 0 : *std::max_element(dev.begin(), dev.end());
    if (!lateUs.empty()) {
        r.releaseLateP99Us = telemetry::Percentile(lateUs, 99);
        r.releaseLateMaxUs = *std::max_element(lateUs.begin(), lateUs.end());
    }
    r.lateFrames = lateFrames;
    SYSTEM_INFO si;
    ::GetSystemInfo(&si);
    r.cpuPercentOneCore = wallSeconds > 0 ? 100.0 * cpuSeconds / wallSeconds : 0;
    r.cpuPercentSystem = r.cpuPercentOneCore / std::max<DWORD>(1, si.dwNumberOfProcessors);
    return r;
}

Result RunPacer(double fps, timing::PacingMode mode, const Config& cfg)
{
    timing::FramePacer pacer(fps, mode);
    std::mt19937 rng(42);
    const double periodMs = 1000.0 / fps;

    // Warm-up: let the adaptive timer margin converge, not measured.
    const std::int64_t warmEnd = timing::Clock::Now() + timing::Clock::MsToTicks(750);
    while (timing::Clock::Now() < warmEnd) {
        SimulatedWork(rng, periodMs, cfg.workFraction);
        pacer.WaitForNextFrame();
    }

    const auto frames = static_cast<std::size_t>(cfg.seconds * fps);
    std::vector<std::int64_t> releases;
    std::vector<double> lateUs;
    releases.reserve(frames + 1);
    lateUs.reserve(frames + 1);
    std::size_t lateFrames = 0;

    CyclesPerSecond();
    const std::int64_t active0 = pacer.ActiveWaitTicks();
    const std::uint64_t cpu0 = ThreadCycles();
    const std::int64_t wall0 = timing::Clock::Now();
    for (std::size_t i = 0; i <= frames; ++i) {
        SimulatedWork(rng, periodMs, cfg.workFraction);
        const auto r = pacer.WaitForNextFrame();
        releases.push_back(r.releaseTicks);
        lateUs.push_back(timing::Clock::TicksToUs(r.releaseTicks - r.deadlineTicks));
        if (r.late || r.rebased) ++lateFrames;
    }
    const double wall = timing::Clock::TicksToMs(timing::Clock::Now() - wall0) / 1000.0;
    const double cpu = static_cast<double>(ThreadCycles() - cpu0) / CyclesPerSecond();

    auto res = Summarize(std::string("FramePacer/") + std::string(timing::ToString(mode)), fps, releases, lateUs,
                         lateFrames, cpu, wall);
    res.finalMarginUs = pacer.AdaptiveMarginUs();
    res.activeWaitPercent = 100.0 * timing::Clock::TicksToMs(pacer.ActiveWaitTicks() - active0) / 1000.0 / wall;
    return res;
}

Result RunNaive(double fps, const Config& cfg)
{
    timing::NaiveSleepLimiter lim(fps);
    std::mt19937 rng(42);
    const double periodMs = 1000.0 / fps;
    const auto frames = static_cast<std::size_t>(std::min(cfg.seconds, 5.0) * fps);
    std::vector<std::int64_t> releases;
    releases.reserve(frames + 1);
    for (int i = 0; i < 10; ++i) lim.WaitForNextFrame();
    CyclesPerSecond();
    const std::uint64_t cpu0 = ThreadCycles();
    const std::int64_t wall0 = timing::Clock::Now();
    for (std::size_t i = 0; i <= frames; ++i) {
        SimulatedWork(rng, periodMs, cfg.workFraction);
        releases.push_back(lim.WaitForNextFrame());
    }
    const double wall = timing::Clock::TicksToMs(timing::Clock::Now() - wall0) / 1000.0;
    const double cpu = static_cast<double>(ThreadCycles() - cpu0) / CyclesPerSecond();
    return Summarize("Naive Sleep() baseline", fps, releases, {}, 0, cpu, wall);
}

std::string FormatBlock(const Result& r)
{
    char buf[1024];
    std::snprintf(buf, sizeof(buf),
                  "Target FPS: %g  [%s]\n"
                  "Target frametime: %.4f ms\n"
                  "Frames measured: %zu\n"
                  "Average: %.4f ms (%.3f FPS)\n"
                  "Median: %.4f ms\n"
                  "Std deviation: %.4f ms\n"
                  "P95 deviation: %.4f ms\n"
                  "P99 deviation: %.4f ms\n"
                  "Max deviation: %.4f ms\n"
                  "Release lateness vs deadline: P99 %.1f us, max %.1f us\n"
                  "Late/rebased frames: %zu\n"
                  "CPU overhead: %.2f%% of one core (%.3f%% of total CPU); pacer awake-wait %.2f%%\n"
                  "Adaptive timer margin at end: %.0f us\n",
                  r.targetFps, r.label.c_str(), r.targetMs, r.frames, r.avgMs, r.avgFps, r.medianMs, r.stddevMs,
                  r.p95Dev, r.p99Dev, r.maxDev, r.releaseLateP99Us, r.releaseLateMaxUs, r.lateFrames,
                  r.cpuPercentOneCore, r.cpuPercentSystem, r.activeWaitPercent, r.finalMarginUs);
    return buf;
}

} // namespace

int main(int argc, char** argv)
{
    core::Log::Initialize(L"benchmark", core::LogLevel::Warning, false);
    const Config cfg = Parse(argc, argv);

    SYSTEM_INFO si;
    ::GetSystemInfo(&si);
    timing::FramePacer probe;
    std::printf("FrameFlow.Benchmark %s\n", FF_VERSION_STRING);
    std::printf("QPC frequency: %lld Hz | logical CPUs: %lu | high-resolution waitable timer: %s\n",
                static_cast<long long>(timing::Clock::Frequency()), si.dwNumberOfProcessors,
                probe.UsingHighResolutionTimer() ? "yes" : "no (timeBeginPeriod fallback)");
    std::printf("Duration per case: %.1f s | simulated work: 0..%.0f%% of frame\n\n", cfg.seconds, cfg.workFraction * 100);
    std::fflush(stdout);

    std::vector<Result> results;
    for (const auto& modeName : cfg.modes) {
        timing::PacingMode mode;
        if (!timing::ParsePacingMode(modeName, mode)) {
            std::fprintf(stderr, "Unknown mode %s\n", modeName.c_str());
            return 2;
        }
        for (double fps : cfg.fps) {
            auto r = RunPacer(fps, mode, cfg);
            std::printf("%s\n", FormatBlock(r).c_str());
            std::fflush(stdout);
            results.push_back(std::move(r));
        }
    }
    if (cfg.baseline) {
        for (double fps : {60.0, 144.0}) {
            auto r = RunNaive(fps, cfg);
            std::printf("%s\n", FormatBlock(r).c_str());
            std::fflush(stdout);
            results.push_back(std::move(r));
        }
    }

    // Markdown summary table.
    std::ostringstream md;
    md << "# FrameFlow timing benchmark\n\n";
    md << "Measured by `FrameFlow.Benchmark.exe` on this machine. QPC " << timing::Clock::Frequency() << " Hz, "
       << si.dwNumberOfProcessors << " logical CPUs, " << cfg.seconds << " s per case, simulated work 0.."
       << cfg.workFraction * 100 << "% of the frame.\n\n";
    md << "| Limiter | Target FPS | Avg ms | Avg FPS | Stddev ms | P95 dev ms | P99 dev ms | Max dev ms | Late | CPU (1 core) | Awake-wait |\n";
    md << "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";
    char line[512];
    for (const auto& r : results) {
        std::snprintf(line, sizeof(line), "| %s | %g | %.4f | %.3f | %.4f | %.4f | %.4f | %.4f | %zu | %.2f%% | %.2f%% |\n",
                      r.label.c_str(), r.targetFps, r.avgMs, r.avgFps, r.stddevMs, r.p95Dev, r.p99Dev, r.maxDev,
                      r.lateFrames, r.cpuPercentOneCore, r.activeWaitPercent);
        md << line;
    }
    std::printf("%s\n", md.str().c_str());
    if (!cfg.out.empty()) {
        std::ofstream o(cfg.out, std::ios::trunc);
        o << md.str() << "\n## Detail\n\n```\n";
        for (const auto& r : results) o << FormatBlock(r) << "\n";
        o << "```\n";
    }
    return 0;
}
