using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using Microsoft.UI.Xaml.Media.Imaging;

namespace FrameFlow.UI.Services;

/// <summary>Turns an executable's own icon into a XAML image for the game card.</summary>
public static partial class IconExtractor
{
    private static readonly Dictionary<string, BitmapImage?> Cache = new(StringComparer.OrdinalIgnoreCase);

    [LibraryImport("user32.dll", EntryPoint = "PrivateExtractIconsW", StringMarshalling = StringMarshalling.Utf16)]
    private static partial uint PrivateExtractIcons(string file, int index, int cx, int cy, out IntPtr icon, out uint id, uint count, uint flags);

    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool DestroyIcon(IntPtr icon);

    public static async Task<BitmapImage?> FromExeAsync(string exePath)
    {
        if (string.IsNullOrEmpty(exePath)) return null;
        if (Cache.TryGetValue(exePath, out var cached)) return cached;

        byte[]? png = await Task.Run(() => ExtractPng(exePath));

        BitmapImage? image = null;
        if (png is not null)
        {
            image = new BitmapImage();
            using var stream = new MemoryStream(png).AsRandomAccessStream();
            await image.SetSourceAsync(stream);
        }
        Cache[exePath] = image;
        return image;
    }

    private static byte[]? ExtractPng(string exePath)
    {
        try
        {
            if (!File.Exists(exePath)) return null;
            Bitmap? bmp = null;
            // Prefer the 256 px frame from the exe's resources; ExtractAssociatedIcon only yields 32 px.
            if (PrivateExtractIcons(exePath, 0, 256, 256, out IntPtr hIcon, out _, 1, 0) > 0 && hIcon != IntPtr.Zero)
            {
                try
                {
                    using var icon = Icon.FromHandle(hIcon);
                    bmp = icon.ToBitmap();
                }
                finally
                {
                    DestroyIcon(hIcon);
                }
            }
            if (bmp is null)
            {
                using Icon? assoc = Icon.ExtractAssociatedIcon(exePath);
                if (assoc is null) return null;
                bmp = assoc.ToBitmap();
            }
            using (bmp)
            using (var ms = new MemoryStream())
            {
                bmp.Save(ms, ImageFormat.Png);
                return ms.ToArray();
            }
        }
        catch (Exception ex) when (ex is ArgumentException or IOException or ExternalException or UnauthorizedAccessException)
        {
            return null;
        }
    }
}
