using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
using Windows.Foundation;
using Windows.UI;

namespace FrameFlow.UI.Controls;

/// <summary>
/// Quiet frametime line chart: one thin teal line of per-bucket mean frametime, a faint area
/// under it, and a dashed reference line at the target frametime. Deliberately understated —
/// its job is to show "flat = smooth", not to be an analysis tool.
/// </summary>
public sealed class FrametimeGraph : Grid
{
    private readonly Canvas _canvas = new();
    private readonly Polyline _line;
    private readonly Polygon _area;
    private readonly Line _target;
    private readonly Line _top;
    private readonly Line _bottom;
    private readonly TextBlock _yTop = MakeAxisLabel();
    private readonly TextBlock _yMid = MakeAxisLabel();
    private readonly TextBlock _yBottom = MakeAxisLabel();
    private readonly TextBlock _empty;
    private readonly Grid _xGrid = new();

    private double[] _data = Array.Empty<double>();
    private double _targetMs;
    private double _historySeconds = 30;

    public FrametimeGraph()
    {
        ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(52) });
        ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

        var yAxis = new Grid { Margin = new Thickness(0, -6, 8, -6) };
        _yTop.VerticalAlignment = VerticalAlignment.Top;
        _yMid.VerticalAlignment = VerticalAlignment.Center;
        _yBottom.VerticalAlignment = VerticalAlignment.Bottom;
        yAxis.Children.Add(_yTop);
        yAxis.Children.Add(_yMid);
        yAxis.Children.Add(_yBottom);
        Children.Add(yAxis);

        var gridBrush = new SolidColorBrush(Color.FromArgb(0x2E, 0x71, 0xC9, 0xCE));
        _top = new Line { Stroke = gridBrush, StrokeThickness = 1 };
        _bottom = new Line { Stroke = gridBrush, StrokeThickness = 1 };
        _target = new Line
        {
            Stroke = new SolidColorBrush(Color.FromArgb(0x73, 0x71, 0xC9, 0xCE)),
            StrokeThickness = 1,
            StrokeDashArray = new DoubleCollection { 4, 5 },
        };
        _area = new Polygon
        {
            Fill = new LinearGradientBrush
            {
                StartPoint = new Point(0, 0),
                EndPoint = new Point(0, 1),
                GradientStops =
                {
                    new GradientStop { Color = Color.FromArgb(0x29, 0x71, 0xC9, 0xCE), Offset = 0 },
                    new GradientStop { Color = Color.FromArgb(0x00, 0x71, 0xC9, 0xCE), Offset = 1 },
                },
            },
        };
        _line = new Polyline
        {
            Stroke = new SolidColorBrush(Color.FromArgb(0xFF, 0x5D, 0xB8, 0xBE)),
            StrokeThickness = 1.6,
            StrokeLineJoin = PenLineJoin.Round,
        };
        _empty = new TextBlock
        {
            Text = "Waiting for frames…",
            FontSize = 12,
            Foreground = (Brush)Application.Current.Resources["FfInk3Brush"],
            HorizontalAlignment = HorizontalAlignment.Center,
            VerticalAlignment = VerticalAlignment.Center,
            Margin = new Thickness(0, 0, 0, 36),
        };
        _canvas.Children.Add(_top);
        _canvas.Children.Add(_bottom);
        _canvas.Children.Add(_target);
        _canvas.Children.Add(_area);
        _canvas.Children.Add(_line);

        var plot = new Grid();
        plot.Children.Add(_canvas);
        plot.Children.Add(_empty);
        SetColumn(plot, 1);
        Children.Add(plot);

        SetColumn(_xGrid, 1);
        SetRow(_xGrid, 1);
        _xGrid.Margin = new Thickness(0, 4, 0, 0);
        Children.Add(_xGrid);

        _canvas.SizeChanged += (_, _) => Redraw();
        RebuildXAxis();
    }

    private static TextBlock MakeAxisLabel() => new()
    {
        FontSize = 10.5,
        TextAlignment = TextAlignment.Right,
        HorizontalAlignment = HorizontalAlignment.Right,
        Foreground = (Brush)Application.Current.Resources["FfInk3Brush"],
    };

    private void RebuildXAxis()
    {
        _xGrid.Children.Clear();
        _xGrid.ColumnDefinitions.Clear();
        int secs = (int)Math.Round(_historySeconds);
        string[] labels = { $"{secs}s ago", $"{secs * 3 / 4}s", $"{secs / 2}s", $"{secs / 4}s", "Now" };
        for (int i = 0; i < labels.Length; i++)
        {
            var tb = new TextBlock
            {
                Text = labels[i],
                FontSize = 10.5,
                Foreground = (Brush)Application.Current.Resources["FfInk3Brush"],
                HorizontalAlignment = i == 0 ? HorizontalAlignment.Left
                    : i == labels.Length - 1 ? HorizontalAlignment.Right : HorizontalAlignment.Center,
            };
            _xGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            SetColumn(tb, i);
            _xGrid.Children.Add(tb);
        }
    }

    /// <param name="meanMs">Per-bucket mean frametimes, oldest first; 0 = no data.</param>
    /// <param name="targetMs">Reference frametime (1000 / FPS limit) or 0 for none.</param>
    public void SetData(double[] meanMs, double targetMs, double historySeconds)
    {
        _data = meanMs;
        _targetMs = targetMs;
        if (Math.Abs(historySeconds - _historySeconds) > 0.5)
        {
            _historySeconds = historySeconds;
            RebuildXAxis();
        }
        Redraw();
    }

    public void Clear() => SetData(Array.Empty<double>(), _targetMs, _historySeconds);

    private void Redraw()
    {
        double w = _canvas.ActualWidth, h = _canvas.ActualHeight;
        if (w <= 0 || h <= 0) return;

        _top.X1 = 0; _top.X2 = w; _top.Y1 = _top.Y2 = 0.5;
        _bottom.X1 = 0; _bottom.X2 = w; _bottom.Y1 = _bottom.Y2 = h - 0.5;

        int first = Array.FindIndex(_data, v => v > 0);
        bool hasData = first >= 0;
        _empty.Visibility = hasData ? Visibility.Collapsed : Visibility.Visible;

        // Scale around the reference so a perfectly paced game draws a calm, centred line.
        double reference = _targetMs > 0 ? _targetMs : (hasData ? _data.Where(v => v > 0).Average() : 16.67);
        double span = Math.Max(reference * 0.28, 1.0);
        if (hasData)
        {
            // Grow (never shrink below ±28 %) to keep real deviations on screen, capped at 3× so one
            // huge hitch cannot flatten everything else.
            double maxDev = _data.Where(v => v > 0).Select(v => Math.Abs(v - reference)).DefaultIfEmpty(0).Max();
            span = Math.Min(Math.Max(span, maxDev * 1.15), reference * 3);
        }
        double yMin = Math.Max(0, reference - span), yMax = reference + span;
        double Y(double v) => h - (Math.Clamp(v, yMin, yMax) - yMin) / (yMax - yMin) * h;

        _yTop.Text = $"{yMax:0.#} ms";
        _yMid.Text = $"{reference:0.0#}";
        _yBottom.Text = $"{yMin:0.#} ms";

        double ty = Y(reference);
        _target.X1 = 0; _target.X2 = w; _target.Y1 = _target.Y2 = ty;

        var pts = new PointCollection();
        var area = new PointCollection();
        if (hasData && _data.Length > 1)
        {
            double step = w / (_data.Length - 1);
            double firstX = first * step;
            area.Add(new Point(firstX, h));
            for (int i = first; i < _data.Length; i++)
            {
                double v = _data[i] > 0 ? _data[i] : reference;
                var p = new Point(i * step, Y(v));
                pts.Add(p);
                area.Add(p);
            }
            area.Add(new Point(w, h));
        }
        _line.Points = pts;
        _area.Points = area;
    }
}
