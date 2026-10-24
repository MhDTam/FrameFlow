#include "Display/FpsPresets.h"

#include <algorithm>
#include <cmath>

namespace ff::display {

const std::vector<double>& StandardFpsLimits()
{
    static const std::vector<double> limits = {30, 40, 45, 50, 60, 72, 75, 90, 100, 120, 138, 141,
                                               144, 160, 162, 165, 180, 200, 225, 240, 360};
    return limits;
}

std::vector<double> SmartPresets(double hz)
{
    struct Row { double hz; std::vector<double> presets; };
    static const std::vector<Row> table = {
        {60, {30, 40, 60}},
        {120, {40, 60, 90, 120}},
        {144, {60, 90, 120, 138, 141, 144}},
        {165, {60, 90, 120, 141, 160, 162, 165}},
        {240, {120, 180, 225, 237, 240}},
    };
    if (!(hz > 0.0)) return table[0].presets;

    // Exact-ish match first (59.94 counts as 60, 143.98 as 144).
    for (const auto& r : table)
        if (std::fabs(r.hz - hz) < 1.0) return r.presets;

    // Otherwise derive: half, two-thirds, refresh-3 and refresh, plus 60 when it fits.
    std::vector<double> out;
    auto push = [&](double v) {
        v = std::round(v);
        if (v >= 30 && v <= hz + 0.5 && std::find(out.begin(), out.end(), v) == out.end()) out.push_back(v);
    };
    push(60);
    push(hz / 2.0);
    push(hz * 2.0 / 3.0);
    push(hz - 3.0);
    push(hz);
    std::sort(out.begin(), out.end());
    return out;
}

double SuggestedLimit(double hz)
{
    if (!(hz > 0.0)) return 60.0;
    if (hz < 100.0) return std::round(hz);
    return std::round(hz) - 3.0;
}

} // namespace ff::display
