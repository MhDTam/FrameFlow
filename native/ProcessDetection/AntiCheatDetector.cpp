#include "ProcessDetection/AntiCheatDetector.h"
#include "Core/Strings.h"

#include <algorithm>
#include <filesystem>

namespace ff::process {

namespace {

struct Signature {
    std::string_view product;
    std::vector<std::string_view> files;      // next to the game
    std::vector<std::string_view> modules;    // loaded in the game
    std::vector<std::string_view> processes;  // running system-wide while the game runs
    std::vector<std::string_view> gameExes;   // well-known protected executables
};

const std::vector<Signature>& Signatures()
{
    static const std::vector<Signature> sigs = {
        {"Easy Anti-Cheat",
         {"easyanticheat", "easyanticheat_eos", "easyanticheat_eos_setup.exe", "easyanticheat_setup.exe", "start_protected_game.exe"},
         {"easyanticheat_x64.dll", "easyanticheat_eos.dll", "eossdk-win64-shipping.dll-eac"},
         {"easyanticheat.exe", "easyanticheat_eos.exe"},
         {"fortniteclient-win64-shipping.exe", "r5apex.exe", "r5apex_dx12.exe"}},
        {"BattlEye",
         {"battleye", "beclient_x64.dll", "beservice_x64.exe"},
         {"beclient_x64.dll", "beclient.dll"},
         {"beservice.exe", "beservice_x64.exe"},
         {"pubg-win64-shipping.exe", "tslgame.exe", "rainbowsix.exe", "rainbowsix_be.exe", "escapefromtarkov.exe"}},
        {"Riot Vanguard",
         {"vanguard"},
         {},
         {"vgc.exe", "vgtray.exe"},
         {"valorant-win64-shipping.exe", "valorant.exe", "league of legends.exe"}},
        {"FACEIT Anti-Cheat",
         {},
         {"faceitac.dll"},
         {"faceit.exe", "faceitclient.exe", "faceitservice.exe"},
         {}},
        {"Ricochet Anti-Cheat",
         {"randgrid.sys"},
         {},
         {},
         {"cod.exe", "modernwarfare.exe", "blackopscoldwar.exe", "cod23-cod.exe", "cod24-cod.exe", "cod25-cod.exe"}},
        {"EA AntiCheat",
         {"eaanticheat", "eaanticheat.installer.exe", "eaanticheat.gameservicelauncher.exe"},
         {"eaanticheat.gameservice.dll"},
         {"eaanticheat.gameservice.exe"},
         {"bf2042.exe", "fc24.exe", "fc25.exe", "fc26.exe", "fifa23.exe", "bf6.exe"}},
        {"nProtect GameGuard",
         {"gameguard"},
         {"npgg.dll"},
         {"gamemon.des", "gamemon64.des"},
         {}},
        {"XIGNCODE3",
         {"xigncode"},
         {"x3.xem"},
         {},
         {}},
        {"NetEase Yidun (NEP)",
         {"nep2.dll", "nepkernel.sys"},
         {"nep2.dll"},
         {},
         {}},
    };
    return sigs;
}

bool ContainsName(const std::vector<std::string>& haystack, std::string_view needle)
{
    return std::find(haystack.begin(), haystack.end(), needle) != haystack.end();
}

} // namespace

std::optional<AntiCheatHit> DetectAntiCheat(std::string_view exeName,
                                            const std::vector<std::string>& moduleNames,
                                            const std::vector<std::string>& runningProcesses,
                                            const std::vector<std::string>& nearbyFiles)
{
    const std::string exe = core::ToLowerAscii(exeName);
    for (const auto& sig : Signatures()) {
        for (auto g : sig.gameExes)
            if (exe == g) return AntiCheatHit{std::string(sig.product), "known protected executable " + std::string(g)};
        for (auto m : sig.modules)
            if (ContainsName(moduleNames, m)) return AntiCheatHit{std::string(sig.product), "module " + std::string(m)};
        for (auto f : sig.files)
            if (ContainsName(nearbyFiles, f)) return AntiCheatHit{std::string(sig.product), "file " + std::string(f)};
    }
    // Running anti-cheat services only count together with local evidence of a game install,
    // otherwise an unrelated EAC service would block every game on the machine.
    // Exception: services that only run while their protected game runs (BattlEye, EAC runtime).
    for (const auto& sig : Signatures()) {
        for (auto p : sig.processes) {
            if (!ContainsName(runningProcesses, p)) continue;
            if (sig.product == "BattlEye" || sig.product == "Easy Anti-Cheat") {
                return AntiCheatHit{std::string(sig.product), "running process " + std::string(p)};
            }
        }
    }
    return std::nullopt;
}

std::vector<std::string> CollectNearbyFiles(const std::wstring& exePath)
{
    std::vector<std::string> out;
    if (exePath.empty()) return out;
    namespace fs = std::filesystem;

    // Directories that hold many unrelated installs. We never list them: an anti-cheat folder that
    // belongs to another game (e.g. %LOCALAPPDATA%\BattlEye) must not flag this executable.
    static constexpr std::string_view kSharedRoots[] = {
        "common", "steamapps", "steamlibrary", "program files", "program files (x86)", "programdata", "local",
        "roaming", "locallow", "appdata", "games", "epic games", "xboxgames", "windows", "users", "desktop",
        "downloads", "documents",
    };
    auto isSharedRoot = [](const fs::path& p) {
        const std::string name = core::ToLowerAscii(core::WideToUtf8(p.filename().wstring()));
        if (name.empty() || p == p.root_path()) return true;
        return std::any_of(std::begin(kSharedRoots), std::end(kSharedRoots), [&](std::string_view r) { return name == r; });
    };

    std::error_code ec;
    fs::path dir = fs::path(exePath).parent_path();
    // Walk from the exe folder up to (at most) three levels, stopping at the first shared root:
    // game\bin\x64\game.exe inspects x64, bin and game — never the library that contains the game.
    for (int level = 0; level < 3 && !dir.empty() && !isSharedRoot(dir); ++level) {
        std::size_t count = 0;
        for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
             it.increment(ec)) {
            out.push_back(core::ToLowerAscii(core::WideToUtf8(it->path().filename().wstring())));
            if (++count > 512) break; // bounded: some game roots contain thousands of files
        }
        const fs::path parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
        ec.clear();
    }
    return out;
}

} // namespace ff::process
