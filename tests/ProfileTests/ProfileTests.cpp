#include "TestHarness.h"

#include "Core/Json.h"
#include "Core/Paths.h"
#include "Profiles/ProfileStore.h"

#include <windows.h>

#include <filesystem>

using namespace ff;

TEST(Json_RoundTrip)
{
    auto j = core::Json::MakeObject();
    j.Set("game", "Cyberpunk 2077");
    j.Set("fps", 59.94);
    j.Set("on", true);
    auto arr = core::Json::MakeArray();
    arr.Push(1);
    arr.Push("two");
    j.Set("list", std::move(arr));
    const auto parsed = core::Json::Parse(j.Dump(true));
    CHECK(parsed.has_value());
    CHECK((*parsed)["game"].AsString() == "Cyberpunk 2077");
    CHECK_NEAR((*parsed)["fps"].AsNumber(), 59.94, 1e-12);
    CHECK((*parsed)["on"].AsBool() == true);
    CHECK((*parsed)["list"].Items().size() == 2);
}

TEST(Json_UnicodeEscapes)
{
    const auto j = core::Json::Parse(R"({"name":"\u0645\u0631\u062d\u0628\u0627 \ud83c\udfae"})");
    CHECK(j.has_value());
    CHECK((*j)["name"].AsString() == "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 \xF0\x9F\x8E\xAE");
}

TEST(Json_RejectsMalformedAndDeepInput)
{
    CHECK(!core::Json::Parse("{\"a\":}").has_value());
    CHECK(!core::Json::Parse("[1,2").has_value());
    CHECK(!core::Json::Parse("{} trailing").has_value());
    CHECK(!core::Json::Parse("\"\\ud800\"").has_value()); // lone surrogate
    std::string deep(200, '[');
    deep += std::string(200, ']');
    CHECK(!core::Json::Parse(deep, 64).has_value());
}

TEST(ProfileId_StableAndPathSensitive)
{
    const auto a = profiles::MakeProfileId("Game.exe", "C:\\Games\\A\\Game.exe");
    const auto b = profiles::MakeProfileId("game.exe", "c:\\games\\a\\game.exe");
    const auto c = profiles::MakeProfileId("Game.exe", "C:\\Games\\B\\Game.exe");
    CHECK(a == b);
    CHECK(a != c);
    CHECK(a.rfind("game.exe-", 0) == 0);
}

TEST(Profile_FromJsonValidates)
{
    auto j = core::Json::MakeObject();
    j.Set("exe", "x.exe");
    j.Set("fpsLimit", 99999.0);
    j.Set("pacingMode", "Turbo");
    const auto p = profiles::GameProfile::FromJson(j);
    CHECK(p.has_value());
    CHECK(p->fpsLimit == 60.0);
    CHECK(p->pacingMode == "Balanced");
    CHECK(p->limitMethod == "auto");
    CHECK(!p->framePacing);
    CHECK(!p->startWithGame);
    CHECK(!p->allowProtected); // old profiles never imply consent
    j.Set("limitMethod", "unknown");
    CHECK(profiles::GameProfile::FromJson(j)->limitMethod == "auto");
    j.Set("limitMethod", "driver"); // migrate profiles from builds with the removed NVIDIA method
    CHECK(profiles::GameProfile::FromJson(j)->limitMethod == "auto");
    CHECK(!profiles::GameProfile::FromJson(core::Json::MakeObject()).has_value());
}

TEST(ProfileStore_SaveLoadAndLoadOrCreate)
{
    wchar_t tmp[MAX_PATH];
    ::GetTempPathW(MAX_PATH, tmp);
    const auto dir = std::filesystem::path(tmp) / (L"FrameFlowProfileTest-" + std::to_wstring(::GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    {
        profiles::ProfileStore store(dir);
        auto p = store.LoadOrCreate("Cyberpunk2077.exe", "D:\\Games\\Cyberpunk2077.exe", 60.0, "Balanced");
        CHECK(p.fpsLimit == 60.0);
        CHECK(!p.allowProtected);
        CHECK(p.game == "Cyberpunk2077"); // file does not exist -> falls back to exe stem
        p.fpsLimit = 141.0;
        p.pacingMode = "Precision";
        p.limitMethod = "frameflow";
        p.allowProtected = true;
        CHECK(store.Save(p));

        const auto again = store.LoadOrCreate("Cyberpunk2077.exe", "D:\\Games\\Cyberpunk2077.exe", 60.0, "Balanced");
        CHECK(again.fpsLimit == 141.0);
        CHECK(again.pacingMode == "Precision");
        CHECK(again.limitMethod == "frameflow");
        CHECK(again.allowProtected);
        CHECK(store.LoadAll().size() == 1);
    }
    std::filesystem::remove_all(dir, ec);
}

TEST(Paths_DataDirOverride)
{
    wchar_t tmp[MAX_PATH];
    ::GetTempPathW(MAX_PATH, tmp);
    const auto custom = std::filesystem::path(tmp) / (L"FrameFlowPathTest-" + std::to_wstring(::GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(custom, ec);

    ::SetEnvironmentVariableW(L"FRAMEFLOW_DATA_DIR", custom.c_str());
    CHECK(core::AppDataRoot() == custom);
    CHECK(core::ProfilesDir() == custom / L"Profiles");
    CHECK(core::LogsDir() == custom / L"Logs");
    CHECK(core::SettingsFile() == custom / L"settings.json");

    ::SetEnvironmentVariableW(L"FRAMEFLOW_DATA_DIR", nullptr);
    std::filesystem::remove_all(custom, ec);
}

TEST(Paths_PortableModeDetection)
{
    ::SetEnvironmentVariableW(L"FRAMEFLOW_PORTABLE", L"1");
    CHECK(core::IsPortableMode());
    ::SetEnvironmentVariableW(L"FRAMEFLOW_PORTABLE", L"0");
    // When 0, should not be portable unless directory has marker
    CHECK(!core::IsPortableMode() || std::filesystem::exists(core::ExecutableDir() / L".portable"));
    ::SetEnvironmentVariableW(L"FRAMEFLOW_PORTABLE", nullptr);
}

TEST_MAIN()
