namespace FrameFlow.UI.Services;

/// <summary>Command-line switches. Snapshot options exist for automated visual verification.</summary>
public sealed class LaunchOptions
{
    public bool StartMinimized { get; private set; }

    /// <summary>--snapshot &lt;file.png&gt;: render the window to a PNG after a delay, then exit.</summary>
    public string? SnapshotPath { get; private set; }
    public int SnapshotDelayMs { get; private set; } = 4000;

    /// <summary>--view main|settings|advanced: which page to show first (used with --snapshot).</summary>
    public string InitialView { get; private set; } = "main";

    /// <summary>--no-engine: do not start FrameFlow.Engine automatically.</summary>
    public bool NoEngine { get; private set; }

    /// <summary>--portable: force portable mode (uses data\ next to the executable).</summary>
    public bool Portable { get; private set; }

    public static LaunchOptions Parse(string[] args)
    {
        var o = new LaunchOptions();
        for (int i = 1; i < args.Length; i++)
        {
            string a = args[i];
            string Next() => i + 1 < args.Length ? args[++i] : string.Empty;
            switch (a)
            {
                case "--minimized": o.StartMinimized = true; break;
                case "--portable":
                    o.Portable = true;
                    Environment.SetEnvironmentVariable("FRAMEFLOW_PORTABLE", "1");
                    break;
                case "--data-dir":
                    string d = Next();
                    if (!string.IsNullOrWhiteSpace(d))
                    {
                        Environment.SetEnvironmentVariable("FRAMEFLOW_DATA_DIR", d);
                    }
                    break;
                case "--snapshot": o.SnapshotPath = Next(); break;
                case "--snapshot-delay":
                    if (int.TryParse(Next(), out int sd)) o.SnapshotDelayMs = Math.Clamp(sd, 500, 60000);
                    break;
                case "--view": o.InitialView = Next().ToLowerInvariant(); break;
                case "--no-engine": o.NoEngine = true; break;
            }
        }
        return o;
    }
}
