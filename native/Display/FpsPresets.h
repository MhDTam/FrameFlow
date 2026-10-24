#pragma once
#include <vector>

namespace ff::display {

// Every limit offered in the UI list (custom values 10–1000 are allowed as well).
const std::vector<double>& StandardFpsLimits();

// Refresh-rate-aware suggestions. Only suggestions: FrameFlow never changes a user's limit on its own.
std::vector<double> SmartPresets(double refreshHz);

// A good default suggestion for VRR displays: a few FPS below refresh keeps the game inside the
// VRR window (e.g. 141 @ 144 Hz, 162 @ 165 Hz). Returns refresh itself for low refresh rates.
double SuggestedLimit(double refreshHz);

} // namespace ff::display
