param(
    [Parameter(Mandatory = $true)][string]$SourceImage,
    [Parameter(Mandatory = $true)][string]$RepositoryRoot
)

Add-Type -AssemblyName System.Drawing
$outputPath = Join-Path $RepositoryRoot `
    "actors\fire_flower\rasen_shuriken.rgba16.inc.c"
$source = [System.Drawing.Bitmap]::FromFile($SourceImage)
$bitmap = New-Object System.Drawing.Bitmap 32, 32
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.Clear([System.Drawing.Color]::Transparent)
$graphics.InterpolationMode = `
    [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$graphics.PixelOffsetMode = `
    [System.Drawing.Drawing2D.PixelOffsetMode]::Half
$graphics.DrawImage($source, 0, 0, 32, 32)
$graphics.Dispose()
$source.Dispose()

$lines = New-Object System.Collections.Generic.List[string]
for ($y = 0; $y -lt 32; $y++) {
    $row = New-Object System.Collections.Generic.List[string]
    for ($x = 0; $x -lt 32; $x++) {
        $pixel = $bitmap.GetPixel($x, $y)
        # Color channels are System.Byte values. Cast before shifting or
        # PowerShell keeps the operation byte-sized and discards the red and
        # green high bits, turning the supplied artwork almost pure dark blue.
        $red = [int]$pixel.R
        $green = [int]$pixel.G
        $blue = [int]$pixel.B
        $alpha = if ($pixel.A -ge 128) { 1 } else { 0 }
        $value = (($red -shr 3) -shl 11) -bor `
            (($green -shr 3) -shl 6) -bor `
            (($blue -shr 3) -shl 1) -bor $alpha
        # Texture is a byte array, so preserve the complete RGBA16 pixel by
        # writing its high and low bytes in N64 order.
        $row.Add(('0x{0:X2}' -f (($value -shr 8) -band 0xFF)))
        $row.Add(('0x{0:X2}' -f ($value -band 0xFF)))
    }
    $lines.Add('    ' + ($row -join ', ') + ',')
}
$bitmap.Dispose()
[System.IO.File]::WriteAllLines($outputPath, $lines)
