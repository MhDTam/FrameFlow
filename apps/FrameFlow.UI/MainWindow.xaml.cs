using System.Globalization;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.WindowsRuntime;
using System.Text.Json;
using FrameFlow.UI.Services;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.Graphics;
using Windows.Graphics.Imaging;
using Windows.Storage.Pickers;
using Windows.System;
using WinRT.Interop;

namespace FrameFlow.UI;

public sealed partial class MainWindow : Window
{
    private const int DesignWidth = 1080;
    private const int DesignHeight = 760;

    private readonly EngineClient _engine;
    private readonly UiSettings _uiSettings = UiSettings.Load();
    private EngineStatus? _status;
    private Telemetry? _telemetry;
    private string? _iconForExe;
    private double _pendingFps = double.NaN;       // optimistic value while the engine confirms
    private DateTime _pendingFpsUntil = DateTime.MinValue;

    public MainWindow()
    {
        InitializeComponent();
        ConfigureWindow();

        VersionText.Text = AppInfo.IsPortable ? $"v{AppInfo.Version} (portable)" : $"v{AppInfo.Version}";
        Slider.ValueChanging += v => FpsValue.Text = FormatFps(v);
        Slider.ValueCommitted += v => _ = CommitFps(v);
        SettingsPage.BackRequested += ShowMain;
        AdvancedPage.BackRequested += ShowMain;

        _engine = new EngineClient(DispatcherQueue, autoStartEngine: !App.Options.NoEngine);
        _engine.MessageReceived += OnEngineMessage;
        _engine.ConnectionChanged += OnEngineConnection;
        SettingsPage.Attach(_engine, _uiSettings);
        AdvancedPage.Attach(_engine);

        ApplyDisconnectedState();
        ShowIdleGame();
        _engine.Start();

        switch (App.Options.InitialView)
        {
            case "settings": ShowPage(SettingsPage); break;
            case "advanced": ShowPage(AdvancedPage); break;
        }
        if (App.Options.SnapshotPath is { } snap) _ = SnapshotAndExit(snap, App.Options.SnapshotDelayMs);
        Closed += OnWindowClosed;
    }

    // ------------------------------------------------------------------ window chrome

    [DllImport("user32.dll")]
    private static extern uint GetDpiForWindow(IntPtr hwnd);

    private void ConfigureWindow()
    {
        ExtendsContentIntoTitleBar = true;
        SetTitleBar(DragRegion);

        IntPtr hwnd = WindowNative.GetWindowHandle(this);
        AppWindow app = AppWindow.GetFromWindowId(Win32Interop.GetWindowIdFromWindow(hwnd));
        app.Title = "FrameFlow";
        string icon = Path.Combine(AppInfo.ExeDir, "Assets", "FrameFlow.ico");
        if (File.Exists(icon)) app.SetIcon(icon);

        if (app.Presenter is OverlappedPresenter p)
        {
            p.IsResizable = false;
            p.IsMaximizable = false;
            p.SetBorderAndTitleBar(true, false);
            if (App.Options.StartMinimized) p.Minimize();
        }

        double scale = GetDpiForWindow(hwnd) / 96.0;
        var area = DisplayArea.GetFromWindowId(app.Id, DisplayAreaFallback.Primary).WorkArea;
        var size = new SizeInt32(Math.Min((int)(DesignWidth * scale), area.Width),
                                 Math.Min((int)(DesignHeight * scale), area.Height));
        app.MoveAndResize(new RectInt32(area.X + (area.Width - size.Width) / 2, area.Y + (area.Height - size.Height) / 2,
                                        size.Width, size.Height));
    }

    private void OnMinimize(object sender, RoutedEventArgs e)
    {
        if (AppWindow.Presenter is OverlappedPresenter p) p.Minimize();
    }

    private void OnClose(object sender, RoutedEventArgs e) => Close();

    // Until tray support lands (Phase 5) closing the window exits FrameFlow completely, so the engine
    // is asked to stop; it releases every game (pacing off) before exiting.
    private void OnWindowClosed(object sender, WindowEventArgs args)
    {
        if (App.Options.SnapshotPath is null && _engine.IsConnected)
        {
            try { _engine.Shutdown().Wait(TimeSpan.FromMilliseconds(400)); }
            catch (AggregateException) { }
        }
        _engine.Dispose();
    }

    // ------------------------------------------------------------------ engine messages

    private void OnEngineConnection(bool connected)
    {
        if (!connected) ApplyDisconnectedState();
        else
        {
            StatusDot.Fill = Res<Brush>("FfGreenBrush");
            StatusText.Text = "Running";
            EngineDot.Fill = Res<Brush>("FfGreenBrush");
            EngineText.Text = "Ready";
        }
    }

    private void ApplyDisconnectedState()
    {
        var amber = new SolidColorBrush(Windows.UI.Color.FromArgb(0xFF, 0xE0, 0xB4, 0x5A));
        StatusDot.Fill = amber;
        StatusText.Text = "Starting";
        EngineDot.Fill = amber;
        EngineText.Text = "Connecting to engine…";
        PacingSwitch.IsEnabled = false;
    }

    private void OnEngineMessage(JsonElement msg)
    {
        switch (msg.Str("type"))
        {
            case "EngineStatus":
                _status = EngineStatus.Parse(msg);
                ApplyStatus(_status);
                SettingsPage.Apply(_status);
                AdvancedPage.Apply(_status, _telemetry);
                break;
            case "TelemetryUpdate":
                _telemetry = Telemetry.Parse(msg);
                ApplyTelemetry(_telemetry);
                if (AdvancedPage.Visibility == Visibility.Visible) AdvancedPage.Apply(_status, _telemetry);
                break;
            case "Ack":
                if (!msg.Bool("ok")) Log.Warn($"Engine rejected request {msg.Int("id")}: {msg.Str("error")}");
                break;
            case "Error":
                Log.Warn($"Engine error {msg.Str("code")}: {msg.Str("message")}");
                break;
        }
    }

    private void ApplyStatus(EngineStatus s)
    {
        // FPS: keep the user's optimistic value for a moment so the number never jumps back while dragging.
        double fps = s.FpsLimit;
        if (!double.IsNaN(_pendingFps) && DateTime.UtcNow < _pendingFpsUntil && Math.Abs(_pendingFps - fps) > 0.001) fps = _pendingFps;
        else _pendingFps = double.NaN;
        if (FpsEditor.Visibility != Visibility.Visible) FpsValue.Text = FormatFps(fps);
        Slider.Value = fps;

        ModeText.Text = s.PacingMode;
        MiniOverlayButton.IsChecked = s.Settings.MiniOverlay;
        ModeEfficiency.IsChecked = s.PacingMode == "Efficiency";
        ModeBalanced.IsChecked = s.PacingMode == "Balanced";
        ModePrecision.IsChecked = s.PacingMode == "Precision";
        LimitMethodText.Text = s.LimitMethodPreference switch { "frameflow" => "FrameFlow", "driver" => "NVIDIA", _ => "Off" };
        MethodAuto.IsChecked = s.LimitMethodPreference == "auto";
        MethodFrameFlow.IsChecked = s.LimitMethodPreference == "frameflow";
        MethodDriver.IsChecked = s.LimitMethodPreference == "driver";
        LimitMethodButton.IsEnabled = s.Game is not null && s.Integration.Status != "Protected";
        MethodFrameFlow.IsEnabled = s.Game is not null && s.Integration.Status != "Protected" && !s.Integration.HookBlocked;
        MethodDriver.IsEnabled = s.Nvidia.Available && s.Game is not null && s.Integration.Status != "Protected";
        RefreshText.Text = s.RefreshHz > 0 ? $"{s.RefreshHz:0.##} Hz" : "—";

        ApplySuggestions(s, fps);
        ApplyGame(s);

        bool canToggle = s.Game is not null && s.LimitMethodPreference != "auto" &&
                         !(s.Integration.HookBlocked && s.LimitMethodPreference == "frameflow") &&
                         s.Integration.Status is not ("Protected" or "AccessDenied");
        PacingSwitch.IsEnabled = canToggle;
        PacingSwitch.IsChecked = canToggle && s.PacingEnabled;
        (PacingStatus.Text, PacingStatus.Foreground) = s.Integration.Status switch
        {
            "Active" => ("Active", Res<Brush>("FfGreenTextBrush")),
            "Standby" => ("Disabled", Res<Brush>("FfInk3Brush")),
            "Protected" => ("Unavailable", Res<Brush>("FfAmberTextBrush")),
            "NotAvailable" when s.LimitMethodPreference == "auto" => ("Choose limiter", Res<Brush>("FfInk3Brush")),
            "NotAvailable" when s.Integration.HookBlocked && s.LimitMethodPreference == "frameflow" => ("Unavailable", Res<Brush>("FfInk3Brush")),
            "NotAvailable" when canToggle => (s.PacingEnabled ? "Connecting" : "Off", Res<Brush>("FfInk3Brush")),
            "NotAvailable" or "AccessDenied" => ("Unavailable", Res<Brush>("FfInk3Brush")),
            _ => ("Waiting for game", Res<Brush>("FfInk3Brush")),
        };
        ToolTipService.SetToolTip(PacingSwitch, canToggle ? null : s.Integration.Message);
    }

    private void ApplyGame(EngineStatus s)
    {
        if (s.Game is not { } g)
        {
            ShowIdleGame();
            return;
        }

        LaunchGameButton.IsEnabled = false;
        LaunchGameButton.Visibility = Visibility.Collapsed;
        ProtectedRiskButton.Visibility = s.Integration.AntiCheat is null ? Visibility.Collapsed : Visibility.Visible;
        ProtectedRiskButton.Content = s.Integration.RiskAccepted ? "Disable risk mode" : "Play at your own risk";
        if (!string.IsNullOrWhiteSpace(g.ExePath) && !string.Equals(_uiSettings.LastGamePath, g.ExePath, StringComparison.OrdinalIgnoreCase))
        {
            _uiSettings.LastGamePath = g.ExePath;
            _uiSettings.Save();
        }
        GameName.Text = g.Name;
        ChangeGameText.Text = "Change Game";
        GameExe.Text = g.Exe;
        GameMetaDot.Visibility = Visibility.Visible;
        GameStatus.Visibility = Visibility.Visible;

        bool protectedGame = s.Integration.AntiCheat is not null;
        GameStatusText.Text = protectedGame ? (s.Integration.RiskAccepted ? "Protected · risk accepted" : "Protected game detected")
                              : s.DriverLimited ? (g.Manual ? "Selected manually · driver limiter" : "Detected · driver limiter")
                              : g.Manual ? "Selected manually" : "Game Detected";
        var fg = protectedGame ? Res<Brush>("FfAmberTextBrush") : Res<Brush>("FfGreenTextBrush");
        GameStatusText.Foreground = fg;
        GameStatusGlyph.Foreground = fg;
        GameStatusGlyph.Glyph = protectedGame ? "" : "";
        GameStatusDisc.Fill = new SolidColorBrush(protectedGame
            ? Windows.UI.Color.FromArgb(0x26, 0xE0, 0xB4, 0x5A)
            : Windows.UI.Color.FromArgb(0x26, 0x5B, 0xBF, 0x8A));

        // A note when limiting is unavailable, or when the driver limiter works but real-game metrics
        // need administrator rights.
        string? note = !s.Integration.CanPace && !string.IsNullOrEmpty(s.Integration.Message) ? s.Integration.Message
                       : s.DriverLimited && s.MeasurementNeedsAdmin ? "Frametime metrics need FrameFlow running as administrator."
                       : null;
        IntegrationNote.Text = note ?? "";
        IntegrationNote.Visibility = note is null ? Visibility.Collapsed : Visibility.Visible;

        if (!string.Equals(_iconForExe, g.ExePath, StringComparison.OrdinalIgnoreCase))
        {
            _iconForExe = g.ExePath;
            _ = LoadIcon(g.ExePath);
        }
    }

    private void ShowIdleGame()
    {
        string path = _uiSettings.LastGamePath;
        bool hasRecentGame = !string.IsNullOrWhiteSpace(path) && File.Exists(path);
        GameName.Text = "No game running";
        GameExe.Text = hasRecentGame ? $"Last selected: {Path.GetFileNameWithoutExtension(path)}" : "Start a game; FrameFlow will detect it automatically";
        ChangeGameText.Text = "Choose Game";
        LaunchGameButton.IsEnabled = hasRecentGame;
        LaunchGameButton.Visibility = hasRecentGame ? Visibility.Visible : Visibility.Collapsed;
        ProtectedRiskButton.Visibility = Visibility.Collapsed;
        GameMetaDot.Visibility = Visibility.Collapsed;
        GameStatus.Visibility = Visibility.Collapsed;
        IntegrationNote.Visibility = Visibility.Collapsed;
        GameIcon.Visibility = Visibility.Collapsed;
        GameGlyph.Visibility = Visibility.Visible;
        _iconForExe = null;
    }

    private async Task LoadIcon(string exePath)
    {
        BitmapImage? img = await IconExtractor.FromExeAsync(exePath);
        if (!string.Equals(_iconForExe, exePath, StringComparison.OrdinalIgnoreCase)) return;
        GameIcon.Source = img;
        GameIcon.Visibility = img is null ? Visibility.Collapsed : Visibility.Visible;
        GameGlyph.Visibility = img is null ? Visibility.Visible : Visibility.Collapsed;
    }

    private void ApplySuggestions(EngineStatus s, double currentFps)
    {
        SuggestionChips.Items.Clear();
        if (s.RefreshHz <= 0 || s.Presets.Count == 0)
        {
            SuggestionRow.Visibility = Visibility.Collapsed;
            return;
        }
        SuggestionLabel.Text = $"Suggested for {s.RefreshHz:0.##} Hz";
        foreach (double p in s.Presets)
        {
            bool current = Math.Abs(p - currentFps) < 0.01;
            var chip = new Button
            {
                Content = FormatFps(p),
                Padding = new Thickness(9, 2, 9, 3),
                MinWidth = 0,
                FontSize = 12,
                CornerRadius = new CornerRadius(10),
                BorderThickness = new Thickness(1),
                BorderBrush = Res<Brush>("FfLineBrush"),
                Background = current ? Res<Brush>("FfCyanBrush") : new SolidColorBrush(Colors.Transparent),
                Foreground = current ? Res<Brush>("FfTealDeepBrush") : Res<Brush>("FfInk2Brush"),
                Tag = p,
            };
            chip.Click += (sender, args) => { if (sender is Button { Tag: double v }) _ = CommitFps(v); };
            SuggestionChips.Items.Add(chip);
        }
        SuggestionRow.Visibility = Visibility.Collapsed;
    }

    private void ApplyTelemetry(Telemetry t)
    {
        if (!t.Live || t.CurrentFps <= 0)
        {
            MetricFrametime.Text = "—";
            MetricFps.Text = "—";
            MetricLow.Text = "—";
            GraphAverage.Text = "—";
            Graph.SetData(Array.Empty<double>(), _status is null ? 0 : 1000.0 / _status.FpsLimit, t.HistorySeconds);
            return;
        }
        MetricFrametime.Text = t.FrametimeMs.ToString("0.00", CultureInfo.InvariantCulture);
        MetricFps.Text = t.CurrentFps.ToString("0.00", CultureInfo.InvariantCulture);
        MetricLow.Text = t.Low1Fps.ToString("0.0", CultureInfo.InvariantCulture);
        GraphAverage.Text = $"{t.AvgFrametimeMs.ToString("0.00", CultureInfo.InvariantCulture)} ms";
        double target = t.PacingActive && t.AppliedFps > 0 ? 1000.0 / t.AppliedFps : 0;
        Graph.SetData(t.GraphMean, target, t.HistorySeconds);
    }

    // ------------------------------------------------------------------ user actions

    private async Task CommitFps(double fps)
    {
        fps = Math.Clamp(Math.Round(fps, 3), 10, 1000);
        _pendingFps = fps;
        _pendingFpsUntil = DateTime.UtcNow.AddSeconds(1.5);
        FpsValue.Text = FormatFps(fps);
        Slider.Value = fps;
        await _engine.SetFpsLimit(fps);
    }

    private void OnPacingSwitchClick(object sender, RoutedEventArgs e) => _ = _engine.SetPacing(PacingSwitch.IsChecked == true);

    private void OnModeClick(object sender, RoutedEventArgs e)
    {
        if (sender is RadioMenuFlyoutItem { Tag: string mode })
        {
            ModeText.Text = mode;
            _ = _engine.SetPacingMode(mode);
        }
    }

    private void OnLimitMethodClick(object sender, RoutedEventArgs e)
    {
        if (sender is RadioMenuFlyoutItem { Tag: string method })
            _ = _engine.SetLimitMethod(method);
    }

    private void OnMiniOverlayClick(object sender, RoutedEventArgs e) =>
        _ = _engine.SetEngineSettings(new System.Text.Json.Nodes.JsonObject { ["miniOverlay"] = MiniOverlayButton.IsChecked == true });

    private async void OnProtectedRisk(object sender, RoutedEventArgs e)
    {
        if (_status?.Integration.AntiCheat is null) return;
        if (_status.Integration.RiskAccepted) {
            await _engine.SetProtectedRisk(false);
            return;
        }
        var dialog = new ContentDialog
        {
            XamlRoot = Root.XamlRoot,
            Title = "Protected game",
            Content = $"{_status.Integration.AntiCheat} was detected. Loading FrameFlow into this game may be blocked, crash the game, or violate its rules and risk an account ban. FrameFlow does not bypass anti-cheat. Enable for this game only?",
            PrimaryButtonText = "Play at my own risk",
            CloseButtonText = "Cancel",
            DefaultButton = ContentDialogButton.Close,
        };
        if (await dialog.ShowAsync() == ContentDialogResult.Primary)
            await _engine.SetProtectedRisk(true);
    }

    private void OnEditFps(object sender, RoutedEventArgs e)
    {
        FpsEditor.Text = FpsValue.Text;
        FpsValueButton.Visibility = Visibility.Collapsed;
        FpsEditor.Visibility = Visibility.Visible;
        FpsEditor.Focus(FocusState.Programmatic);
        FpsEditor.SelectAll();
    }

    private void OnFpsEditorKeyDown(object sender, KeyRoutedEventArgs e)
    {
        if (e.Key == VirtualKey.Enter) { EndFpsEdit(commit: true); e.Handled = true; }
        else if (e.Key == VirtualKey.Escape) { EndFpsEdit(commit: false); e.Handled = true; }
    }

    private void OnFpsEditorLostFocus(object sender, RoutedEventArgs e) => EndFpsEdit(commit: true);

    private void EndFpsEdit(bool commit)
    {
        if (FpsEditor.Visibility != Visibility.Visible) return;
        FpsEditor.Visibility = Visibility.Collapsed;
        FpsValueButton.Visibility = Visibility.Visible;
        string text = FpsEditor.Text.Trim().Replace(',', '.');
        if (commit && double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out double fps) && fps >= 10 && fps <= 1000)
            _ = CommitFps(fps);
        else if (_status is not null)
            FpsValue.Text = FormatFps(_status.FpsLimit);
    }

    private void OnChangeGameOpening(object sender, object e)
    {
        ChangeGameFlyout.Items.Clear();
        var running = _status?.Candidates.Where(c => c.Score > 0 && !string.IsNullOrEmpty(c.ExePath)).Take(6).ToList() ?? new();
        if (running.Count > 0)
        {
            ChangeGameFlyout.Items.Add(new MenuFlyoutItem { Text = "Running applications", IsEnabled = false });
            foreach (var c in running)
            {
                var item = new MenuFlyoutItem
                {
                    Text = string.IsNullOrWhiteSpace(c.Title) ? c.Exe : $"{c.Title}  ·  {c.Exe}",
                    Tag = c.ExePath,
                    Icon = new FontIcon { Glyph = _status?.Game?.Pid == c.Pid ? "" : "" },
                };
                item.Click += (s, args) => { if (s is MenuFlyoutItem { Tag: string path }) _ = _engine.SelectGame(path); };
                ChangeGameFlyout.Items.Add(item);
            }
            ChangeGameFlyout.Items.Add(new MenuFlyoutSeparator());
        }
        var browse = new MenuFlyoutItem { Text = "Browse for a game…", Icon = new FontIcon { Glyph = "" } };
        browse.Click += async (_, _) => await BrowseForGame();
        ChangeGameFlyout.Items.Add(browse);

        if (_status?.Game is { Manual: true } g)
        {
            var clear = new MenuFlyoutItem { Text = "Stop using manual selection", Icon = new FontIcon { Glyph = "" } };
            clear.Click += (_, _) => _ = _engine.ClearManualGame(g.ProfileId);
            ChangeGameFlyout.Items.Add(clear);
        }
        if (_status?.Game is null && !string.IsNullOrWhiteSpace(_uiSettings.LastGamePath))
        {
            var forget = new MenuFlyoutItem { Text = "Forget last selected game", Icon = new FontIcon { Glyph = "" } };
            forget.Click += (_, _) =>
            {
                _uiSettings.LastGamePath = "";
                _uiSettings.Save();
                if (_status is { Game: null } status) ApplyGame(status);
            };
            ChangeGameFlyout.Items.Add(forget);
        }
    }

    private async Task BrowseForGame()
    {
        var picker = new FileOpenPicker { SuggestedStartLocation = PickerLocationId.ComputerFolder, ViewMode = PickerViewMode.List };
        picker.FileTypeFilter.Add(".exe");
        InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(this));
        var file = await picker.PickSingleFileAsync();
        if (file is not null)
        {
            _uiSettings.LastGamePath = file.Path;
            _uiSettings.Save();
            if (_status is { Game: null } status) ApplyGame(status);
            await _engine.SelectGame(file.Path);
        }
    }

    private async void OnLaunchGame(object sender, RoutedEventArgs e)
    {
        string path = _uiSettings.LastGamePath;
        if (!File.Exists(path) || !File.Exists(AppInfo.LauncherPath))
        {
            IntegrationNote.Text = "Game or FrameFlow launcher was not found. Choose the game again.";
            IntegrationNote.Visibility = Visibility.Visible;
            return;
        }
        LaunchGameButton.IsEnabled = false;
        try
        {
            var start = new ProcessStartInfo(AppInfo.LauncherPath)
            {
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardError = true,
                WorkingDirectory = Path.GetDirectoryName(path)!,
            };
            if (AppInfo.IsPortable)
            {
                start.EnvironmentVariables["FRAMEFLOW_PORTABLE"] = "1";
            }
            start.EnvironmentVariables["FRAMEFLOW_DATA_DIR"] = AppInfo.DataRoot;
            start.ArgumentList.Add(path);
            using var launcher = Process.Start(start) ?? throw new InvalidOperationException("Launcher did not start");
            string error = await launcher.StandardError.ReadToEndAsync();
            await launcher.WaitForExitAsync();
            if (launcher.ExitCode == 7)
            {
                IntegrationNote.Text = error.Trim();
                IntegrationNote.Visibility = Visibility.Visible;
                Log.Info($"Protected game started normally: {error.Trim()}");
                LaunchGameButton.IsEnabled = true;
            }
            else if (launcher.ExitCode == 8)
            {
                IntegrationNote.Text = error.Trim();
                IntegrationNote.Visibility = Visibility.Visible;
                Log.Info($"Game started without DXGI hook: {error.Trim()}");
                LaunchGameButton.IsEnabled = true;
            }
            else if (launcher.ExitCode != 0)
            {
                IntegrationNote.Text = string.IsNullOrWhiteSpace(error) ? "FrameFlow could not start this game." : error.Trim();
                IntegrationNote.Visibility = Visibility.Visible;
                Log.Warn($"Game launcher exited with code {launcher.ExitCode}: {error.Trim()}");
                LaunchGameButton.IsEnabled = true;
            }
        }
        catch (Exception ex) when (ex is IOException or InvalidOperationException or System.ComponentModel.Win32Exception)
        {
            IntegrationNote.Text = $"Could not launch game: {ex.Message}";
            IntegrationNote.Visibility = Visibility.Visible;
            LaunchGameButton.IsEnabled = true;
            Log.Error($"Could not launch game: {ex}");
        }
    }

    // ------------------------------------------------------------------ navigation

    private void OnOpenSettings(object sender, RoutedEventArgs e) => ShowPage(SettingsPage);
    private void OnOpenAdvanced(object sender, RoutedEventArgs e) => ShowPage(AdvancedPage);

    private void ShowPage(FrameworkElement page)
    {
        MainView.Visibility = Visibility.Collapsed;
        SettingsPage.Visibility = page == SettingsPage ? Visibility.Visible : Visibility.Collapsed;
        AdvancedPage.Visibility = page == AdvancedPage ? Visibility.Visible : Visibility.Collapsed;
        if (page == AdvancedPage) AdvancedPage.Apply(_status, _telemetry);
    }

    private void ShowMain()
    {
        SettingsPage.Visibility = Visibility.Collapsed;
        AdvancedPage.Visibility = Visibility.Collapsed;
        MainView.Visibility = Visibility.Visible;
    }

    // ------------------------------------------------------------------ helpers

    private static T Res<T>(string key) => (T)Application.Current.Resources[key];

    private static string FormatFps(double fps) =>
        Math.Abs(fps - Math.Round(fps)) < 0.001 ? fps.ToString("0", CultureInfo.InvariantCulture) : fps.ToString("0.##", CultureInfo.InvariantCulture);

    /// <summary>Renders the window content to a PNG (automated visual verification), then exits.</summary>
    private async Task SnapshotAndExit(string path, int delayMs)
    {
        await Task.Delay(delayMs);
        try
        {
            var rtb = new RenderTargetBitmap();
            await rtb.RenderAsync(Root);
            var pixels = await rtb.GetPixelsAsync();
            string full = Path.GetFullPath(path);
            Directory.CreateDirectory(Path.GetDirectoryName(full)!);
            using (var fs = File.Create(full))
            {
                var encoder = await BitmapEncoder.CreateAsync(BitmapEncoder.PngEncoderId, fs.AsRandomAccessStream());
                double dpi = 96.0 * (Root.XamlRoot?.RasterizationScale ?? 1.0);
                encoder.SetPixelData(BitmapPixelFormat.Bgra8, BitmapAlphaMode.Premultiplied,
                                     (uint)rtb.PixelWidth, (uint)rtb.PixelHeight, dpi, dpi, pixels.ToArray());
                await encoder.FlushAsync();
            }
            Log.Info($"Snapshot written to {full}");
        }
        catch (Exception ex)
        {
            Log.Error($"Snapshot failed: {ex}");
        }
        Application.Current.Exit();
    }
}
