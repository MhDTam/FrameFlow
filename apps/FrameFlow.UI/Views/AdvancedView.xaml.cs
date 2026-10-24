using System.Diagnostics;
using System.Globalization;
using FrameFlow.UI.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace FrameFlow.UI.Views;

public sealed partial class AdvancedView : UserControl
{
    private EngineClient? _engine;
    private bool _applying;

    public event Action? BackRequested;

    public AdvancedView() => InitializeComponent();

    public void Attach(EngineClient engine) => _engine = engine;

    public void Apply(EngineStatus? s, Telemetry? t)
    {
        if (s is not null)
        {
            _applying = true;
            DebugLogging.IsOn = s.Settings.LogLevel is "DEBUG" or "TRACE";
            _applying = false;

            GameValue.Text = s.Game is { } g ? $"{g.Name} ({g.Exe}, PID {g.Pid})" : "No game";
            ApiValue.Text = s.Game?.Api ?? "—";
            MethodValue.Text = s.LimitMethod switch
            {
                "driver" => "NVIDIA driver limiter (no injection)",
                "frameflow" => "FrameFlow in-process pacer",
                _ => "None",
            };
            IntegrationValue.Text = string.IsNullOrEmpty(s.Integration.Message)
                ? s.Integration.Title
                : $"{s.Integration.Title}. {s.Integration.Message}";
            NvidiaValue.Text = s.Nvidia.Available
                ? $"{s.Nvidia.Gpu}" + (string.IsNullOrEmpty(s.Nvidia.DriverVersion) ? "" : $" (driver {s.Nvidia.DriverVersion})")
                : "Not present";
            MeasurementValue.Text = s.Measurement switch
            {
                "live" => "Live (ETW present monitor)",
                "needsAdmin" => "Run FrameFlow as administrator to measure real games",
                _ => "Off",
            };
            PrecisionValue.Text = s.LimitMethod == "driver"
                ? "Set by the NVIDIA driver"
                : s.PacingMode switch
                {
                    "Efficiency" => "Efficiency: wide timer sleep, short spin",
                    "Precision" => "Precision: longer final spin, tightest release",
                    _ => "Balanced: timer sleep, then yield and a short spin",
                };
            ReasonsValue.Text = s.Game is { Reasons.Count: > 0 } gg ? string.Join(" · ", gg.Reasons) : "—";
            DisplayValue.Text = s.DisplayWidth > 0 ? $"{s.DisplayWidth}×{s.DisplayHeight} @ {s.RefreshHz:0.###} Hz" : "—";
        }

        if (t is { Live: true, CurrentFps: > 0 })
        {
            var ci = CultureInfo.InvariantCulture;
            AvgFps.Text = t.AverageFps.ToString("0.00", ci);
            Lows.Text = $"{t.Low1Fps.ToString("0.0", ci)} / {t.Low01Fps.ToString("0.0", ci)} FPS";
            AvgFt.Text = $"{t.AvgFrametimeMs.ToString("0.000", ci)} ms";
            MinMax.Text = $"{t.MinFrametimeMs.ToString("0.000", ci)} / {t.MaxFrametimeMs.ToString("0.000", ci)} ms";
            Stddev.Text = $"±{t.StddevMs.ToString("0.000", ci)} ms (std. dev.)";
            Applied.Text = t.PacingActive ? $"{t.AppliedFps.ToString("0.###", ci)} FPS" : "Not limiting";
        }
        else
        {
            AvgFps.Text = Lows.Text = AvgFt.Text = MinMax.Text = Stddev.Text = Applied.Text = "—";
        }
    }

    private void OnBack(object sender, RoutedEventArgs e) => BackRequested?.Invoke();

    private void OnDebugLogging(object sender, RoutedEventArgs e)
    {
        if (_applying) return;
        _ = _engine?.SetLogLevel(DebugLogging.IsOn ? "DEBUG" : "INFO");
    }

    private static void OpenFolder(string path)
    {
        Directory.CreateDirectory(path);
        Process.Start(new ProcessStartInfo("explorer.exe", $"\"{path}\"") { UseShellExecute = true });
    }

    private void OnOpenLogs(object sender, RoutedEventArgs e) => OpenFolder(AppInfo.LogsDir);
    private void OnOpenProfiles(object sender, RoutedEventArgs e) => OpenFolder(AppInfo.ProfilesDir);

    private void OnLaunchRenderer(object sender, RoutedEventArgs e)
    {
        if (!File.Exists(AppInfo.TestRendererPath))
        {
            Log.Warn("Test renderer not found next to FrameFlow.UI.exe");
            return;
        }
        var psi = new ProcessStartInfo(AppInfo.TestRendererPath)
        {
            UseShellExecute = false,
            WorkingDirectory = AppInfo.ExeDir,
        };
        if (AppInfo.IsPortable)
        {
            psi.EnvironmentVariables["FRAMEFLOW_PORTABLE"] = "1";
        }
        psi.EnvironmentVariables["FRAMEFLOW_DATA_DIR"] = AppInfo.DataRoot;
        Process.Start(psi);
    }
}
