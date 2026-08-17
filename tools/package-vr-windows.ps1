[CmdletBinding()]
param(
    [string]$BuildDirectory = "build/us_pc",
    [string]$OutputDirectory = "dist",
    [string]$Version = "dev"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDirectory))
$outputPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $OutputDirectory))
$safeVersion = $Version -replace '[^A-Za-z0-9._-]', '-'
$packageName = "SM64-Co-Op-DX-VR-Windows-$safeVersion"
$stagePath = Join-Path $outputPath $packageName
$zipPath = Join-Path $outputPath "$packageName.zip"

$requiredFiles = @(
    "sm64coopdx.exe",
    "discord_game_sdk.dll",
    "libopenxr_loader.dll",
    "libgcc_s_seh-1.dll",
    "libstdc++-6.dll",
    "libwinpthread-1.dll",
    "openxr-sdk-LICENSE",
    "gcc-COPYING.LIB",
    "gcc-COPYING.RUNTIME",
    "gcc-COPYING3",
    "libwinpthread-COPYING"
)

$requiredDirectories = @(
    "dynos",
    "lang",
    "mods",
    "palettes"
)

foreach ($file in $requiredFiles) {
    $candidate = Join-Path $buildPath $file
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw "Missing required build file: $candidate. Build the Windows game before packaging."
    }
}

foreach ($directory in $requiredDirectories) {
    $candidate = Join-Path $buildPath $directory
    if (-not (Test-Path -LiteralPath $candidate -PathType Container)) {
        throw "Missing required build directory: $candidate. Build the Windows game before packaging."
    }
}

New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
if (Test-Path -LiteralPath $stagePath) {
    Remove-Item -LiteralPath $stagePath -Recurse -Force
}
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
New-Item -ItemType Directory -Path $stagePath | Out-Null

Copy-Item -LiteralPath (Join-Path $buildPath "sm64coopdx.exe") -Destination (Join-Path $stagePath "SM64-Co-Op-DX-VR.exe")
Copy-Item -LiteralPath (Join-Path $buildPath "discord_game_sdk.dll") -Destination $stagePath
Copy-Item -LiteralPath (Join-Path $buildPath "libopenxr_loader.dll") -Destination $stagePath
Copy-Item -LiteralPath (Join-Path $buildPath "libgcc_s_seh-1.dll") -Destination $stagePath
Copy-Item -LiteralPath (Join-Path $buildPath "libstdc++-6.dll") -Destination $stagePath
Copy-Item -LiteralPath (Join-Path $buildPath "libwinpthread-1.dll") -Destination $stagePath
Copy-Item -LiteralPath (Join-Path $repoRoot "docs/PLAYER-GUIDE.txt") -Destination (Join-Path $stagePath "README.txt")

$licensesPath = Join-Path $stagePath "licenses"
New-Item -ItemType Directory -Path $licensesPath | Out-Null
Copy-Item -LiteralPath (Join-Path $buildPath "openxr-sdk-LICENSE") -Destination (Join-Path $licensesPath "OpenXR-SDK-LICENSE.txt")
Copy-Item -LiteralPath (Join-Path $buildPath "gcc-COPYING.LIB") -Destination (Join-Path $licensesPath "GCC-COPYING.LIB.txt")
Copy-Item -LiteralPath (Join-Path $buildPath "gcc-COPYING.RUNTIME") -Destination (Join-Path $licensesPath "GCC-Runtime-Library-Exception.txt")
Copy-Item -LiteralPath (Join-Path $buildPath "gcc-COPYING3") -Destination (Join-Path $licensesPath "GCC-GPL-3.0.txt")
Copy-Item -LiteralPath (Join-Path $buildPath "libwinpthread-COPYING") -Destination (Join-Path $licensesPath "libwinpthread-COPYING.txt")

foreach ($directory in $requiredDirectories) {
    Copy-Item -LiteralPath (Join-Path $buildPath $directory) -Destination $stagePath -Recurse
}

# Incremental test builds may leave the failed synchronized Fire Flowers mod
# in the build output. The release candidate uses the proven native Fire
# Flower implementation and its lightweight protocol manifest instead.
$stagedModsPath = Join-Path $stagePath "mods"
$failedFireFlowersPath = Join-Path $stagedModsPath "Fire Flowers"
if (Test-Path -LiteralPath $failedFireFlowersPath) {
    Remove-Item -LiteralPath $failedFireFlowersPath -Recurse -Force
}
$nativeManifestSource = Join-Path $repoRoot "mods/vr-special-moves"
$nativeManifestDestination = Join-Path $stagedModsPath "vr-special-moves"
if (Test-Path -LiteralPath $nativeManifestDestination) {
    Remove-Item -LiteralPath $nativeManifestDestination -Recurse -Force
}
Copy-Item -LiteralPath $nativeManifestSource `
    -Destination $nativeManifestDestination -Recurse

$romExtensions = @(".z64", ".n64", ".v64")
$romFiles = @(
    Get-ChildItem -LiteralPath $stagePath -Recurse -File |
        Where-Object { $_.Extension.ToLowerInvariant() -in $romExtensions }
)
if ($romFiles.Count -ne 0) {
    throw "Packaging stopped because a ROM file was found in the staging directory."
}

Compress-Archive -LiteralPath $stagePath -DestinationPath $zipPath -CompressionLevel Optimal

Write-Host "Created player package: $zipPath"
Write-Host "The package contains no ROM, updater, map, debug database, or backup executable."
