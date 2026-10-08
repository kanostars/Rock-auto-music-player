param(
    [string]$Source,
    [string]$Destination
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (!$Source) { $Source = Join-Path $projectRoot 'assets/icons/app.png' }
if (!$Destination) { $Destination = Join-Path $projectRoot 'assets/icons/app.ico' }
Add-Type -AssemblyName System.Drawing
$sourceBitmap = [System.Drawing.Bitmap]::new($Source)
$sizes = @(16,24,32,48,64,128,256)
$frames = [System.Collections.Generic.List[byte[]]]::new()
try {
    foreach ($size in $sizes) {
        $bitmap = [System.Drawing.Bitmap]::new($size,$size,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        $attributes = [System.Drawing.Imaging.ImageAttributes]::new()
        $stream = [System.IO.MemoryStream]::new()
        try {
            $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
            $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
            $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $attributes.SetWrapMode([System.Drawing.Drawing2D.WrapMode]::TileFlipXY)
            $graphics.DrawImage($sourceBitmap,[System.Drawing.Rectangle]::new(0,0,$size,$size),0,0,$sourceBitmap.Width,$sourceBitmap.Height,[System.Drawing.GraphicsUnit]::Pixel,$attributes)
            $bitmap.Save($stream,[System.Drawing.Imaging.ImageFormat]::Png)
            $frames.Add($stream.ToArray())
        } finally {
            $stream.Dispose()
            $attributes.Dispose()
            $graphics.Dispose()
            $bitmap.Dispose()
        }
    }
} finally { $sourceBitmap.Dispose() }
$file = [System.IO.File]::Create($Destination)
$writer = [System.IO.BinaryWriter]::new($file)
try {
    $writer.Write([uint16]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    for ($index=0; $index -lt $sizes.Count; $index++) {
        $sizeByte = if ($sizes[$index] -eq 256) { 0 } else { $sizes[$index] }
        $writer.Write([byte]$sizeByte)
        $writer.Write([byte]$sizeByte)
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]32)
        $writer.Write([uint32]$frames[$index].Length)
        $writer.Write([uint32]$offset)
        $offset += $frames[$index].Length
    }
    foreach ($frame in $frames) { $writer.Write([byte[]]$frame) }
} finally { $writer.Dispose() }
Write-Output "Icon exported: $Destination"
