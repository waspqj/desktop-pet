# make_frames.ps1 - convert art PNGs into the .dat frames the pet reads.
#
#   input : assets/frames/*.png   (RGBA, transparent background, any size)
#   output: assets/frames_raw/*.dat  (110x110, premultiplied BGRA, header [u32 W][u32 H])
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File make_frames.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File make_frames.ps1 -Size 220
#
# The .dat format is what pet.rc embeds as RCDATA and pet.c loads with
# UpdateLayeredWindow, so the alpha channel must be PREMULTIPLIED.
param(
    [string]$FramesDir = (Join-Path $PSScriptRoot 'assets\frames'),
    [string]$RawDir    = (Join-Path $PSScriptRoot 'assets\frames_raw'),
    [int]$Size         = 110
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

if (-not (Test-Path $FramesDir)) { Write-Error "frames dir not found: $FramesDir"; exit 1 }
if (-not (Test-Path $RawDir))    { New-Item -ItemType Directory $RawDir -Force | Out-Null }

function Convert-One([string]$png, [string]$dst, [int]$S) {
    $src = [System.Drawing.Bitmap]::FromFile($png)
    try {
        # 统一成 32bpp ARGB, 保证有 alpha 通道
        $bmp = New-Object System.Drawing.Bitmap $src.Width, $src.Height, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.Clear([System.Drawing.Color]::Transparent)
        $g.DrawImage($src, 0, 0)
        $g.Dispose()
        $src.Dispose()

        $out = New-Object System.Drawing.Bitmap $S, $S, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $g2 = [System.Drawing.Graphics]::FromImage($out)
        $g2.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g2.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
        $g2.PixelOffsetMode   = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $g2.CompositingMode   = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
        $g2.Clear([System.Drawing.Color]::Transparent)
        $g2.DrawImage($bmp, 0, 0, $S, $S)
        $g2.Dispose()
        $bmp.Dispose()

        $rect = New-Object System.Drawing.Rectangle 0, 0, $S, $S
        $data = $out.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $len = $data.Stride * $S
            $buf = New-Object byte[] $len
            [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $buf, 0, $len)
        } finally { $out.UnlockBits($data) }
        $out.Dispose()

        # 预乘 alpha: RGB *= A/255  (UpdateLayeredWindow 期望 premultiplied)
        for ($i = 0; $i -lt $len; $i += 4) {
            $a = $buf[$i + 3]
            if ($a -ne 0 -and $a -ne 255) {
                $buf[$i + 0] = [byte]([int]$buf[$i + 0] * $a / 255)   # B
                $buf[$i + 1] = [byte]([int]$buf[$i + 1] * $a / 255)   # G
                $buf[$i + 2] = [byte]([int]$buf[$i + 2] * $a / 255)   # R
            }
        }

        $ms = New-Object System.IO.MemoryStream
        $ms.Write([BitConverter]::GetBytes([uint32]$S), 0, 4)
        $ms.Write([BitConverter]::GetBytes([uint32]$S), 0, 4)
        $ms.Write($buf, 0, $len)
        [System.IO.File]::WriteAllBytes($dst, $ms.ToArray())
        $ms.Dispose()
    } finally {
        if ($src) { try { $src.Dispose() } catch {} }
    }
}

$pngFiles = Get-ChildItem $FramesDir -Filter *.png | Sort-Object Name
if (-not $pngFiles) { Write-Error "no PNG found in $FramesDir"; exit 1 }

Write-Host ("Converting {0} PNG -> {1}x{2} .dat ..." -f $pngFiles.Count, $Size, $Size)
$total = 0
foreach ($f in $pngFiles) {
    $out = Join-Path $RawDir ($f.BaseName + ".dat")
    Convert-One $f.FullName $out $Size
    $total += (Get-Item $out).Length
}
Write-Host ("Done. {0} files, {1:N0} bytes total." -f $pngFiles.Count, $total)
