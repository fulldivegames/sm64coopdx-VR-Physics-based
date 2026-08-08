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
    "discord_game_sdk.dll"
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
Copy-Item -LiteralPath (Join-Path $repoRoot "docs/PLAYER-GUIDE.txt") -Destination (Join-Path $stagePath "README.txt")

foreach ($directory in $requiredDirectories) {
    Copy-Item -LiteralPath (Join-Path $buildPath $directory) -Destination $stagePath -Recurse
}

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
