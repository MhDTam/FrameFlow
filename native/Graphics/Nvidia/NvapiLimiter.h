#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace ff::nvidia {

// Applies an FPS limit through the NVIDIA driver's own frame-rate limiter (the "Max Frame Rate"
// setting, FRL_FPS in the Driver Settings Repository) for a specific game executable.
//
// This is the driver limiter NVIDIA Control Panel / NVIDIA App expose. FrameFlow writes it into a
// per-application DRS profile, exactly like NVIDIA Profile Inspector. Nothing is injected into the
// game and no anti-cheat is touched, so it is safe for any title — but the driver, not FrameFlow,
// does the pacing, so it is less tight than the in-process FramePacer.
//
// Writes are reversible: the previous FRL value of a profile is remembered and restored when the
// limit is cleared (game exit, pacing off, or engine shutdown). Runs as a normal user; DRS writes
// go to the current user's driver settings and need no elevation.
//
// Availability depends on an NVIDIA driver being present. On non-NVIDIA machines every call is a
// no-op that reports Unavailable, and callers fall back to "no driver limiter".

enum class NvapiStatus : std::uint8_t {
    Ok,
    Unavailable,   // no NVIDIA driver / NvAPI not present
    NotBuilt,      // this build has no NVAPI support compiled in
    Failed,        // an NvAPI call failed (details in the log)
};

struct NvapiInfo {
    bool available = false;
    std::string gpuName;      // e.g. "NVIDIA GeForce GTX 1080"
    std::string driverVersion;
};

class NvapiLimiter {
public:
    NvapiLimiter();
    ~NvapiLimiter();
    NvapiLimiter(const NvapiLimiter&) = delete;
    NvapiLimiter& operator=(const NvapiLimiter&) = delete;

    bool Available() const { return info_.available; }
    const NvapiInfo& Info() const { return info_; }

    // Sets the driver frame-rate limit for `exePath` to `fps` (rounded; 0 clears). The exe's own DRS
    // profile is used when the driver already has one, otherwise a FrameFlow profile is created for it.
    // Remembers the profile's previous FRL value the first time FrameFlow touches it.
    NvapiStatus SetGameLimit(const std::wstring& exePath, std::uint32_t fps);

    // Restores the driver limit for `exePath` to what it was before FrameFlow changed it.
    NvapiStatus ClearGameLimit(const std::wstring& exePath);

    // Restores every profile FrameFlow changed this session. Called on shutdown.
    void RestoreAll();

private:
    struct Impl;
    Impl* impl_ = nullptr;
    NvapiInfo info_;
};

} // namespace ff::nvidia
