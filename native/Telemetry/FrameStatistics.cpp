#include "Telemetry/FrameStatistics.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>

namespace ff::telemetry {

namespace {

double MeanOfSlowest(std::vector<double>& sortedDesc, double fraction)
{
    const std::size_t n = sortedDesc.size();
    const std::size_t k = std::max<std::size_t>(1, static_cast<std::size_t>(std::floor(static_cast<double>(n) * fraction)));
    double sum = 0.0;
    for (std::size_t i = 0; i < k; ++i) sum += sortedDesc[i];
    return sum / static_cast<double>(k);
}

} // namespace

FrameStats ComputeStats(std::span<const double> ft, double currentWindowMs)
{
    FrameStats s;
    s.frames = ft.size();
    if (ft.empty()) return s;

    const double total = std::accumulate(ft.begin(), ft.end(), 0.0);
    s.windowSeconds = total / 1000.0;
    s.lastFrametimeMs = ft.back();
    s.avgFrametimeMs = total / static_cast<double>(ft.size());
    s.averageFps = total > 0.0 ? 1000.0 * static_cast<double>(ft.size()) / total : 0.0;

    const auto [mn, mx] = std::minmax_element(ft.begin(), ft.end());
    s.minFrametimeMs = *mn;
    s.maxFrametimeMs = *mx;

    double sq = 0.0;
    for (double v : ft) sq += (v - s.avgFrametimeMs) * (v - s.avgFrametimeMs);
    s.stddevMs = std::sqrt(sq / static_cast<double>(ft.size()));

    // Current FPS over the most recent window.
    double recent = 0.0;
    std::size_t recentFrames = 0;
    for (auto it = ft.rbegin(); it != ft.rend(); ++it) {
        recent += *it;
        ++recentFrames;
        if (recent >= currentWindowMs) break;
    }
    s.currentFps = recent > 0.0 ? 1000.0 * static_cast<double>(recentFrames) / recent : 0.0;

    std::vector<double> sorted(ft.begin(), ft.end());
    std::sort(sorted.begin(), sorted.end(), std::greater<>());
    const double slow1 = MeanOfSlowest(sorted, 0.01);
    const double slow01 = MeanOfSlowest(sorted, 0.001);
    s.low1Fps = slow1 > 0.0 ? 1000.0 / slow1 : 0.0;
    s.low01Fps = slow01 > 0.0 ? 1000.0 / slow01 : 0.0;
    return s;
}

GraphSeries BuildGraph(std::span<const double> ft, double historySeconds, std::size_t buckets)
{
    GraphSeries g;
    if (buckets == 0) return g;
    g.meanMs.assign(buckets, 0.0);
    g.maxMs.assign(buckets, 0.0);
    if (ft.empty() || historySeconds <= 0.0) return g;

    const double historyMs = historySeconds * 1000.0;
    const double bucketMs = historyMs / static_cast<double>(buckets);
    std::vector<double> sum(buckets, 0.0);
    std::vector<std::size_t> count(buckets, 0);

    // Walk backwards from "now" (end of the last frame). A frame belongs to the bucket in which it ended.
    double age = 0.0;
    for (auto it = ft.rbegin(); it != ft.rend(); ++it) {
        if (age >= historyMs) break;
        const auto fromEnd = static_cast<std::size_t>(age / bucketMs);
        const std::size_t idx = buckets - 1 - std::min(fromEnd, buckets - 1);
        sum[idx] += *it;
        ++count[idx];
        g.maxMs[idx] = std::max(g.maxMs[idx], *it);
        age += *it;
    }

    double carry = 0.0;
    for (std::size_t i = 0; i < buckets; ++i) {
        if (count[i] > 0) {
            g.meanMs[i] = sum[i] / static_cast<double>(count[i]);
            carry = g.meanMs[i];
        } else {
            g.meanMs[i] = carry;
            g.maxMs[i] = carry;
        }
    }
    return g;
}

double Percentile(std::span<const double> values, double p)
{
    if (values.empty()) return 0.0;
    std::vector<double> v(values.begin(), values.end());
    std::sort(v.begin(), v.end());
    const double rank = std::clamp(p, 0.0, 100.0) / 100.0 * static_cast<double>(v.size());
    std::size_t idx = static_cast<std::size_t>(std::ceil(rank));
    if (idx == 0) idx = 1;
    return v[std::min(idx, v.size()) - 1];
}

} // namespace ff::telemetry
