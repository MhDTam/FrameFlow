using System.Text;

namespace FrameFlow.UI.Services;

/// <summary>Structured, append-only UI log at %LOCALAPPDATA%\FrameFlow\Logs\ui.log.</summary>
public static class Log
{
    private static readonly object Gate = new();
    private static readonly string FilePath;

    static Log()
    {
        Directory.CreateDirectory(AppInfo.LogsDir);
        FilePath = Path.Combine(AppInfo.LogsDir, "ui.log");
        try
        {
            var fi = new FileInfo(FilePath);
            if (fi.Exists && fi.Length > 4 * 1024 * 1024) File.Move(FilePath, FilePath + ".old", overwrite: true);
        }
        catch (IOException) { }
    }

    public static void Info(string message) => Write("INFO", message);
    public static void Warn(string message) => Write("WARN", message);
    public static void Error(string message) => Write("ERROR", message);

    private static void Write(string level, string message)
    {
        string line = $"{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff} [{level}] {message}{Environment.NewLine}";
        lock (Gate)
        {
            try { File.AppendAllText(FilePath, line, Encoding.UTF8); }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }
    }
}
