using Microsoft.UI;
using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
using Windows.Foundation;
using Windows.System;
using Windows.UI;

namespace FrameFlow.UI.Controls;

/// <summary>
/// Minimal FPS slider with evenly spaced preset markers (30 … 240). Dragging snaps to markers;
/// custom values typed elsewhere (e.g. 141 or 59.94) are placed proportionally between markers.
/// </summary>
public sealed class FpsSlider : Grid
{
    public static readonly double[] DefaultMarkers = { 30, 40, 60, 90, 120, 144, 240 };

    private const double Pad = 16;       // keeps the knob and end labels inside the control
    private const double TrackTop = 11;
    private const double KnobSize = 24;

    private readonly Canvas _canvas = new();
    private readonly Border _track;
    private readonly Border _fill;
    private readonly Ellipse _knob;
    private readonly List<TextBlock> _labels = new();
    private readonly List<Rectangle> _ticks = new();
    private double[] _markers = DefaultMarkers;
    private double _value = 60;
    private bool _dragging;

    /// <summary>Raised while the user drags (preview) — value is always a marker.</summary>
    public event Action<double>? ValueChanging;
    /// <summary>Raised when the user commits a new value (pointer release or key press).</summary>
    public event Action<double>? ValueCommitted;

    public FpsSlider()
    {
        Height = 52;
        IsTabStop = true;
        UseSystemFocusVisuals = true;
        Background = new SolidColorBrush(Colors.Transparent);
        AutomationProperties.SetName(this, "FPS limit");

        _track = new Border
        {
            Height = 7,
            CornerRadius = new CornerRadius(4),
            Background = new SolidColorBrush(Color.FromArgb(0xFF, 0x9D, 0xE4, 0xEB)),
        };
        _fill = new Border
        {
            Height = 7,
            CornerRadius = new CornerRadius(4),
            Background = new LinearGradientBrush
            {
                StartPoint = new Point(0, 0),
                EndPoint = new Point(1, 0),
                GradientStops =
                {
                    new GradientStop { Color = Color.FromArgb(0xFF, 0xB7, 0xED, 0xC1), Offset = 0 },
                    new GradientStop { Color = Color.FromArgb(0xFF, 0x42, 0xC5, 0xC8), Offset = 1 },
                },
            },
        };
        _knob = new Ellipse
        {
            Width = KnobSize,
            Height = KnobSize,
            Fill = new SolidColorBrush(Color.FromArgb(0xFF, 0x42, 0xC5, 0xC8)),
            Stroke = new SolidColorBrush(Color.FromArgb(0xFF, 0xBD, 0xEE, 0xF0)),
            StrokeThickness = 1,
        };
        _canvas.Children.Add(_track);
        _canvas.Children.Add(_fill);
        Children.Add(_canvas);
        BuildLabels();
        _canvas.Children.Add(_knob);

        SizeChanged += (_, _) => Layout();
        PointerPressed += OnPointerPressed;
        PointerMoved += OnPointerMoved;
        PointerReleased += OnPointerReleased;
        PointerCaptureLost += (_, _) => _dragging = false;
        KeyDown += OnKeyDown;
        ProtectedCursor = InputSystemCursor.Create(InputSystemCursorShape.Hand);
    }

    public double Value
    {
        get => _value;
        set
        {
            if (_dragging || Math.Abs(value - _value) < 1e-9) return;
            _value = value;
            Layout();
        }
    }

    public IReadOnlyList<double> Markers => _markers;

    private void BuildLabels()
    {
        foreach (var l in _labels) _canvas.Children.Remove(l);
        foreach (var t in _ticks) _canvas.Children.Remove(t);
        _labels.Clear();
        _ticks.Clear();
        foreach (double m in _markers)
        {
            var tick = new Rectangle { Width = 1, Height = 9, Fill = new SolidColorBrush(Color.FromArgb(0xA0, 0x9D, 0xD8, 0xE8)) };
            var label = new TextBlock
            {
                Text = m.ToString("0"),
                FontSize = 12,
                Width = 44,
                TextAlignment = TextAlignment.Center,
                FontFamily = (FontFamily)Application.Current.Resources["FfFont"],
                Foreground = (Brush)Application.Current.Resources["FfInk2Brush"],
            };
            _ticks.Add(tick);
            _labels.Add(label);
            _canvas.Children.Add(tick);
            _canvas.Children.Add(label);
        }
    }

    private double ContentWidth => Math.Max(10, ActualWidth - 2 * Pad);

    /// <summary>Maps an FPS value to 0..1 along the track (piecewise linear between markers).</summary>
    private double Position(double fps)
    {
        int n = _markers.Length;
        if (fps <= _markers[0]) return 0;
        if (fps >= _markers[n - 1]) return 1;
        for (int i = 0; i < n - 1; i++)
        {
            if (fps <= _markers[i + 1])
            {
                double t = (fps - _markers[i]) / (_markers[i + 1] - _markers[i]);
                return (i + t) / (n - 1);
            }
        }
        return 1;
    }

    private int NearestMarker(double x)
    {
        double p = Math.Clamp((x - Pad) / ContentWidth, 0, 1);
        return (int)Math.Round(p * (_markers.Length - 1));
    }

    private void Layout()
    {
        double w = ContentWidth;
        _track.Width = w;
        Canvas.SetLeft(_track, Pad);
        Canvas.SetTop(_track, TrackTop);

        double p = Position(_value);
        _fill.Width = Math.Max(6, p * w);
        Canvas.SetLeft(_fill, Pad);
        Canvas.SetTop(_fill, TrackTop);

        Canvas.SetLeft(_knob, Pad + p * w - KnobSize / 2);
        Canvas.SetTop(_knob, TrackTop + 3.5 - KnobSize / 2);

        for (int i = 0; i < _markers.Length; i++)
        {
            double x = Pad + (double)i / (_markers.Length - 1) * w;
            Canvas.SetLeft(_ticks[i], x - 0.5);
            Canvas.SetTop(_ticks[i], TrackTop + 9);
            Canvas.SetLeft(_labels[i], x - 22);
            Canvas.SetTop(_labels[i], TrackTop + 24);
            bool on = Math.Abs(_markers[i] - _value) < 0.01;
            _labels[i].Foreground = (Brush)Application.Current.Resources[on ? "FfTealDeepBrush" : "FfInk3Brush"];
            _labels[i].FontWeight = on ? Microsoft.UI.Text.FontWeights.SemiBold : Microsoft.UI.Text.FontWeights.Normal;
        }
        AutomationProperties.SetHelpText(this, $"{_value:0.##} frames per second");
    }

    private void SetFromPointer(double x, bool commit)
    {
        double v = _markers[NearestMarker(x)];
        bool changed = Math.Abs(v - _value) > 1e-9;
        _value = v;
        Layout();
        if (changed) ValueChanging?.Invoke(v);
        if (commit) ValueCommitted?.Invoke(v);
    }

    private void OnPointerPressed(object sender, PointerRoutedEventArgs e)
    {
        Focus(FocusState.Pointer);
        _dragging = true;
        CapturePointer(e.Pointer);
        SetFromPointer(e.GetCurrentPoint(this).Position.X, commit: false);
        e.Handled = true;
    }

    private void OnPointerMoved(object sender, PointerRoutedEventArgs e)
    {
        if (!_dragging) return;
        SetFromPointer(e.GetCurrentPoint(this).Position.X, commit: false);
    }

    private void OnPointerReleased(object sender, PointerRoutedEventArgs e)
    {
        if (!_dragging) return;
        _dragging = false;
        ReleasePointerCapture(e.Pointer);
        SetFromPointer(e.GetCurrentPoint(this).Position.X, commit: true);
    }

    private void OnKeyDown(object sender, KeyRoutedEventArgs e)
    {
        int idx = Array.FindIndex(_markers, m => m >= _value - 0.01);
        if (idx < 0) idx = _markers.Length - 1;
        int target = e.Key switch
        {
            VirtualKey.Left or VirtualKey.Down => Math.Max(0, (_markers[idx] > _value + 0.01 ? idx : idx) - 1),
            VirtualKey.Right or VirtualKey.Up => Math.Min(_markers.Length - 1, _markers[idx] > _value + 0.01 ? idx : idx + 1),
            VirtualKey.Home => 0,
            VirtualKey.End => _markers.Length - 1,
            _ => -1,
        };
        if (target < 0) return;
        _value = _markers[target];
        Layout();
        ValueChanging?.Invoke(_value);
        ValueCommitted?.Invoke(_value);
        e.Handled = true;
    }
}
