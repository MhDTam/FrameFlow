using System.Diagnostics;
using System.Text.Json.Nodes;
using FrameFlow.UI.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Windows.System;

namespace FrameFlow.UI.Views;

public sealed partial class SettingsView : UserControl
{
    private EngineClient? _engine;
    private UiSettings? _ui;
    private bool _applying;                   // suppress change events while filling from engine state
    private List<string> _exclusions = new();

    public event Action? BackRequested;

    public SettingsView()
    {
        InitializeComponent();
        AboutVersion.Text = AppInfo.IsPortable ? $"FrameFlow {AppInfo.Version} (Portable)" : $"FrameFlow {AppInfo.Version}";
        if (AppInfo.IsPortable)
        {
            AboutDescription.Text = $"Portable mode active. Data stored in: {AppInfo.DataRoot}";
        }
    }

    public void Attach(EngineClient engine, UiSettings ui)
    {
        _engine = engine;
        _ui = ui;
        _applying = true;
        StartWithWindows.IsOn = StartupRegistration.IsEnabled();
        StartMinimized.IsOn = ui.StartMinimized;
        _applying = false;
    }

    public void Apply(EngineStatus s)
    {
        _applying = true;
        AutoDetect.IsOn = s.Settings.AutoDetect;
        if (XamlRoot is null || !ReferenceEquals(FocusManager.GetFocusedElement(XamlRoot), DefaultFps)) DefaultFps.Value = s.Settings.DefaultFps;
        DefaultMode.SelectedItem = s.Settings.DefaultMode;
        TelemetryWindow.SelectedIndex = s.Settings.TelemetrySeconds >= 45 ? 1 : 0;
        _exclusions = s.Settings.ExtraExclusions.ToList();
        RenderExclusions();
        var builtIn = s.Settings.BuiltInExclusions;
        BuiltInHint.Text = builtIn.Count > 0
            ? $"Never treated as games. Built in: {string.Join(", ", builtIn.Take(6))} and {Math.Max(0, builtIn.Count - 6)} more."
            : "Never treated as games.";
        _applying = false;
    }

    private void RenderExclusions()
    {
        ExclusionList.Items.Clear();
        if (_exclusions.Count == 0)
        {
            ExclusionList.Items.Add(new TextBlock
            {
                Text = "No applications added yet.",
                FontSize = 12,
                Foreground = (Brush)Application.Current.Resources["FfInk3Brush"],
            });
            return;
        }
        foreach (string name in _exclusions)
        {
            var row = new Grid { Padding = new Thickness(12, 6, 6, 6), Margin = new Thickness(0, 0, 0, 6), CornerRadius = new CornerRadius(8),
                                 Background = (Brush)Application.Current.Resources["FfSoftFillBrush"] };
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            row.Children.Add(new TextBlock { Text = name, VerticalAlignment = VerticalAlignment.Center, FontSize = 13,
                                             Foreground = (Brush)Application.Current.Resources["FfInkBrush"] });
            var remove = new Button
            {
                Content = new FontIcon { Glyph = "", FontSize = 10 },
                Style = (Style)Application.Current.Resources["FfGhostButton"],
                Padding = new Thickness(8),
                Tag = name,
            };
            AutomationPropertiesHelper.SetName(remove, $"Remove {name}");
            remove.Click += (sender, _) =>
            {
                if (sender is Button { Tag: string n }) { _exclusions.Remove(n); PushExclusions(); }
            };
            Grid.SetColumn(remove, 1);
            row.Children.Add(remove);
            ExclusionList.Items.Add(row);
        }
    }

    private void PushExclusions()
    {
        RenderExclusions();
        var arr = new JsonArray();
        foreach (var e in _exclusions) arr.Add(e);
        _ = _engine?.SetEngineSettings(new JsonObject { ["extraExclusions"] = arr });
    }

    private void OnBack(object sender, RoutedEventArgs e) => BackRequested?.Invoke();

    private void OnStartWithWindows(object sender, RoutedEventArgs e)
    {
        if (_applying || _ui is null) return;
        try { StartupRegistration.SetEnabled(StartWithWindows.IsOn, _ui.StartMinimized); }
        catch (Exception ex) when (ex is UnauthorizedAccessException or System.Security.SecurityException)
        {
            Log.Warn($"Startup registration failed: {ex.Message}");
            _applying = true;
            StartWithWindows.IsOn = StartupRegistration.IsEnabled();
            _applying = false;
        }
    }

    private void OnStartMinimized(object sender, RoutedEventArgs e)
    {
        if (_applying || _ui is null) return;
        _ui.StartMinimized = StartMinimized.IsOn;
        _ui.Save();
        if (StartupRegistration.IsEnabled()) StartupRegistration.SetEnabled(true, _ui.StartMinimized);
    }

    private void OnAutoDetect(object sender, RoutedEventArgs e)
    {
        if (_applying) return;
        _ = _engine?.SetEngineSettings(new JsonObject { ["autoDetect"] = AutoDetect.IsOn });
    }

    private void OnDefaultFps(NumberBox sender, NumberBoxValueChangedEventArgs args)
    {
        if (_applying || double.IsNaN(args.NewValue)) return;
        double v = Math.Clamp(args.NewValue, 10, 1000);
        _ = _engine?.SetEngineSettings(new JsonObject { ["defaultFps"] = v });
    }

    private void OnDefaultMode(object sender, SelectionChangedEventArgs e)
    {
        if (_applying || DefaultMode.SelectedItem is not string mode) return;
        _ = _engine?.SetEngineSettings(new JsonObject { ["defaultMode"] = mode });
    }

    private void OnTelemetryWindow(object sender, SelectionChangedEventArgs e)
    {
        if (_applying || TelemetryWindow.SelectedIndex < 0) return;
        _ = _engine?.SetEngineSettings(new JsonObject { ["telemetrySeconds"] = TelemetryWindow.SelectedIndex == 1 ? 60 : 30 });
    }

    private void OnExclusionKeyDown(object sender, KeyRoutedEventArgs e)
    {
        if (e.Key != VirtualKey.Enter) return;
        AddExclusion();
        e.Handled = true;
    }

    private void OnAddExclusion(object sender, RoutedEventArgs e) => AddExclusion();

    private void AddExclusion()
    {
        string name = NewExclusion.Text.Trim().Trim('"');
        if (name.Length == 0) return;
        name = Path.GetFileName(name).ToLowerInvariant();
        if (!name.EndsWith(".exe", StringComparison.Ordinal)) name += ".exe";
        if (name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || _exclusions.Contains(name)) return;
        _exclusions.Add(name);
        NewExclusion.Text = string.Empty;
        PushExclusions();
    }

    private void OnOpenDataFolder(object sender, RoutedEventArgs e)
    {
        Directory.CreateDirectory(AppInfo.DataRoot);
        Process.Start(new ProcessStartInfo("explorer.exe", $"\"{AppInfo.DataRoot}\"") { UseShellExecute = true });
    }
}

internal static class AutomationPropertiesHelper
{
    public static void SetName(DependencyObject element, string name) =>
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(element, name);
}
