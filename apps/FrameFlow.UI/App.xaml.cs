using FrameFlow.UI.Services;
using Microsoft.UI.Xaml;

namespace FrameFlow.UI;

public partial class App : Application
{
    private MainWindow? _window;

    public static LaunchOptions Options { get; private set; } = new();

    public App()
    {
        InitializeComponent();
        UnhandledException += (_, e) =>
        {
            Log.Error($"Unhandled UI exception: {e.Exception}");
            e.Handled = true; // never take the whole app down for a UI glitch; the engine keeps pacing
        };
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        Options = LaunchOptions.Parse(Environment.GetCommandLineArgs());
        Log.Info($"FrameFlow.UI {AppInfo.Version} starting");
        _window = new MainWindow();
        _window.Activate();
    }
}
