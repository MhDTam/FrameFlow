#include "Profiles/ProfileStore.h"
#include "Core/Log.h"
#include "Core/Strings.h"

#include <windows.h>

#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>

namespace ff::profiles {

namespace {

constexpr std::uintmax_t kMaxProfileBytes = 64 * 1024;

std::uint32_t Fnv1a(std::string_view s)
{
    std::uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

std::int64_t UnixNow()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string QueryVersionString(const std::wstring& path, const wchar_t* key)
{
    DWORD handle = 0;
    const DWORD size = ::GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (size == 0) return {};
    std::vector<BYTE> data(size);
    if (!::GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return {};

    struct LangCodePage { WORD lang; WORD codepage; }* translations = nullptr;
    UINT len = 0;
    if (!::VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &len) ||
        len < sizeof(LangCodePage)) {
        return {};
    }
    wchar_t sub[128];
    swprintf_s(sub, L"\\StringFileInfo\\%04x%04x\\%s", translations[0].lang, translations[0].codepage, key);
    wchar_t* value = nullptr;
    UINT valueLen = 0;
    if (!::VerQueryValueW(data.data(), sub, reinterpret_cast<void**>(&value), &valueLen) || valueLen == 0) return {};
    return core::WideToUtf8(std::wstring_view(value, valueLen > 0 ? valueLen - 1 : 0));
}

bool LooksLikeUsefulName(const std::string& s)
{
    if (s.size() < 2 || s.size() > 80) return false;
    // Engine boilerplate that is not a game title.
    static constexpr std::string_view kGeneric[] = {"BootstrapPackagedGame", "UnrealGame", "Unity", "Game", "Launcher"};
    for (auto g : kGeneric)
        if (core::IEquals(s, g)) return false;
    return true;
}

} // namespace

core::Json GameProfile::ToJson() const
{
    auto j = core::Json::MakeObject();
    j.Set("schema", 1);
    j.Set("profileId", profileId);
    j.Set("game", game);
    j.Set("exe", exe);
    j.Set("exePath", exePath);
    j.Set("fpsLimit", fpsLimit);
    j.Set("framePacing", framePacing);
    j.Set("pacingMode", pacingMode);
    j.Set("limitMethod", limitMethod);
    j.Set("startWithGame", startWithGame);
    j.Set("allowProtected", allowProtected);
    j.Set("manual", manual);
    j.Set("lastDetected", lastDetectedUnix);
    return j;
}

std::optional<GameProfile> GameProfile::FromJson(const core::Json& j)
{
    if (!j.IsObject() || !j["exe"].IsString() || j["exe"].AsString().empty()) return std::nullopt;
    GameProfile p;
    p.exe = j["exe"].AsString();
    p.exePath = j["exePath"].AsString("");
    p.profileId = j["profileId"].AsString(MakeProfileId(p.exe, p.exePath));
    p.game = j["game"].AsString(p.exe);
    const double fps = j["fpsLimit"].AsNumber(60.0);
    p.fpsLimit = (std::isfinite(fps) && fps >= 10.0 && fps <= 1000.0) ? fps : 60.0;
    p.framePacing = j["framePacing"].AsBool(true);
    const std::string mode = j["pacingMode"].AsString("Balanced");
    p.pacingMode = (mode == "Efficiency" || mode == "Precision") ? mode : "Balanced";
    const std::string method = j["limitMethod"].AsString("auto");
    p.limitMethod = (method == "frameflow" || method == "driver") ? method : "auto";
    p.startWithGame = j["startWithGame"].AsBool(true);
    p.allowProtected = j["allowProtected"].AsBool(false);
    p.manual = j["manual"].AsBool(false);
    p.lastDetectedUnix = j["lastDetected"].AsInt(0);
    return p;
}

std::string MakeProfileId(std::string_view exeName, std::string_view exePath)
{
    const std::string base = core::SanitizeFileName(core::ToLowerAscii(exeName));
    if (exePath.empty()) return base;
    char hash[16];
    snprintf(hash, sizeof(hash), "%08x", Fnv1a(core::ToLowerAscii(exePath)));
    return base + "-" + hash;
}

std::string DisplayNameForExe(const std::wstring& exePath, std::string_view exeName)
{
    if (!exePath.empty()) {
        for (const wchar_t* key : {L"ProductName", L"FileDescription"}) {
            std::string v = QueryVersionString(exePath, key);
            while (!v.empty() && (v.back() == ' ' || v.back() == '\0')) v.pop_back();
            if (LooksLikeUsefulName(v)) return v;
        }
    }
    std::string name(exeName);
    if (name.size() > 4 && core::IEquals(name.substr(name.size() - 4), ".exe")) name.resize(name.size() - 4);
    return name;
}

ProfileStore::ProfileStore(std::filesystem::path dir) : dir_(std::move(dir))
{
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
}

std::filesystem::path ProfileStore::PathFor(const std::string& profileId) const
{
    return dir_ / core::Utf8ToWide(core::SanitizeFileName(profileId) + ".json");
}

std::optional<GameProfile> ProfileStore::Load(const std::string& profileId) const
{
    const auto path = PathFor(profileId);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || std::filesystem::file_size(path, ec) > kMaxProfileBytes) return std::nullopt;
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    auto json = core::Json::Parse(ss.str());
    if (!json) {
        FF_LOG_WARN("Profile {} is corrupt; ignoring", core::WideToUtf8(path.wstring()));
        return std::nullopt;
    }
    return GameProfile::FromJson(*json);
}

bool ProfileStore::Save(const GameProfile& profile) const
{
    const auto path = PathFor(profile.profileId);
    auto tmp = path;
    tmp += L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << profile.ToJson().Dump(true) << '\n';
        if (!out.good()) return false;
    }
    if (!::MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        FF_LOG_ERROR("Saving profile {} failed: {}", profile.profileId, ::GetLastError());
        return false;
    }
    return true;
}

std::vector<GameProfile> ProfileStore::LoadAll() const
{
    std::vector<GameProfile> out;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir_, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() != L".json") continue;
        const std::string id = core::WideToUtf8(it->path().stem().wstring());
        if (auto p = Load(id)) out.push_back(std::move(*p));
    }
    return out;
}

GameProfile ProfileStore::LoadOrCreate(std::string_view exeName, std::string_view exePath, double defaultFps,
                                       std::string_view defaultMode)
{
    const std::string id = MakeProfileId(exeName, exePath);
    if (auto existing = Load(id)) {
        existing->lastDetectedUnix = UnixNow();
        if (existing->exePath.empty()) existing->exePath = std::string(exePath);
        Save(*existing);
        return *existing;
    }
    GameProfile p;
    p.profileId = id;
    p.exe = std::string(exeName);
    p.exePath = std::string(exePath);
    p.game = DisplayNameForExe(core::Utf8ToWide(exePath), exeName);
    p.fpsLimit = defaultFps;
    p.pacingMode = std::string(defaultMode);
    p.lastDetectedUnix = UnixNow();
    Save(p);
    FF_LOG_INFO("Created profile {} for {}", p.profileId, p.game);
    return p;
}

} // namespace ff::profiles
