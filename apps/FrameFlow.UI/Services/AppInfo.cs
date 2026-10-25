using System.Reflection;

namespace FrameFlow.UI.Services;

public static class AppInfo
{
    public static string Version { get; } =
        Assembly.GetExecutingAssembly().GetName().Version is { } v ? $"{v.Major}.{v.Minor}.{v.Build}" : "1.0.1";

    public static string ExeDir { get; } = AppContext.BaseDirectory;
    public static string EnginePath => Path.Combine(ExeDir, "FrameFlow.Engine.exe");
    public static string TestRendererPath => Path.Combine(ExeDir, "FrameFlow.TestRenderer.exe");
    public static string LauncherPath => Path.Combine(ExeDir, "FrameFlow.Launch.exe");
    public static string UiExePath => Environment.ProcessPath ?? Path.Combine(ExeDir, "FrameFlow.UI.exe");

    public static bool IsPortable { get; }
    public static string DataRoot { get; }

    static AppInfo()
    {
        string? envOverride = Environment.GetEnvironmentVariable("FRAMEFLOW_DATA_DIR");
        string? envPortable = Environment.GetEnvironmentVariable("FRAMEFLOW_PORTABLE");

        bool hasPortableMarker =
            File.Exists(Path.Combine(ExeDir, ".portable")) ||
            File.Exists(Path.Combine(ExeDir, "portable")) ||
            File.Exists(Path.Combine(ExeDir, "portable.txt")) ||
            Directory.Exists(Path.Combine(ExeDir, "data"));

        bool isPortableEnv = !string.IsNullOrEmpty(envPortable) &&
                             envPortable != "0" &&
                             !envPortable.Equals("false", StringComparison.OrdinalIgnoreCase);

        if (!string.IsNullOrWhiteSpace(envOverride))
        {
            DataRoot = Path.GetFullPath(envOverride);
            IsPortable = isPortableEnv || hasPortableMarker;
        }
        else if (isPortableEnv || hasPortableMarker)
        {
            IsPortable = true;
            DataRoot = Path.Combine(ExeDir, "data");
        }
        else
        {
            IsPortable = false;
            DataRoot = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "FrameFlow");
        }

        if (IsPortable)
        {
            Environment.SetEnvironmentVariable("FRAMEFLOW_PORTABLE", "1");
            Environment.SetEnvironmentVariable("FRAMEFLOW_DATA_DIR", DataRoot);
        }

        try
        {
            Directory.CreateDirectory(DataRoot);
        }
        catch { }
    }

    public static string LogsDir => Path.Combine(DataRoot, "Logs");
    public static string ProfilesDir => Path.Combine(DataRoot, "Profiles");
    public static string UiSettingsFile => Path.Combine(DataRoot, "ui-settings.json");
}
