#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ff::process {

// Conservative anti-cheat detection. FrameFlow never tries to evade anti-cheat: if any known
// protection is present for a process, injection-based integration is disabled for it.
struct AntiCheatHit {
    std::string product;   // "Easy Anti-Cheat", "BattlEye", ...
    std::string evidence;  // what matched (file, module or process name)
};

// Pure check on already-collected evidence (unit-tested).
//  exeName          - the game's executable file name
//  moduleNames      - lower-case module file names loaded in the game process
//  runningProcesses - lower-case executable names of all processes on the system
//  nearbyFiles      - lower-case file/dir names found next to the exe and up to two parent dirs
std::optional<AntiCheatHit> DetectAntiCheat(std::string_view exeName,
                                            const std::vector<std::string>& moduleNames,
                                            const std::vector<std::string>& runningProcesses,
                                            const std::vector<std::string>& nearbyFiles);

// Collects `nearbyFiles` for an executable path (bounded directory listing, no recursion).
std::vector<std::string> CollectNearbyFiles(const std::wstring& exePath);

} // namespace ff::process
