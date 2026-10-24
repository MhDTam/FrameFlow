using System.Text.Json;
using System.Text.Json.Serialization;
using Microsoft.Win32;

namespace FrameFlow.UI.Services;

/// <summary>UI-only preferences, stored in %LOCALAPPDATA%\FrameFlow\ui-settings.json.</summary>
public sealed class UiSettings
{
    public bool StartMinimized { get; set; }
    public bool ShowAdvancedStats { get; set; }
    public string LastGamePath { get; set; } = "";

    [JsonIgnore] public bool StartWithWindows
    {
        get => StartupRegistration.IsEnabled();
        set => StartupRegistration.SetEnabled(value, StartMinimized);
    }

    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };

    public static UiSettings Load()
    {
        try
        {
            if (File.Exists(AppInfo.UiSettingsFile))
            {
                var settings = JsonSerializer.Deserialize<UiSettings>(File.ReadAllText(AppInfo.UiSettingsFile)) ?? new UiSettings();
                if (string.IsNullOrEmpty(settings.LastGamePath)) settings.LastGamePath = FindLatestGameExe();
                return settings;
            }
        }
        catch (Exception ex) when (ex is IOException or JsonException or UnauthorizedAccessException)
        {
            Log.Warn($"ui-settings.json unreadable, using defaults: {ex.Message}");
        }
        return new UiSettings { LastGamePath = FindLatestGameExe() };
    }

    private static string FindLatestGameExe()
    {
        try
        {
            foreach (string profile in Directory.EnumerateFiles(AppInfo.ProfilesDir, "*.json")
                         .OrderByDescending(File.GetLastWriteTimeUtc))
            {
                using var doc = JsonDocument.Parse(File.ReadAllText(profile));
                if (doc.RootElement.TryGetProperty("exePath", out var value) && value.ValueKind == JsonValueKind.String)
                {
                    string? path = value.GetString();
                    if (!string.IsNullOrEmpty(path) && File.Exists(path)) return path;
                }
            }
        }
        catch (Exception ex) when (ex is IOException or JsonException or UnauthorizedAccessException or DirectoryNotFoundException)
        {
            Log.Warn($"Could not load recent game: {ex.Message}");
        }
        return "";
    }

    public void Save()
    {
        try
        {
            Directory.CreateDirectory(AppInfo.DataRoot);
            string tmp = AppInfo.UiSettingsFile + ".tmp";
            File.WriteAllText(tmp, JsonSerializer.Serialize(this, JsonOptions));
            File.Move(tmp, AppInfo.UiSettingsFile, overwrite: true);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            Log.Warn($"Could not save ui-settings.json: {ex.Message}");
        }
    }
}

/// <summary>Per-user autostart via HKCU\...\Run. No administrator rights required.</summary>
public static class StartupRegistration
{
    private const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string ValueName = "FrameFlow";

    public static bool IsEnabled()
    {
        using var key = Registry.CurrentUser.OpenSubKey(RunKey);
        return key?.GetValue(ValueName) is string s && s.Contains(Path.GetFileName(AppInfo.UiExePath), StringComparison.OrdinalIgnoreCase);
    }

    public static void SetEnabled(bool enabled, bool minimized)
    {
        using var key = Registry.CurrentUser.CreateSubKey(RunKey);
        if (enabled)
            key.SetValue(ValueName, $"\"{AppInfo.UiExePath}\"" + (minimized ? " --minimized" : ""));
        else if (key.GetValue(ValueName) is not null)
            key.DeleteValue(ValueName);
    }
}
