<#
.SYNOPSIS
    Generates the branded Inno Setup wizard bitmaps for the ZDE installer.

.DESCRIPTION
    Creates the large welcome-page sidebar image and the small header image
    from the mascot artwork, plus the setup/taskbar icon from the Zenvra logo.
    Files are written next to this script by default, not into a separate
    installer folder. Run this once after changing the artwork, or let
    Scripts/build_installer.ps1 call it automatically when they are missing.

.PARAMETER Force
    Recreate the images even if they already exist.

.PARAMETER OutputDir
    Directory for the generated files. Defaults to the directory containing
    this script (Scripts\), so setup.iss can reference the artwork with
    relative paths (".\maskot_wizard.bmp", and so on).
#>
[CmdletBinding()]
param(
    [switch]$Force,
    [string]$OutputDir
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot  = Split-Path -Parent $scriptDir

if (-not $OutputDir) {
    $OutputDir = $scriptDir
}
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)

$mascotPath = Join-Path $repoRoot 'Assets\image\maskot.jpg'
$logoPath   = Join-Path $repoRoot 'Assets\icons\zenvra_logo.png'
$mascotWizardPath = Join-Path $OutputDir 'maskot_wizard.bmp'
$smallPath  = Join-Path $OutputDir 'wizard_small.bmp'
$iconPath   = Join-Path $OutputDir 'zenvra_setup.ico'

if (-not (Test-Path -LiteralPath $mascotPath)) {
    throw "Mascot artwork not found: $mascotPath"
}

if (-not (Test-Path -LiteralPath $logoPath)) {
    throw "Zenvra logo not found: $logoPath"
}

if (-not $Force -and (Test-Path -LiteralPath $mascotWizardPath) -and (Test-Path -LiteralPath $smallPath) -and (Test-Path -LiteralPath $iconPath)) {
    Write-Host "Installer artwork already exists. Use -Force to regenerate." -ForegroundColor DarkGray
    return
}

Add-Type -AssemblyName System.Drawing

function New-WizardImage {
    param(
        [string]$Destination,
        [int]$Width,
        [int]$Height
    )

    $mascot = [System.Drawing.Image]::FromFile($mascotPath)
    $bitmap = New-Object System.Drawing.Bitmap($Width, $Height, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality

        # Fill the entire wizard panel with the mascot. Use a cover crop so
        # the image keeps its aspect ratio without letterboxing.
        $targetAspect = [double]$Width / [double]$Height
        $sourceAspect = [double]$mascot.Width / [double]$mascot.Height
        if ($sourceAspect -gt $targetAspect) {
            $srcHeight = [double]$mascot.Height
            $srcWidth = $srcHeight * $targetAspect
            $srcX = ([double]$mascot.Width - $srcWidth) / 2.0
            $srcY = 0.0
        }
        else {
            $srcWidth = [double]$mascot.Width
            $srcHeight = $srcWidth / $targetAspect
            $srcX = 0.0
            $srcY = ([double]$mascot.Height - $srcHeight) / 2.0
        }

        $sourceRect = [System.Drawing.RectangleF]::new([float]$srcX, [float]$srcY, [float]$srcWidth, [float]$srcHeight)
        $destinationRect = [System.Drawing.RectangleF]::new(0, 0, $Width, $Height)
        $graphics.DrawImage($mascot, $destinationRect, $sourceRect, [System.Drawing.GraphicsUnit]::Pixel)

        $bitmap.Save($Destination, [System.Drawing.Imaging.ImageFormat]::Bmp)
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
        $mascot.Dispose()
    }
}

function New-SmallImage {
    param(
        [string]$Destination,
        [int]$Size
    )

    $mascot = [System.Drawing.Image]::FromFile($mascotPath)
    $bitmap = New-Object System.Drawing.Bitmap($Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality

        # Square crop focused on the face/ears, with no rounded mask.
        $srcSize = [double]$mascot.Height * 0.72
        $srcX = ([double]$mascot.Width - $srcSize) / 2.0
        $srcY = 0.0

        $sourceRect = [System.Drawing.RectangleF]::new([float]$srcX, [float]$srcY, [float]$srcSize, [float]$srcSize)
        $destinationRect = [System.Drawing.RectangleF]::new(0, 0, $Size, $Size)
        $graphics.DrawImage($mascot, $destinationRect, $sourceRect, [System.Drawing.GraphicsUnit]::Pixel)

        $bitmap.Save($Destination, [System.Drawing.Imaging.ImageFormat]::Bmp)
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
        $mascot.Dispose()
    }
}

function New-RoundedRectanglePath {
    param(
        [System.Drawing.RectangleF]$Rect,
        [float]$Radius
    )

    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $diameter = $Radius * 2
    if ($diameter -le 0) {
        $path.AddRectangle($Rect)
        return $path
    }

    $arc = [System.Drawing.RectangleF]::new($Rect.X, $Rect.Y, $diameter, $diameter)
    $path.AddArc($arc, 180, 90)
    $arc.X = $Rect.Right - $diameter
    $path.AddArc($arc, 270, 90)
    $arc.Y = $Rect.Bottom - $diameter
    $path.AddArc($arc, 0, 90)
    $arc.X = $Rect.X
    $path.AddArc($arc, 90, 90)
    $path.CloseFigure()
    return $path
}

function Get-OpaqueBounds {
    param(
        [System.Drawing.Bitmap]$Bitmap,
        [int]$Threshold = 8
    )

    $rectangle = [System.Drawing.Rectangle]::new(0, 0, $Bitmap.Width, $Bitmap.Height)
    $locked = $Bitmap.LockBits($rectangle, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        $stride = $locked.Stride
        $pixels = New-Object byte[] ($stride * $Bitmap.Height)
        [System.Runtime.InteropServices.Marshal]::Copy($locked.Scan0, $pixels, 0, $pixels.Length)
    }
    finally {
        $Bitmap.UnlockBits($locked)
    }

    $minX = $Bitmap.Width
    $minY = $Bitmap.Height
    $maxX = -1
    $maxY = -1
    for ($y = 0; $y -lt $Bitmap.Height; $y++) {
        $row = $y * $stride
        for ($x = 0; $x -lt $Bitmap.Width; $x++) {
            if ($pixels[$row + ($x * 4) + 3] -gt $Threshold) {
                if ($x -lt $minX) { $minX = $x }
                if ($x -gt $maxX) { $maxX = $x }
                if ($y -lt $minY) { $minY = $y }
                if ($y -gt $maxY) { $maxY = $y }
            }
        }
    }

    if ($maxX -lt $minX -or $maxY -lt $minY) {
        throw "Logo image contains no opaque pixels."
    }

    return [System.Drawing.Rectangle]::new($minX, $minY, ($maxX - $minX + 1), ($maxY - $minY + 1))
}

function New-SetupIcon {
    param(
        [string]$Destination,
        [int[]]$Sizes = @(16, 24, 32, 48, 64, 128, 256)
    )

    $logo = [System.Drawing.Bitmap]::FromFile($logoPath)
    try {
        $logoBounds = Get-OpaqueBounds -Bitmap $logo
        $logoSource = [System.Drawing.RectangleF]::new(
            [float]$logoBounds.X,
            [float]$logoBounds.Y,
            [float]$logoBounds.Width,
            [float]$logoBounds.Height
        )

        $images = New-Object System.Collections.Generic.List[byte[]]
        foreach ($size in $Sizes) {
            $bitmap = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
            try {
                $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
                try {
                    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
                    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                    $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceOver

                    # Dark app-icon tile: keeps the white logo readable on
                    # both light title bars and the dark taskbar.
                    $tileRect = [System.Drawing.RectangleF]::new(0, 0, $size, $size)
                    $tilePath = New-RoundedRectanglePath -Rect $tileRect -Radius ([float]($size * 0.22))
                    try {
                        $tileBrush = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
                            $tileRect,
                            [System.Drawing.Color]::FromArgb(255, 32, 35, 44),
                            [System.Drawing.Color]::FromArgb(255, 10, 11, 16),
                            90.0
                        )
                        $graphics.FillPath($tileBrush, $tilePath)
                        $tileBrush.Dispose()

                        $rimPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(36, 255, 255, 255), [float][Math]::Max(1.0, $size / 128.0))
                        $graphics.DrawPath($rimPen, $tilePath)
                        $rimPen.Dispose()
                    }
                    finally {
                        $tilePath.Dispose()
                    }

                    $logoHeight = [double]$size * 0.70
                    $logoWidth = $logoHeight * ([double]$logoBounds.Width / [double]$logoBounds.Height)
                    $logoX = ([double]$size - $logoWidth) / 2.0
                    $logoY = ([double]$size - $logoHeight) / 2.0
                    $logoDestination = [System.Drawing.RectangleF]::new([float]$logoX, [float]$logoY, [float]$logoWidth, [float]$logoHeight)
                    $graphics.DrawImage($logo, $logoDestination, $logoSource, [System.Drawing.GraphicsUnit]::Pixel)
                }
                finally {
                    $graphics.Dispose()
                }

                $rectangle = [System.Drawing.Rectangle]::new(0, 0, $size, $size)
                $locked = $bitmap.LockBits($rectangle, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
                try {
                    $stride = $locked.Stride
                    $pixels = New-Object byte[] ($stride * $size)
                    [System.Runtime.InteropServices.Marshal]::Copy($locked.Scan0, $pixels, 0, $pixels.Length)
                }
                finally {
                    $bitmap.UnlockBits($locked)
                }

                # ICO entries use a bottom-up BGRA bitmap plus a 1-bit AND mask.
                $xor = New-Object byte[] ($size * $size * 4)
                $index = 0
                for ($y = $size - 1; $y -ge 0; $y--) {
                    $row = $y * $stride
                    for ($x = 0; $x -lt $size; $x++) {
                        $pixel = $row + ($x * 4)
                        $xor[$index++] = $pixels[$pixel + 2]
                        $xor[$index++] = $pixels[$pixel + 1]
                        $xor[$index++] = $pixels[$pixel]
                        $xor[$index++] = $pixels[$pixel + 3]
                    }
                }

                $maskStride = [int]([Math]::Floor(($size + 31) / 32.0) * 4)
                $andMask = New-Object byte[] ($maskStride * $size)

                $stream = New-Object System.IO.MemoryStream
                $writer = New-Object System.IO.BinaryWriter($stream)
                try {
                    $writer.Write([uint32]40)                     # BITMAPINFOHEADER size
                    $writer.Write([int32]$size)                   # Width
                    $writer.Write([int32]($size * 2))             # Height (XOR + AND)
                    $writer.Write([uint16]1)                      # Planes
                    $writer.Write([uint16]32)                     # Bits per pixel
                    $writer.Write([uint32]0)                      # BI_RGB
                    $writer.Write([uint32]($xor.Length + $andMask.Length))
                    $writer.Write([int32]0)
                    $writer.Write([int32]0)
                    $writer.Write([uint32]0)
                    $writer.Write([uint32]0)
                    $writer.Write($xor)
                    $writer.Write($andMask)
                    $writer.Flush()
                    $images.Add($stream.ToArray())
                }
                finally {
                    $writer.Dispose()
                }
            }
            finally {
                $bitmap.Dispose()
            }
        }

        $file = [System.IO.File]::Create($Destination)
        try {
            $writer = New-Object System.IO.BinaryWriter($file)
            try {
                $writer.Write([uint16]0)          # Reserved
                $writer.Write([uint16]1)          # Type: icon
                $writer.Write([uint16]$images.Count)

                $offset = 6 + (16 * $images.Count)
                for ($i = 0; $i -lt $images.Count; $i++) {
                    $size = $Sizes[$i]
                    $dimension = if ($size -ge 256) { 0 } else { $size }
                    $writer.Write([byte]$dimension)
                    $writer.Write([byte]$dimension)
                    $writer.Write([byte]0)        # Color count
                    $writer.Write([byte]0)        # Reserved
                    $writer.Write([uint16]1)      # Color planes
                    $writer.Write([uint16]32)     # Bits per pixel
                    $writer.Write([uint32]$images[$i].Length)
                    $writer.Write([uint32]$offset)
                    $offset += $images[$i].Length
                }

                foreach ($data in $images) {
                    $writer.Write($data)
                }
            }
            finally {
                $writer.Dispose()
            }
        }
        finally {
            $file.Dispose()
        }
    }
    finally {
        $logo.Dispose()
    }
}

if (-not (Test-Path -LiteralPath $OutputDir)) {
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
}

New-WizardImage -Destination $mascotWizardPath -Width 240 -Height 459
New-SmallImage -Destination $smallPath -Size 55
New-SetupIcon -Destination $iconPath

Write-Host "Created installer artwork:" -ForegroundColor Green
Write-Host "  $mascotWizardPath"
Write-Host "  $smallPath"
Write-Host "  $iconPath"
