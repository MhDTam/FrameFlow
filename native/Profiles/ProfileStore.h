#pragma once
#include "Core/Json.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ff::profiles {

struct GameProfile {
    std::string profileId;      // stable id derived from the exe path
    std::string game;           // display name
    std::string exe;            // file name, e.g. "Cyberpunk2077.exe"
    std::string exePath;        // full path (may be empty if never accessible)
    double fpsLimit = 60.0;
    bool framePacing = false;
    std::string pacingMode = "Balanced";
    std::string limitMethod = "auto"; // auto (off) | frameflow
    bool startWithGame = false;
    bool allowProtected = false; // explicit per-game risk acknowledgement
    bool manual = false;        // user picked this exe explicitly
    std::int64_t lastDetectedUnix = 0;

    core::Json ToJson() const;
    static std::optional<GameProfile> FromJson(const core::Json& j);
};

// Stable profile id: lower-case exe name plus a short FNV-1a hash of the lower-case full path,
// so two different games that both ship "game.exe" get separate profiles.
std::string MakeProfileId(std::string_view exeName, std::string_view exePath);

// Best-effort display name from the exe's version resource (ProductName / FileDescription),
// falling back to the file name without extension.
std::string DisplayNameForExe(const std::wstring& exePath, std::string_view exeName);

// JSON-file profile store under %LOCALAPPDATA%\FrameFlow\Profiles (one file per game).
// Writes are atomic (temp file + rename) so a crash never leaves a half-written profile.
class ProfileStore {
public:
    explicit ProfileStore(std::filesystem::path dir);

    std::optional<GameProfile> Load(const std::string& profileId) const;
    bool Save(const GameProfile& profile) const;
    std::vector<GameProfile> LoadAll() const;
    const std::filesystem::path& Directory() const { return dir_; }

    // Loads the existing profile for this exe or creates (and saves) a new one from defaults.
    GameProfile LoadOrCreate(std::string_view exeName, std::string_view exePath, double defaultFps,
                             std::string_view defaultMode);

private:
    std::filesystem::path PathFor(const std::string& profileId) const;
    std::filesystem::path dir_;
};

} // namespace ff::profiles
