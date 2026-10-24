#include "Graphics/Nvidia/NvapiLimiter.h"
#include "Core/Log.h"
#include "Core/Strings.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <map>

#ifdef FF_HAVE_NVAPI
#include "nvapi.h"
#include "NvApiDriverSettings.h"
#endif

namespace ff::nvidia {

namespace {

// NVIDIA's driver limiter accepts 0 (off) through 1023. Below ~20 the driver ignores it, so we treat
// anything under 20 as "off" rather than writing a value the driver will not honour.
[[maybe_unused]] constexpr std::uint32_t kMinDriverFps = 20;
[[maybe_unused]] constexpr std::uint32_t kMaxDriverFps = 1023;

[[maybe_unused]] std::wstring AppNameFor(const std::wstring& exePath)
{
    return std::filesystem::path(exePath).filename().wstring();
}

} // namespace

#ifdef FF_HAVE_NVAPI

namespace {

// FrameFlow's memory of one profile it changed, so the change can be undone exactly.
struct SavedProfile {
    std::wstring appName;       // original-case application name
    std::wstring profileName;
    bool ownedProfile = false;  // FrameFlow created this profile (delete it wholesale on restore)
    bool hadSetting = false;    // FRL_FPS existed before FrameFlow touched it
    std::uint32_t prevValue = 0;
};

void ToUnicode(NvAPI_UnicodeString dst, const std::wstring& src)
{
    const size_t n = std::min<size_t>(src.size(), NVAPI_UNICODE_STRING_MAX - 1);
    for (size_t i = 0; i < n; ++i) dst[i] = static_cast<NvU16>(src[i]);
    dst[n] = 0;
}

} // namespace

struct NvapiLimiter::Impl {
    bool initialized = false;
    std::map<std::wstring, SavedProfile> changed; // keyed by lower-case app name

    // Opens a DRS session with settings loaded. Caller destroys it.
    bool OpenSession(NvDRSSessionHandle& session)
    {
        if (NvAPI_DRS_CreateSession(&session) != NVAPI_OK) return false;
        if (NvAPI_DRS_LoadSettings(session) != NVAPI_OK) {
            NvAPI_DRS_DestroySession(session);
            return false;
        }
        return true;
    }

    // Finds the DRS profile that owns `appName`, or creates a FrameFlow profile + application for it.
    bool ResolveProfile(NvDRSSessionHandle session, const std::wstring& appName, NvDRSProfileHandle& profile,
                        bool& created, std::wstring& profileName)
    {
        created = false;
        NvAPI_UnicodeString app{};
        ToUnicode(app, appName);
        NVDRS_APPLICATION found{};
        found.version = NVDRS_APPLICATION_VER;
        NvAPI_Status s = NvAPI_DRS_FindApplicationByName(session, app, &profile, &found);
        if (s == NVAPI_OK) {
            NVDRS_PROFILE info{};
            info.version = NVDRS_PROFILE_VER;
            if (NvAPI_DRS_GetProfileInfo(session, profile, &info) == NVAPI_OK)
                profileName = core::Utf8ToWide(core::WideToUtf8(reinterpret_cast<wchar_t*>(info.profileName)));
            return true;
        }

        // Not owned by any profile: create one FrameFlow controls.
        const std::wstring name = L"FrameFlow - " + appName;
        NVDRS_PROFILE prof{};
        prof.version = NVDRS_PROFILE_VER;
        ToUnicode(prof.profileName, name);
        if (NvAPI_DRS_CreateProfile(session, &prof, &profile) != NVAPI_OK) return false;

        NVDRS_APPLICATION newApp{};
        newApp.version = NVDRS_APPLICATION_VER;
        newApp.isPredefined = 0;
        ToUnicode(newApp.appName, appName);
        ToUnicode(newApp.userFriendlyName, appName);
        if (NvAPI_DRS_CreateApplication(session, profile, &newApp) != NVAPI_OK) return false;
        created = true;
        profileName = name;
        return true;
    }
};

NvapiLimiter::NvapiLimiter() : impl_(new Impl)
{
    if (NvAPI_Initialize() != NVAPI_OK) {
        FF_LOG_INFO("NvAPI not available; driver frame limiter disabled");
        return;
    }
    impl_->initialized = true;
    info_.available = true;

    NvPhysicalGpuHandle gpus[NVAPI_MAX_PHYSICAL_GPUS]{};
    NvU32 count = 0;
    if (NvAPI_EnumPhysicalGPUs(gpus, &count) == NVAPI_OK && count > 0) {
        NvAPI_ShortString name{};
        if (NvAPI_GPU_GetFullName(gpus[0], name) == NVAPI_OK) info_.gpuName = name;
    }
    NvAPI_ShortString ver{};
    NvU32 driverVersion = 0;
    if (NvAPI_SYS_GetDriverAndBranchVersion(&driverVersion, ver) == NVAPI_OK)
        info_.driverVersion = std::to_string(driverVersion / 100) + "." + std::to_string(driverVersion % 100);
    FF_LOG_INFO("NvAPI ready: {} (driver {})", info_.gpuName.empty() ? "NVIDIA GPU" : info_.gpuName, info_.driverVersion);
}

NvapiLimiter::~NvapiLimiter()
{
    if (impl_ && impl_->initialized) {
        RestoreAll();
        NvAPI_Unload();
    }
    delete impl_;
}

NvapiStatus NvapiLimiter::SetGameLimit(const std::wstring& exePath, std::uint32_t fps)
{
    if (!impl_ || !impl_->initialized) return NvapiStatus::Unavailable;
    const std::wstring appName = AppNameFor(exePath);
    if (appName.empty()) return NvapiStatus::Failed;
    const std::uint32_t value = (fps < kMinDriverFps) ? 0 : std::min(fps, kMaxDriverFps);

    NvDRSSessionHandle session{};
    if (!impl_->OpenSession(session)) return NvapiStatus::Failed;
    struct SessionGuard {
        NvDRSSessionHandle s;
        ~SessionGuard() { NvAPI_DRS_DestroySession(s); }
    } guard{session};

    NvDRSProfileHandle profile{};
    bool created = false;
    std::wstring profileName;
    if (!impl_->ResolveProfile(session, appName, profile, created, profileName)) return NvapiStatus::Failed;

    const std::wstring key = core::ToLowerAscii(appName);
    if (impl_->changed.find(key) == impl_->changed.end()) {
        // First time FrameFlow touches this profile: record its baseline so we can restore it exactly.
        SavedProfile saved;
        saved.appName = appName;
        saved.profileName = profileName;
        saved.ownedProfile = created;
        NVDRS_SETTING cur{};
        cur.version = NVDRS_SETTING_VER;
        if (!created && NvAPI_DRS_GetSetting(session, profile, FRL_FPS_ID, &cur) == NVAPI_OK) {
            saved.hadSetting = true;
            saved.prevValue = cur.u32CurrentValue;
        }
        impl_->changed[key] = saved;
    }

    NVDRS_SETTING setting{};
    setting.version = NVDRS_SETTING_VER;
    setting.settingId = FRL_FPS_ID;
    setting.settingType = NVDRS_DWORD_TYPE;
    setting.u32CurrentValue = value;
    if (NvAPI_DRS_SetSetting(session, profile, &setting) != NVAPI_OK) return NvapiStatus::Failed;
    if (NvAPI_DRS_SaveSettings(session) != NVAPI_OK) return NvapiStatus::Failed;

    FF_LOG_INFO("Driver frame limit for {} set to {} FPS (profile '{}')", core::WideToUtf8(appName),
                value == 0 ? std::string("off") : std::to_string(value), core::WideToUtf8(profileName));
    return NvapiStatus::Ok;
}

NvapiStatus NvapiLimiter::ClearGameLimit(const std::wstring& exePath)
{
    if (!impl_ || !impl_->initialized) return NvapiStatus::Unavailable;
    const std::wstring key = core::ToLowerAscii(AppNameFor(exePath));
    const auto it = impl_->changed.find(key);
    if (it == impl_->changed.end()) return NvapiStatus::Ok; // never changed it

    NvDRSSessionHandle session{};
    if (!impl_->OpenSession(session)) return NvapiStatus::Failed;
    struct SessionGuard {
        NvDRSSessionHandle s;
        ~SessionGuard() { NvAPI_DRS_DestroySession(s); }
    } guard{session};

    const std::wstring realName = it->second.appName.empty() ? AppNameFor(exePath) : it->second.appName;
    NvAPI_UnicodeString app{};
    ToUnicode(app, realName);
    NvDRSProfileHandle profile{};
    NVDRS_APPLICATION found{};
    found.version = NVDRS_APPLICATION_VER;
    if (NvAPI_DRS_FindApplicationByName(session, app, &profile, &found) == NVAPI_OK) {
        if (it->second.hadSetting) {
            NVDRS_SETTING setting{};
            setting.version = NVDRS_SETTING_VER;
            setting.settingId = FRL_FPS_ID;
            setting.settingType = NVDRS_DWORD_TYPE;
            setting.u32CurrentValue = it->second.prevValue;
            NvAPI_DRS_SetSetting(session, profile, &setting);
        } else {
            // We introduced the setting (or the whole profile): remove just our setting so the driver
            // falls back to its default. The empty FrameFlow profile is harmless and left in place.
            NvAPI_DRS_DeleteProfileSetting(session, profile, FRL_FPS_ID);
        }
        NvAPI_DRS_SaveSettings(session);
        FF_LOG_INFO("Driver frame limit for {} restored", core::WideToUtf8(realName));
    }
    impl_->changed.erase(it);
    return NvapiStatus::Ok;
}

void NvapiLimiter::RestoreAll()
{
    if (!impl_ || !impl_->initialized) return;
    // Copy keys first: ClearGameLimit mutates the map.
    std::vector<std::wstring> keys;
    keys.reserve(impl_->changed.size());
    for (const auto& [k, v] : impl_->changed) keys.push_back(k);
    for (const auto& k : keys) {
        auto it = impl_->changed.find(k);
        if (it != impl_->changed.end()) ClearGameLimit(it->second.appName);
    }
    impl_->changed.clear();
}

#else // !FF_HAVE_NVAPI  — cross-compile / non-MSVC stub

struct NvapiLimiter::Impl {};
NvapiLimiter::NvapiLimiter() : impl_(nullptr) {}
NvapiLimiter::~NvapiLimiter() = default;
NvapiStatus NvapiLimiter::SetGameLimit(const std::wstring&, std::uint32_t) { return NvapiStatus::NotBuilt; }
NvapiStatus NvapiLimiter::ClearGameLimit(const std::wstring&) { return NvapiStatus::NotBuilt; }
void NvapiLimiter::RestoreAll() {}

#endif

} // namespace ff::nvidia
