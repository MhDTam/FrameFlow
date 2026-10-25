$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$assets = Join-Path $PSScriptRoot '..\apps\FrameFlow.UI\Assets'
$source = Join-Path $assets 'FrameFlow.png'
$destination = Join-Path $assets 'FrameFlow.ico'
$sizes = @(16, 24, 32, 48, 64, 128, 256)
$bitmaps = New-Object 'System.Collections.Generic.List[byte[]]'
$image = [System.Drawing.Image]::FromFile($source)
try {
    foreach ($size in $sizes) {
        $bitmap = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
            try {
                $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
                $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                $graphics.DrawImage($image, 0, 0, $size, $size)
            } finally { $graphics.Dispose() }
            $memory = New-Object System.IO.MemoryStream
            try {
                $bitmap.Save($memory, [System.Drawing.Imaging.ImageFormat]::Png)
                $bitmaps.Add($memory.ToArray())
            } finally { $memory.Dispose() }
        } finally { $bitmap.Dispose() }
    }
} finally { $image.Dispose() }

$stream = [System.IO.File]::Create($destination)
$writer = New-Object System.IO.BinaryWriter($stream)
try {
    $writer.Write([uint16]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    for ($index = 0; $index -lt $sizes.Count; $index++) {
        $size = $sizes[$index]
        $writer.Write([byte]($size % 256))
        $writer.Write([byte]($size % 256))
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]32)
        $writer.Write([uint32]$bitmaps[$index].Length)
        $writer.Write([uint32]$offset)
        $offset += $bitmaps[$index].Length
    }
    foreach ($bitmap in $bitmaps) { $writer.Write($bitmap) }
} finally {
    $writer.Dispose()
    $stream.Dispose()
}
Write-Host "Brand icon: $destination"
