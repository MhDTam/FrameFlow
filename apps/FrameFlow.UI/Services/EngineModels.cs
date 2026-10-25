using System.Text.Json;

namespace FrameFlow.UI.Services;

// Typed views over engine messages. Parsing is tolerant: missing fields get safe defaults, so a
// newer engine can add fields without breaking an older UI.

public sealed record GameInfo(int Pid, string Name, string Exe, string ExePath, string Api, string ProfileId, bool Manual,
                              IReadOnlyList<string> Reasons);

public sealed record Candidate(int Pid, string Exe, string Title, string ExePath, int Score);

public sealed record Integration(string Status, string Title, string Message, string? AntiCheat, bool RiskAccepted, bool HookBlocked)
{
    public bool CanPace => Status is "Active" or "Standby";
}

public sealed record NvidiaInfo(bool Available, string Gpu, string DriverVersion);

public sealed record EngineSettingsView(bool AutoDetect, bool MiniOverlay, double TelemetrySeconds, double DefaultFps, string DefaultMode,
                                        string LogLevel, IReadOnlyList<string> ExtraExclusions,
                                        IReadOnlyList<string> BuiltInExclusions, string ProfilesDir, string LogsDir);

public sealed record EngineStatus(
    string Version,
    double FpsLimit,
    string PacingMode,
    bool PacingEnabled,
    int DisplayWidth,
    int DisplayHeight,
    double RefreshHz,
    IReadOnlyList<double> Presets,
    double SuggestedFps,
    Integration Integration,
    GameInfo? Game,
    IReadOnlyList<Candidate> Candidates,
    EngineSettingsView Settings,
    string LimitMethod,      // "driver" | "frameflow" | "none"
    string LimitMethodPreference,
    NvidiaInfo Nvidia,
    string Measurement)      // "live" | "needsAdmin" | "off"
{
    public bool DriverLimited => LimitMethod == "driver";
    public bool MeasurementNeedsAdmin => Measurement == "needsAdmin";

    public static EngineStatus Parse(JsonElement e)
    {
        var disp = e.Obj("display");
        var integ = e.Obj("integration");
        GameInfo? game = null;
        if (e.TryGetProperty("game", out var g) && g.ValueKind == JsonValueKind.Object)
        {
            game = new GameInfo(g.Int("pid"), g.Str("name"), g.Str("exe"), g.Str("exePath"), g.Str("api", "Unknown"),
                                g.Str("profileId"), g.Bool("manual"), g.StrList("reasons"));
        }
        var cands = new List<Candidate>();
        if (e.TryGetProperty("candidates", out var ca) && ca.ValueKind == JsonValueKind.Array)
            foreach (var c in ca.EnumerateArray())
                cands.Add(new Candidate(c.Int("pid"), c.Str("exe"), c.Str("title"), c.Str("exePath"), c.Int("score")));

        var s = e.Obj("settings");
        var settings = new EngineSettingsView(s.Bool("autoDetect", true), s.Bool("miniOverlay"), s.Num("telemetrySeconds", 30), s.Num("defaultFps", 60),
                                              s.Str("defaultMode", "Balanced"), s.Str("logLevel", "INFO"),
                                              s.StrList("extraExclusions"), s.StrList("builtInExclusions"),
                                              s.Str("profilesDir"), s.Str("logsDir"));

        return new EngineStatus(
            e.Str("version", "1.0.2"),
            e.Num("fpsLimit", 60),
            e.Str("pacingMode", "Balanced"),
            e.Bool("pacingEnabled"),
            disp.Int("width"), disp.Int("height"), disp.Num("refreshHz"),
            e.NumList("presets"),
            e.Num("suggestedFps", 60),
            new Integration(integ.Str("status", "None"), integ.Str("title"), integ.Str("message"),
                            integ.TryGetProperty("antiCheat", out var ac) ? ac.GetString() : null, integ.Bool("riskAccepted"), integ.Bool("hookBlocked")),
            game, cands, settings,
            e.Str("limitMethod", "none"),
            e.Str("limitMethodPreference", "auto"),
            new NvidiaInfo(e.Obj("nvidia").Bool("available"), e.Obj("nvidia").Str("gpu"), e.Obj("nvidia").Str("driverVersion")),
            e.Str("measurement", "off"));
    }
}

public sealed record Telemetry(
    bool Live,
    string Method,
    bool PacingActive,
    double AppliedFps,
    double CurrentFps,
    double AverageFps,
    double FrametimeMs,
    double AvgFrametimeMs,
    double Low1Fps,
    double Low01Fps,
    double StddevMs,
    double MinFrametimeMs,
    double MaxFrametimeMs,
    double HistorySeconds,
    double[] GraphMean,
    double[] GraphMax)
{
    public static Telemetry Parse(JsonElement e)
    {
        bool live = e.Str("source") == "live";
        var s = e.Obj("stats");
        var g = e.Obj("graph");
        return new Telemetry(live, e.Str("method", "frameflow"), e.Bool("pacingActive"), e.Num("appliedFps"),
            s.Num("currentFps"), s.Num("averageFps"), s.Num("frametimeMs"), s.Num("avgFrametimeMs"),
            s.Num("low1Fps"), s.Num("low01Fps"), s.Num("stddevMs"), s.Num("minFrametimeMs"), s.Num("maxFrametimeMs"),
            g.Num("historySeconds", 30), g.NumList("mean").ToArray(), g.NumList("max").ToArray());
    }
}

internal static class JsonExt
{
    private static readonly JsonElement Empty = JsonDocument.Parse("{}").RootElement.Clone();

    public static JsonElement Obj(this JsonElement e, string name) =>
        e.ValueKind == JsonValueKind.Object && e.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.Object ? v : Empty;

    public static string Str(this JsonElement e, string name, string fallback = "") =>
        e.ValueKind == JsonValueKind.Object && e.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.String ? v.GetString() ?? fallback : fallback;

    public static double Num(this JsonElement e, string name, double fallback = 0) =>
        e.ValueKind == JsonValueKind.Object && e.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.Number ? v.GetDouble() : fallback;

    public static int Int(this JsonElement e, string name, int fallback = 0) => (int)Math.Round(e.Num(name, fallback));

    public static bool Bool(this JsonElement e, string name, bool fallback = false) =>
        e.ValueKind == JsonValueKind.Object && e.TryGetProperty(name, out var v) && v.ValueKind is JsonValueKind.True or JsonValueKind.False ? v.GetBoolean() : fallback;

    public static List<double> NumList(this JsonElement e, string name)
    {
        var list = new List<double>();
        if (e.ValueKind == JsonValueKind.Object && e.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.Array)
            foreach (var x in v.EnumerateArray())
                if (x.ValueKind == JsonValueKind.Number) list.Add(x.GetDouble());
        return list;
    }

    public static List<string> StrList(this JsonElement e, string name)
    {
        var list = new List<string>();
        if (e.ValueKind == JsonValueKind.Object && e.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.Array)
            foreach (var x in v.EnumerateArray())
                if (x.ValueKind == JsonValueKind.String) list.Add(x.GetString() ?? "");
        return list;
    }
}
