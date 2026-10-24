#pragma once
#include "ProcessDetection/GameScorer.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ff::process {

struct Candidate {
    ProcessSignals signals;
    GameScore score;
    std::vector<std::string> moduleNames; // lower-case
    void* hwnd = nullptr;
};

// Enumerates processes that own a visible top-level window and gathers ProcessSignals for them.
// Module lists are cached per PID and refreshed every few seconds, because EnumProcessModules is
// the expensive part and a game's module set is stable once it renders.
class ProcessScanner {
public:
    std::vector<Candidate> Scan(const ExclusionList& exclusions);

    // Lower-case exe names of every running process, collected during the last Scan().
    const std::vector<std::string>& RunningProcessNames() const { return running_; }

    static bool IsProcessAlive(std::uint32_t pid);

private:
    struct ModuleCache {
        std::vector<std::string> modules;
        std::chrono::steady_clock::time_point refreshed;
        bool accessible = true;
    };
    std::unordered_map<std::uint32_t, ModuleCache> moduleCache_;
    std::vector<std::string> running_;
};

} // namespace ff::process
