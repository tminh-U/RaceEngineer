param(
    [string]$SourcePath = (Join-Path $PSScriptRoot '..\final_icon.png'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\assets')
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$sourcePath = [IO.Path]::GetFullPath($SourcePath)
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
if (-not [IO.File]::Exists($sourcePath)) {
    throw "Icon source not found: $sourcePath"
}
[IO.Directory]::CreateDirectory($outputPath) | Out-Null

$sourceBitmap = [System.Drawing.Bitmap]::new($sourcePath)
try {
    function New-IconBitmap([int]$size) {
        $bitmap = [System.Drawing.Bitmap]::new(
            $size,
            $size,
            [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
            $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
            $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
            $graphics.DrawImage($sourceBitmap, 0, 0, $size, $size)
        } finally {
            $graphics.Dispose()
        }
        return $bitmap
    }

    $pngSize = 64
    $pngBitmap = New-IconBitmap $pngSize
    try {
        $pngBitmap.Save((Join-Path $outputPath 'final_icon_64.png'), [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $pngBitmap.Dispose()
    }

    $icoSizes = @(16, 24, 32, 48, 64, 128, 256)
    $pngFrames = [Collections.Generic.List[byte[]]]::new()
    foreach ($size in $icoSizes) {
        $bitmap = New-IconBitmap $size
        try {
            $stream = [IO.MemoryStream]::new()
            try {
                $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
                $pngFrames.Add($stream.ToArray())
            } finally {
                $stream.Dispose()
            }
        } finally {
            $bitmap.Dispose()
        }
    }

    $icoPath = Join-Path $outputPath 'final_icon.ico'
    $file = [IO.File]::Open($icoPath, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try {
        $writer = [IO.BinaryWriter]::new($file)
        try {
            $writer.Write([uint16]0)
            $writer.Write([uint16]1)
            $writer.Write([uint16]$icoSizes.Count)

            $offset = 6 + (16 * $icoSizes.Count)
            for ($index = 0; $index -lt $icoSizes.Count; ++$index) {
                $size = $icoSizes[$index]
                $width = if ($size -eq 256) { 0 } else { $size }
                $writer.Write([byte]$width)
                $writer.Write([byte]$width)
                $writer.Write([byte]0)
                $writer.Write([byte]0)
                $writer.Write([uint16]1)
                $writer.Write([uint16]32)
                $writer.Write([uint32]$pngFrames[$index].Length)
                $writer.Write([uint32]$offset)
                $offset += $pngFrames[$index].Length
            }
            foreach ($frame in $pngFrames) {
                $writer.Write($frame)
            }
        } finally {
            $writer.Dispose()
        }
    } finally {
        $file.Dispose()
    }
} finally {
    $sourceBitmap.Dispose()
}

Write-Host "Generated $outputPath\final_icon_64.png and $outputPath\final_icon.ico from $sourcePath"
