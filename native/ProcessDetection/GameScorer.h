#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ff::process {

// Everything we know about one candidate process. Filled by ProcessScanner, consumed by the
// pure scoring function below (kept pure so it can be unit-tested without real processes).
struct ProcessSignals {
    std::uint32_t pid = 0;
    std::string exeName;       // UTF-8, e.g. "Cyberpunk2077.exe"
    std::string exePath;       // UTF-8 full path; empty if inaccessible
    std::string windowTitle;
    std::string parentExeName;
    bool accessible = true;    // false when OpenProcess was denied (usually an elevated process)
    bool hasVisibleWindow = false;
    bool isForeground = false;
    bool coversMonitor = false; // exclusive fullscreen or borderless-fullscreen
    bool usesDxgi = false;
    bool usesD3D11 = false;
    bool usesD3D12 = false;
    bool usesD3D9 = false;
    bool usesVulkan = false;
    bool usesOpenGL = false;
    bool hasFrameFlowSession = false; // process already speaks our shared-memory protocol
};

struct GameScore {
    int score = 0;
    bool excluded = false;
    std::vector<std::string> reasons; // human-readable, shown in Advanced/debug logs
};

constexpr int kGameScoreThreshold = 60;

class ExclusionList {
public:
    ExclusionList();                           // built-in defaults
    void Add(std::string_view exeName);
    void Remove(std::string_view exeName);
    bool Contains(std::string_view exeName) const;
    const std::vector<std::string>& Entries() const { return entries_; } // lower-case
    static const std::vector<std::string>& Defaults();

private:
    std::vector<std::string> entries_;
};

// Returns the primary graphics API name inferred from loaded modules ("DX12", "DX11", ...).
std::string_view InferGraphicsApi(const ProcessSignals& s);

GameScore ScoreProcess(const ProcessSignals& s, const ExclusionList& exclusions);

} // namespace ff::process
