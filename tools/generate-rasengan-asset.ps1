param(
    [Parameter(Mandatory = $true)][string]$SourceImage,
    [Parameter(Mandatory = $true)][string]$RepositoryRoot
)

Add-Type -AssemblyName System.Drawing
$actorDir = Join-Path $RepositoryRoot "actors\fire_flower"
$texturePath = Join-Path $actorDir "rasengan.rgba16.inc.c"
$geometryPath = Join-Path $actorDir "rasengan_sphere.generated.inc.c"

$source = [System.Drawing.Image]::FromFile($SourceImage)
$bitmap = New-Object System.Drawing.Bitmap 32, 32
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
# Keep the original square vortex intact. Visual coverage is provided by two
# rotated sphere shells in the GeoLayout instead of stretching/repeating the
# source image across individual hemispheres.
$graphics.DrawImage($source, 0, 0, 32, 32)
$graphics.Dispose()
$source.Dispose()

$texture = New-Object System.Collections.Generic.List[string]
for ($y = 0; $y -lt 32; $y++) {
    $row = New-Object System.Collections.Generic.List[string]
    for ($x = 0; $x -lt 32; $x++) {
        $pixel = $bitmap.GetPixel($x, $y)
        # Keep the complete sphere visibly blue/white and remove stray purple
        # tones before quantizing to N64 RGBA16.
        $pixelRed = [int]$pixel.R
        $pixelGreen = [int]$pixel.G
        $pixelBlue = [int]$pixel.B
        $red = [int][Math]::Max(24, [Math]::Min($pixelRed, $pixelGreen))
        $green = [int][Math]::Max(88, $pixelGreen)
        $blue = [int][Math]::Max(176, $pixelBlue)
        $value = (($red -shr 3) -shl 11) -bor `
            (($green -shr 3) -shl 6) -bor `
            (($blue -shr 3) -shl 1) -bor 1
        # Texture is typedef'd as u8. RGBA16 pixels therefore must be emitted
        # as two big-endian bytes; emitting one 0xFFFF-style initializer per
        # pixel truncates it and leaves the texture at half its required size.
        $row.Add(('0x{0:X2}' -f (($value -shr 8) -band 0xFF)))
        $row.Add(('0x{0:X2}' -f ($value -band 0xFF)))
    }
    $texture.Add('    ' + ($row -join ', ') + ',')
}
$bitmap.Dispose()
[System.IO.File]::WriteAllLines($texturePath, $texture)

$longitudeSegments = 12
$latitudeSegments = 6
$radius = 64.0
$geometry = New-Object System.Collections.Generic.List[string]
for ($band = 0; $band -lt $latitudeSegments; $band++) {
    $geometry.Add("static const Vtx vr_rasengan_band_${band}_vtx[] = {")
    for ($longitude = 0; $longitude -le $longitudeSegments; $longitude++) {
        for ($ringOffset = 0; $ringOffset -le 1; $ringOffset++) {
            $latitude = $band + $ringOffset
            $phi = [Math]::PI * $latitude / $latitudeSegments
            $theta = 2.0 * [Math]::PI * $longitude / $longitudeSegments
            $vx = [Math]::Round($radius * [Math]::Sin($phi) * [Math]::Cos($theta))
            $vy = [Math]::Round($radius * [Math]::Cos($phi))
            $vz = [Math]::Round($radius * [Math]::Sin($phi) * [Math]::Sin($theta))
            $s = [Math]::Round(992.0 * $longitude / $longitudeSegments)
            $t = [Math]::Round(992.0 * $latitude / $latitudeSegments)
            $geometry.Add("    {{{${vx}, ${vy}, ${vz}}, 0, {${s}, ${t}}, {255, 255, 255, 255}}},")
        }
    }
    $geometry.Add('};')
    $geometry.Add('')
}

$geometry.Add('const Gfx vr_rasengan_dl[] = {')
$geometry.Add('    gsDPPipeSync(),')
$geometry.Add('    gsSPClearGeometryMode(G_LIGHTING | G_CULL_BACK | G_CULL_FRONT),')
$geometry.Add('    gsDPSetCombineLERP(TEXEL0, 0, ENVIRONMENT, 0, TEXEL0, 0, ENVIRONMENT, 0, TEXEL0, 0, ENVIRONMENT, 0, TEXEL0, 0, ENVIRONMENT, 0),')
$geometry.Add('    gsDPLoadTextureBlock(vr_rasengan_texture, G_IM_FMT_RGBA, G_IM_SIZ_16b, 32, 32, 0, G_TX_WRAP, G_TX_CLAMP, 5, 5, G_TX_NOLOD, G_TX_NOLOD),')
$geometry.Add('    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON),')
for ($band = 0; $band -lt $latitudeSegments; $band++) {
    $geometry.Add("    gsSPVertex(vr_rasengan_band_${band}_vtx, 26, 0),")
    for ($longitude = 0; $longitude -lt $longitudeSegments; $longitude++) {
        $a = $longitude * 2
        $b = $a + 1
        $c = $a + 2
        $d = $a + 3
        $geometry.Add("    gsSP2Triangles($a, $b, $c, 0, $c, $b, $d, 0),")
    }
}
$geometry.Add('    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF),')
$geometry.Add('    gsDPPipeSync(),')
$geometry.Add('    gsSPSetGeometryMode(G_LIGHTING | G_CULL_BACK),')
$geometry.Add('    gsDPSetEnvColor(255, 255, 255, 255),')
$geometry.Add('    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),')
$geometry.Add('    gsSPEndDisplayList(),')
$geometry.Add('};')
[System.IO.File]::WriteAllLines($geometryPath, $geometry)
