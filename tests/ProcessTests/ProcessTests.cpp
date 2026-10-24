#include "TestHarness.h"

#include "ProcessDetection/AntiCheatDetector.h"
#include "ProcessDetection/GameScorer.h"

using namespace ff::process;

namespace {
ProcessSignals SteamGame()
{
    ProcessSignals s;
    s.pid = 100;
    s.exeName = "Cyberpunk2077.exe";
    s.exePath = "D:\\SteamLibrary\\steamapps\\common\\Cyberpunk 2077\\bin\\x64\\Cyberpunk2077.exe";
    s.hasVisibleWindow = true;
    s.isForeground = true;
    s.coversMonitor = true;
    s.usesDxgi = true;
    s.usesD3D12 = true;
    s.parentExeName = "steam.exe";
    return s;
}
} // namespace

TEST(Score_SteamGameIsAGame)
{
    ExclusionList ex;
    const auto g = ScoreProcess(SteamGame(), ex);
    CHECK(!g.excluded);
    CHECK(g.score >= kGameScoreThreshold);
    CHECK(InferGraphicsApi(SteamGame()) == "DX12");
}

TEST(Score_ExcludedAppsNeverScore)
{
    ExclusionList ex;
    auto s = SteamGame();
    s.exeName = "Discord.exe";
    CHECK(ScoreProcess(s, ex).excluded);
    s.exeName = "CHROME.EXE"; // case-insensitive
    CHECK(ScoreProcess(s, ex).excluded);
}

TEST(Score_WindowedAppWithDxgiIsNotAGame)
{
    ProcessSignals s;
    s.exeName = "SomeEditor.exe";
    s.exePath = "C:\\Program Files\\Editor\\SomeEditor.exe";
    s.hasVisibleWindow = true;
    s.usesDxgi = true;
    ExclusionList ex;
    CHECK(ScoreProcess(s, ex).score < kGameScoreThreshold);
}

TEST(Score_FullscreenVideoWithoutGraphicsApiIsCapped)
{
    ProcessSignals s;
    s.exeName = "player.exe";
    s.hasVisibleWindow = true;
    s.isForeground = true;
    s.coversMonitor = true;
    ExclusionList ex;
    CHECK(ScoreProcess(s, ex).score < kGameScoreThreshold);
}

TEST(Score_WindowedDx11DesktopAppIsNotAGame)
{
    // Regression: a download manager (foreground, visible, DX11) scored exactly the threshold.
    ProcessSignals s;
    s.exeName = "ABDownloadManager.exe";
    s.exePath = "C:\\Users\\u\\AppData\\Local\\ABDownloadManager\\ABDownloadManager.exe";
    s.hasVisibleWindow = true;
    s.isForeground = true;
    s.usesDxgi = true;
    s.usesD3D11 = true;
    ExclusionList ex;
    CHECK(ScoreProcess(s, ex).score < kGameScoreThreshold);
}

TEST(Score_UnreadableUnrealGameIsDetected)
{
    ProcessSignals s;
    s.exeName = "LEGOBatmanLotDK-Win64-Shipping.exe";
    s.hasVisibleWindow = true;
    s.isForeground = true;
    s.accessible = false;
    ExclusionList ex;
    CHECK(ScoreProcess(s, ex).score >= kGameScoreThreshold);
}

TEST(Score_GamesFolderWithoutModuleAccessIsDetected)
{
    ProcessSignals s;
    s.exeName = "ExampleGame.exe";
    s.exePath = "D:\\GAMES\\ExampleGame\\ExampleGame.exe";
    s.hasVisibleWindow = true;
    s.isForeground = true;
    s.accessible = false;
    ExclusionList ex;
    CHECK(ScoreProcess(s, ex).score >= kGameScoreThreshold);
}

TEST(Score_FrameFlowSessionAlwaysQualifies)
{
    ProcessSignals s;
    s.exeName = "FrameFlow.TestRenderer.exe";
    s.hasVisibleWindow = true;
    s.hasFrameFlowSession = true;
    s.usesD3D11 = true;
    ExclusionList ex;
    CHECK(ScoreProcess(s, ex).score >= kGameScoreThreshold);
}

TEST(Exclusions_UserAddRemove)
{
    ExclusionList ex;
    CHECK(!ex.Contains("MyTool.exe"));
    ex.Add("MyTool.exe");
    CHECK(ex.Contains("mytool.exe"));
    ex.Remove("MYTOOL.exe");
    CHECK(!ex.Contains("mytool.exe"));
}

TEST(AntiCheat_DetectsEacFolder)
{
    const auto hit = DetectAntiCheat("game.exe", {}, {}, {"binaries", "easyanticheat", "game.exe"});
    CHECK(hit.has_value());
    CHECK(hit && hit->product == "Easy Anti-Cheat");
}

TEST(AntiCheat_DetectsKnownProtectedExe)
{
    const auto hit = DetectAntiCheat("VALORANT-Win64-Shipping.exe", {}, {}, {});
    CHECK(hit && hit->product == "Riot Vanguard");
}

TEST(AntiCheat_DetectsBattlEyeModule)
{
    const auto hit = DetectAntiCheat("somegame.exe", {"kernel32.dll", "beclient_x64.dll"}, {}, {});
    CHECK(hit && hit->product == "BattlEye");
}

TEST(AntiCheat_DetectsNetEaseNepFiles)
{
    const auto hit = DetectAntiCheat("Aniimo.exe", {}, {}, {"aniimo.exe", "nep2.dll", "nepkernel.sys"});
    CHECK(hit && hit->product == "NetEase Yidun (NEP)");
}

TEST(AntiCheat_CleanGame)
{
    CHECK(!DetectAntiCheat("Cyberpunk2077.exe", {"d3d12.dll", "dxgi.dll"}, {"explorer.exe", "steam.exe"},
                           {"bin", "engine", "r6"}).has_value());
}

TEST(AntiCheat_UnrelatedVanguardServiceDoesNotBlockOtherGames)
{
    // vgc.exe runs permanently once Vanguard is installed; only the protected games are blocked.
    CHECK(!DetectAntiCheat("eldenring.exe", {}, {"vgc.exe"}, {}).has_value());
}

TEST_MAIN()
