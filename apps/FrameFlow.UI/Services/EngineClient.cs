using System.Diagnostics;
using System.IO.Pipes;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.UI.Dispatching;

namespace FrameFlow.UI.Services;

/// <summary>
/// Named-pipe client for FrameFlow.Engine (protocol v1, newline-delimited JSON; see
/// shared/Protocols/ipc-protocol.md). Reconnects forever and starts the engine when it is not running.
/// All events are raised on the UI thread.
/// </summary>
public sealed class EngineClient : IDisposable
{
    public const int ProtocolVersion = 1;
    private const string PipeName = "FrameFlow.Engine.v1";
    private const int MaxLineBytes = 256 * 1024;

    private readonly DispatcherQueue _ui;
    private readonly CancellationTokenSource _cts = new();
    private readonly SemaphoreSlim _writeGate = new(1, 1);
    private readonly bool _autoStartEngine;
    private NamedPipeClientStream? _pipe;
    private StreamWriter? _writer;
    private DateTime _lastEngineLaunch = DateTime.MinValue;
    private int _nextId = 1;

    public event Action<JsonElement>? MessageReceived;
    public event Action<bool>? ConnectionChanged;
    public bool IsConnected { get; private set; }

    public EngineClient(DispatcherQueue ui, bool autoStartEngine)
    {
        _ui = ui;
        _autoStartEngine = autoStartEngine;
    }

    public void Start() => _ = Task.Run(() => RunAsync(_cts.Token));

    private async Task RunAsync(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            try
            {
                using var pipe = new NamedPipeClientStream(".", PipeName, PipeDirection.InOut, PipeOptions.Asynchronous);
                try
                {
                    await pipe.ConnectAsync(1500, ct).ConfigureAwait(false);
                }
                catch (TimeoutException)
                {
                    EnsureEngineRunning();
                    continue;
                }

                _pipe = pipe;
                _writer = new StreamWriter(pipe, new UTF8Encoding(false)) { AutoFlush = true, NewLine = "\n" };
                SetConnected(true);
                await SendAsync(new JsonObject { ["type"] = "Hello", ["client"] = "FrameFlow.UI", ["version"] = AppInfo.Version }).ConfigureAwait(false);

                using var reader = new StreamReader(pipe, Encoding.UTF8);
                while (!ct.IsCancellationRequested)
                {
                    string? line = await reader.ReadLineAsync(ct).ConfigureAwait(false);
                    if (line is null) break;
                    if (line.Length == 0 || line.Length > MaxLineBytes) continue;
                    JsonElement msg;
                    try
                    {
                        using var doc = JsonDocument.Parse(line);
                        msg = doc.RootElement.Clone();
                    }
                    catch (JsonException)
                    {
                        Log.Warn("Engine sent malformed JSON");
                        continue;
                    }
                    _ui.TryEnqueue(() => MessageReceived?.Invoke(msg));
                }
            }
            catch (OperationCanceledException) { break; }
            catch (IOException ex) { Log.Warn($"Engine pipe error: {ex.Message}"); }
            catch (Exception ex) { Log.Error($"Engine client failure: {ex}"); }
            finally
            {
                _writer = null;
                _pipe = null;
                SetConnected(false);
            }

            try { await Task.Delay(500, ct).ConfigureAwait(false); }
            catch (OperationCanceledException) { break; }
        }
    }

    private void SetConnected(bool connected)
    {
        if (IsConnected == connected) return;
        IsConnected = connected;
        _ui.TryEnqueue(() => ConnectionChanged?.Invoke(connected));
    }

    private void EnsureEngineRunning()
    {
        if (!_autoStartEngine) return;
        if (DateTime.UtcNow - _lastEngineLaunch < TimeSpan.FromSeconds(10)) return;
        _lastEngineLaunch = DateTime.UtcNow;
        if (Process.GetProcessesByName("FrameFlow.Engine").Length > 0) return;
        if (!File.Exists(AppInfo.EnginePath))
        {
            Log.Error($"Engine not found at {AppInfo.EnginePath}");
            return;
        }
        try
        {
            var psi = new ProcessStartInfo(AppInfo.EnginePath)
            {
                UseShellExecute = false,
                CreateNoWindow = true,
                WorkingDirectory = AppInfo.ExeDir,
            };
            if (AppInfo.IsPortable)
            {
                psi.EnvironmentVariables["FRAMEFLOW_PORTABLE"] = "1";
            }
            psi.EnvironmentVariables["FRAMEFLOW_DATA_DIR"] = AppInfo.DataRoot;
            Process.Start(psi);
            Log.Info($"Started FrameFlow.Engine (data: {AppInfo.DataRoot})");
        }
        catch (Exception ex)
        {
            Log.Error($"Could not start engine: {ex.Message}");
        }
    }

    public Task SendAsync(JsonObject message)
    {
        message["v"] = ProtocolVersion;
        message["id"] = Interlocked.Increment(ref _nextId);
        return WriteLineAsync(message.ToJsonString());
    }

    private async Task WriteLineAsync(string line)
    {
        var writer = _writer;
        if (writer is null) return;
        await _writeGate.WaitAsync().ConfigureAwait(false);
        try { await writer.WriteLineAsync(line).ConfigureAwait(false); }
        catch (IOException ex) { Log.Warn($"Send failed: {ex.Message}"); }
        catch (ObjectDisposedException) { }
        finally { _writeGate.Release(); }
    }

    // Typed requests ------------------------------------------------------------------------
    public Task SetFpsLimit(double fps) => SendAsync(new JsonObject { ["type"] = "SetFpsLimit", ["fps"] = fps });
    public Task SetPacing(bool enabled) => SendAsync(new JsonObject { ["type"] = enabled ? "EnablePacing" : "DisablePacing" });
    public Task SetPacingMode(string mode) => SendAsync(new JsonObject { ["type"] = "SetPacingMode", ["mode"] = mode });
    public Task SetLimitMethod(string method) => SendAsync(new JsonObject { ["type"] = "SetLimitMethod", ["method"] = method });
    public Task SetProtectedRisk(bool allow) => SendAsync(new JsonObject { ["type"] = "SetProtectedRisk", ["allow"] = allow });
    public Task SelectGame(string exePath) => SendAsync(new JsonObject { ["type"] = "SelectGame", ["path"] = exePath });
    public Task ClearManualGame(string profileId) => SendAsync(new JsonObject { ["type"] = "ClearManualGame", ["profileId"] = profileId });
    public Task SetLogLevel(string level) => SendAsync(new JsonObject { ["type"] = "SetLogLevel", ["level"] = level });
    public Task SetEngineSettings(JsonObject settings)
    {
        settings["type"] = "SetEngineSettings";
        return SendAsync(settings);
    }
    public Task Shutdown() => SendAsync(new JsonObject { ["type"] = "Shutdown" });

    public void Dispose()
    {
        _cts.Cancel();
        _pipe?.Dispose();
        _cts.Dispose();
    }
}
