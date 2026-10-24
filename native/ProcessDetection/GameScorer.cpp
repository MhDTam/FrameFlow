#include "ProcessDetection/GameScorer.h"
#include "Core/Strings.h"

#include <algorithm>

namespace ff::process {

namespace {

// Signal weights. A typical Steam game in fullscreen scores 25+25+25+30 = 105; a windowed
// productivity app that happens to load DXGI scores 10+15 = 25 and stays well below threshold.
constexpr int kForeground = 25;
constexpr int kVisibleWindow = 10;
constexpr int kCoversMonitor = 25;
constexpr int kD3D = 25;          // d3d11 / d3d12 / vulkan
constexpr int kDxgiOnly = 15;     // dxgi without a device module: weaker, many apps do this
constexpr int kD3D9 = 20;
constexpr int kOpenGL = 10;
constexpr int kStoreLibrary = 30; // steamapps\common, Epic Games, etc.
constexpr int kLauncherParent = 20;
constexpr int kSession = 100;     // speaks FrameFlow protocol: always a pacing target
constexpr int kShippingBinary = 40; // Unreal packaged game executable; useful when module access is denied

bool PathLooksLikeGameLibrary(std::string_view path, std::string& which)
{
    static constexpr std::string_view kMarkers[][2] = {
        {"\\steamapps\\common\\", "Steam library"},
        {"\\epic games\\", "Epic Games library"},
        {"\\gog galaxy\\games\\", "GOG library"},
        {"\\gog games\\", "GOG library"},
        {"\\xboxgames\\", "Xbox app library"},
        {"\\ubisoft game launcher\\games\\", "Ubisoft library"},
        {"\\ea games\\", "EA library"},
        {"\\battle.net\\", "Battle.net library"},
        {"\\games\\", "Games folder"},
    };
    const std::string lower = core::ToLowerAscii(path);
    for (const auto& m : kMarkers) {
        if (lower.find(m[0]) != std::string::npos) {
            which = m[1];
            return true;
        }
    }
    return false;
}

bool IsLauncher(std::string_view exe)
{
    static constexpr std::string_view kLaunchers[] = {
        "steam.exe", "epicgameslauncher.exe", "galaxyclient.exe", "upc.exe", "eadesktop.exe",
        "battle.net.exe", "gamelaunchhelper.exe",
    };
    const std::string lower = core::ToLowerAscii(exe);
    return std::any_of(std::begin(kLaunchers), std::end(kLaunchers), [&](std::string_view l) { return lower == l; });
}

bool IsShippingGameBinary(std::string_view exe)
{
    const std::string lower = core::ToLowerAscii(exe);
    return lower.ends_with("-win64-shipping.exe") || lower.ends_with("-wingdk-shipping.exe");
}

} // namespace

const std::vector<std::string>& ExclusionList::Defaults()
{
    static const std::vector<std::string> defaults = {
        "explorer.exe", "dwm.exe", "discord.exe", "steam.exe", "steamwebhelper.exe", "epicgameslauncher.exe",
        "chrome.exe", "msedge.exe", "firefox.exe", "opera.exe", "brave.exe", "spotify.exe",
        "frameflow.ui.exe", "frameflow.engine.exe", "frameflow.benchmark.exe",
        "devenv.exe", "code.exe", "applicationframehost.exe", "shellexperiencehost.exe", "searchhost.exe",
        "startmenuexperiencehost.exe", "textinputhost.exe", "systemsettings.exe", "taskmgr.exe",
        "obs64.exe", "nvidia app.exe", "nvidia overlay.exe", "rtss.exe", "msiafterburner.exe",
        "windowsterminal.exe", "cmd.exe", "powershell.exe", "conhost.exe", "claude.exe", "chatgpt.exe",
        "galaxyclient.exe", "upc.exe", "eadesktop.exe", "battle.net.exe", "parsecd.exe",
    };
    return defaults;
}

ExclusionList::ExclusionList() : entries_(Defaults()) {}

void ExclusionList::Add(std::string_view exeName)
{
    const std::string lower = core::ToLowerAscii(exeName);
    if (lower.empty() || Contains(lower)) return;
    entries_.push_back(lower);
}

void ExclusionList::Remove(std::string_view exeName)
{
    const std::string lower = core::ToLowerAscii(exeName);
    entries_.erase(std::remove(entries_.begin(), entries_.end(), lower), entries_.end());
}

bool ExclusionList::Contains(std::string_view exeName) const
{
    const std::string lower = core::ToLowerAscii(exeName);
    return std::find(entries_.begin(), entries_.end(), lower) != entries_.end();
}

std::string_view InferGraphicsApi(const ProcessSignals& s)
{
    if (s.usesD3D12) return "DX12";
    if (s.usesVulkan) return "Vulkan";
    if (s.usesD3D11) return "DX11";
    if (s.usesD3D9) return "DX9";
    if (s.usesOpenGL) return "OpenGL";
    if (s.usesDxgi) return "DXGI";
    return "Unknown";
}

GameScore ScoreProcess(const ProcessSignals& s, const ExclusionList& exclusions)
{
    GameScore g;
    if (exclusions.Contains(s.exeName)) {
        g.excluded = true;
        g.reasons.emplace_back("excluded application");
        return g;
    }
    auto add = [&](int pts, std::string why) {
        g.score += pts;
        g.reasons.push_back(std::move(why));
    };

    if (s.hasFrameFlowSession) add(kSession, "FrameFlow session present");
    const bool shippingBinary = IsShippingGameBinary(s.exeName);
    if (shippingBinary) add(kShippingBinary, "packaged game executable");
    if (s.isForeground) add(kForeground, "foreground window");
    if (s.hasVisibleWindow) add(kVisibleWindow, "visible top-level window");
    if (s.coversMonitor) add(kCoversMonitor, "fullscreen or borderless");

    if (s.usesD3D12 || s.usesD3D11 || s.usesVulkan) add(kD3D, std::string("renders with ") + std::string(InferGraphicsApi(s)));
    else if (s.usesD3D9) add(kD3D9, "renders with DX9");
    else if (s.usesDxgi) add(kDxgiOnly, "loads DXGI");
    if (s.usesOpenGL && !(s.usesD3D11 || s.usesD3D12 || s.usesVulkan)) add(kOpenGL, "loads OpenGL");

    std::string lib;
    if (PathLooksLikeGameLibrary(s.exePath, lib)) add(kStoreLibrary, "installed in " + lib);
    if (IsLauncher(s.parentExeName)) add(kLauncherParent, "launched by " + s.parentExeName);

    // A normal windowed desktop app that happens to render with D3D (download managers, launchers,
    // Electron apps) must never qualify on "foreground + visible + DX11" alone. At least one signal
    // that is characteristic of games is required.
    const bool strongSignal = s.coversMonitor || !lib.empty() || IsLauncher(s.parentExeName) ||
                              s.hasFrameFlowSession || shippingBinary;
    if (!strongSignal) {
        g.score = std::min(g.score, kGameScoreThreshold - 1);
        g.reasons.emplace_back("no game-specific signal (fullscreen, store library or launcher)");
    }

    // Without any graphics module and without a store path, a foreground fullscreen window
    // (video player, presentation) should not be mistaken for a game.
    const bool anyGraphics = s.usesDxgi || s.usesD3D9 || s.usesD3D11 || s.usesD3D12 || s.usesVulkan || s.usesOpenGL;
    if (!anyGraphics && lib.empty() && !s.hasFrameFlowSession && !shippingBinary) {
        g.score = std::min(g.score, kGameScoreThreshold - 1);
        g.reasons.emplace_back("no graphics API evidence");
    }
    return g;
}

} // namespace ff::process
