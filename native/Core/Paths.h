#pragma once
#include <filesystem>

namespace ff::core {

// True if running in portable mode (detected via .portable/portable marker, data/ folder,
// or the FRAMEFLOW_PORTABLE environment variable).
bool IsPortableMode();

// Returns the root directory for settings, profiles, and logs:
// - %FRAMEFLOW_DATA_DIR% if set
// - <ModuleDir>\data or <ExecutableDir>\data if portable mode is active
// - %LOCALAPPDATA%\FrameFlow otherwise
std::filesystem::path AppDataRoot();
std::filesystem::path ProfilesDir();
std::filesystem::path LogsDir();
std::filesystem::path SettingsFile();

// Directory containing the running executable.
std::filesystem::path ExecutableDir();

// Directory containing the current module (.dll or .exe).
std::filesystem::path ModuleDir();

} // namespace ff::core
