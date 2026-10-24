#include "TestHarness.h"

#include "Display/FpsPresets.h"
#include "Graphics/PresentPacer.h"
#include "IPC/Protocol.h"
#include "IPC/SharedSession.h"
#include "Timing/Clock.h"

#include <windows.h>

using namespace ff;

TEST(SharedSession_CreateOpenAndRing)
{
    auto client = ipc::SharedSession::CreateForCurrentProcess("DX11");
    CHECK(client != nullptr);
    if (!client) return;
    auto engine = ipc::SharedSession::Open(::GetCurrentProcessId());
    CHECK(engine != nullptr);
    if (!engine) return;
    CHECK(std::string(engine->Block().client.graphicsApi) == "DX11");

    const std::int64_t f = timing::Clock::Frequency();
    const std::int64_t t0 = 1'000'000;
    for (int i = 0; i < 11; ++i)
        client->PushPresent(t0 + i * f / 100, t0 + i * f / 100 - f / 1000,
                            i == 0 ? 0 : t0 + (i - 1) * f / 100 + f / 10000,
                            t0 + i * f / 100 - f / 500); // 10 ms apart
    CHECK(engine->Block().ring.preWaitQpc[10] == t0 + f / 10 - f / 1000);
    CHECK(engine->Block().ring.previousAfterPresentQpc[10] == t0 + 9 * f / 100 + f / 10000);
    CHECK(engine->Block().ring.hookEntryQpc[10] == t0 + f / 10 - f / 500);
    ipc::RingReader reader;
    std::vector<double> ft;
    CHECK(reader.ReadNew(engine->Block(), ft) == 10);
    CHECK(ft.size() == 10);
    for (double v : ft) CHECK_NEAR(v, 10.0, 0.001);
    CHECK(reader.ReadNew(engine->Block(), ft) == 0); // nothing new
}

TEST(RingReader_SurvivesOverrun)
{
    auto client = ipc::SharedSession::CreateForCurrentProcess("DX11");
    if (!client) return;
    ipc::RingReader reader;
    std::vector<double> ft;
    reader.ReadNew(client->Block(), ft);
    const std::int64_t f = timing::Clock::Frequency();
    for (std::int64_t i = 0; i < ipc::kRingCapacity * 3; ++i) client->PushPresent(5'000'000 + i * f / 1000);
    ft.clear();
    const auto n = reader.ReadNew(client->Block(), ft);
    CHECK(n > 0 && n < ipc::kRingCapacity);
    for (double v : ft) CHECK_NEAR(v, 1.0, 0.001);
}

TEST(PresentPacer_EngineControlOverridesLocal)
{
    graphics::PresentPacer pacer("DX11");
    CHECK(pacer.HasSession());
    pacer.SetLocal(false, 60.0, timing::PacingMode::Balanced);
    pacer.BeforePresent();
    CHECK(!pacer.PacingActive());

    auto engine = ipc::SharedSession::Open(::GetCurrentProcessId());
    CHECK(engine != nullptr);
    if (!engine) return;
    auto& c = engine->Block().control;
    c.targetFpsMilli = 144000;
    c.mode = 2;
    c.pacingEnabled = 1;
    c.engineHeartbeatQpc = timing::Clock::Now();
    c.engineAttached = 1;
    ::Sleep(60); // config poll interval is 50 ms
    pacer.BeforePresent();
    CHECK(pacer.EngineControlled());
    CHECK(pacer.PacingActive());
    CHECK_NEAR(pacer.AppliedFps(), 144.0, 1e-9);
    CHECK(pacer.AppliedMode() == timing::PacingMode::Precision);

    // Engine goes away (stale heartbeat): fall back to local = off.
    c.engineHeartbeatQpc = timing::Clock::Now() - timing::Clock::Frequency() * 5;
    ::Sleep(60);
    pacer.BeforePresent();
    CHECK(!pacer.EngineControlled());
    CHECK(!pacer.PacingActive());
}

TEST(Protocol_ValidatesFps)
{
    auto m = ipc::protocol::Make("SetFpsLimit");
    m.Set("fps", 141.0);
    CHECK(ipc::protocol::ValidateFps(m).value_or(0) == 141.0);
    m.Set("fps", 5.0);
    CHECK(!ipc::protocol::ValidateFps(m).has_value());
    m.Set("fps", "60");
    CHECK(!ipc::protocol::ValidateFps(m).has_value());
    m.Set("fps", 59.94049);
    CHECK_NEAR(ipc::protocol::ValidateFps(m).value_or(0), 59.94, 1e-9);
}

TEST(Protocol_ValidatesPaths)
{
    auto m = ipc::protocol::Make("SelectGame");
    m.Set("path", "relative\\game.exe");
    CHECK(!ipc::protocol::ValidateExePath(m).has_value());
    m.Set("path", "C:\\Windows\\notepad.txt");
    CHECK(!ipc::protocol::ValidateExePath(m).has_value());
    m.Set("path", "\\\\server\\share\\game.exe");
    CHECK(!ipc::protocol::ValidateExePath(m).has_value());
    m.Set("path", "C:\\Windows\\System32\\notepad.exe");
    CHECK(ipc::protocol::ValidateExePath(m).has_value());
}

TEST(Presets_RefreshAware)
{
    const auto p144 = display::SmartPresets(144.0);
    CHECK(std::find(p144.begin(), p144.end(), 141.0) != p144.end());
    const auto p165 = display::SmartPresets(164.99);
    CHECK(std::find(p165.begin(), p165.end(), 162.0) != p165.end());
    const auto p100 = display::SmartPresets(100.0);
    CHECK(!p100.empty() && p100.back() <= 100.0);
    CHECK(display::SuggestedLimit(144.0) == 141.0);
    CHECK(display::SuggestedLimit(60.0) == 60.0);
}

TEST_MAIN()
