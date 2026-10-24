#include "EngineSettings.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace ff::engine {

core::Json EngineSettings::ToJson() const
{
    auto j = core::Json::MakeObject();
    j.Set("schema", 1);
    j.Set("defaultFps", defaultFps);
    j.Set("defaultMode", defaultMode);
    j.Set("autoDetect", autoDetect);
    j.Set("miniOverlay", miniOverlay);
    j.Set("telemetrySeconds", telemetrySeconds);
    auto ex = core::Json::MakeArray();
    for (const auto& e : extraExclusions) ex.Push(e);
    j.Set("extraExclusions", std::move(ex));
    auto manual = core::Json::MakeArray();
    for (const auto& m : manualProfileIds) manual.Push(m);
    j.Set("manualProfileIds", std::move(manual));
    j.Set("logLevel", logLevel);
    return j;
}

EngineSettings EngineSettings::FromJson(const core::Json& j)
{
    EngineSettings s;
    const double fps = j["defaultFps"].AsNumber(60.0);
    s.defaultFps = (std::isfinite(fps) && fps >= 10 && fps <= 1000) ? fps : 60.0;
    const std::string mode = j["defaultMode"].AsString("Balanced");
    s.defaultMode = (mode == "Efficiency" || mode == "Precision") ? mode : "Balanced";
    s.autoDetect = j["autoDetect"].AsBool(true);
    s.miniOverlay = j["miniOverlay"].AsBool(false);
    s.telemetrySeconds = std::clamp(j["telemetrySeconds"].AsNumber(30.0), 10.0, 60.0);
    for (const auto& e : j["extraExclusions"].Items())
        if (e.IsString() && !e.AsString().empty() && e.AsString().size() < 260) s.extraExclusions.push_back(e.AsString());
    for (const auto& m : j["manualProfileIds"].Items())
        if (m.IsString() && !m.AsString().empty() && m.AsString().size() < 200) s.manualProfileIds.push_back(m.AsString());
    s.logLevel = j["logLevel"].AsString("INFO");
    return s;
}

EngineSettings EngineSettings::Load(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    std::stringstream ss;
    ss << in.rdbuf();
    auto j = core::Json::Parse(ss.str());
    return j ? FromJson(*j) : EngineSettings{};
}

bool EngineSettings::Save(const std::filesystem::path& file) const
{
    auto tmp = file;
    tmp += L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << ToJson().Dump(true) << '\n';
    }
    return ::MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
}

} // namespace ff::engine
