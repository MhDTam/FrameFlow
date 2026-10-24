#pragma once
#include "Core/Json.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ff::engine {

// Engine-owned settings persisted in %LOCALAPPDATA%\FrameFlow\engine-settings.json.
// (UI-only preferences such as tray behaviour live in the UI's own settings file.)
struct EngineSettings {
    double defaultFps = 60.0;
    std::string defaultMode = "Balanced";
    bool autoDetect = true;
    bool miniOverlay = false;
    double telemetrySeconds = 30.0;              // graph + statistics window (10–60)
    std::vector<std::string> extraExclusions;    // lower-case exe names, in addition to built-ins
    std::vector<std::string> manualProfileIds;   // games the user picked explicitly
    std::string logLevel = "INFO";

    core::Json ToJson() const;
    static EngineSettings FromJson(const core::Json& j);

    static EngineSettings Load(const std::filesystem::path& file);
    bool Save(const std::filesystem::path& file) const;
};

} // namespace ff::engine
