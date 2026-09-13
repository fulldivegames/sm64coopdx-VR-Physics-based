[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Commit,
      [Parameter(Mandatory=$true)][ValidateSet('v0.9.2','v0.9.21')][string]$Version)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ($Commit -notmatch '^[0-9a-f]{40}$') { throw 'Expected a complete release commit.' }
$targets = @(
    @{ Repo='sm64coopdx-VR-Physics-based'; File="dist/SM64-Co-Op-DX-VR-Windows-$Version.zip"; Name="SM64-Co-Op-DX-VR-Windows-$Version.zip"; Type='application/zip' },
    @{ Repo='sm64coopdx-VR-Standalone-Physics-based'; File='platform/android/app/build/outputs/apk/debug/app-debug.apk'; Name="SM64-Co-Op-DX-VR-Quest-$Version.apk"; Type='application/vnd.android.package-archive' }
)
foreach ($target in $targets) {
    $target.Path = Join-Path $root $target.File
    $target.Hash = (Get-FileHash -LiteralPath $target.Path -Algorithm SHA256).Hash.ToLowerInvariant()
    $target.Size = (Get-Item -LiteralPath $target.Path).Length
    $target.Uploads = @(@{Path=$target.Path;Name=$target.Name;Type=$target.Type;Hash=$target.Hash;Size=$target.Size})
    if ($target.Type -eq 'application/zip') {
        foreach ($suffix in @('-update.zip','-update.json')) {
            $name = $target.Name -replace '\.zip$', $suffix
            $path = Join-Path (Split-Path $target.Path) $name
            $target.Uploads += @{Path=$path;Name=$name;Type=$(if($suffix -eq '-update.zip') {'application/zip'} else {'application/json'});Hash=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant();Size=(Get-Item -LiteralPath $path).Length}
        }
    }
}
# Existing Git credential manager; never print or persist its response.
$credential = "protocol=https`nhost=github.com`n`n" | git credential fill
if ($LASTEXITCODE -ne 0) { throw 'GitHub credential lookup failed.' }
$passwordLine = $credential | Where-Object { $_ -like 'password=*' } | Select-Object -First 1
if (!$passwordLine) { throw 'No GitHub credential available.' }
$headers = @{ Authorization=('Bearer ' + $passwordLine.Substring(9)); Accept='application/vnd.github+json'; 'X-GitHub-Api-Version'='2022-11-28'; 'User-Agent'='sm64coopdx-vr-release' }
$notes = ((Get-Content -LiteralPath (Join-Path $root 'release_notes.txt') -Raw) -split '## v0.9 —')[0].Trim()
foreach ($target in $targets) {
    $api = 'https://api.github.com/repos/fulldivegames/' + $target.Repo
    $null = Invoke-RestMethod -Headers $headers -Uri "$api/commits/$Commit"
    $releases = @(Invoke-RestMethod -Headers $headers -Uri "$api/releases?per_page=100")
    if ($releases | Where-Object { $_.tag_name -eq $Version }) { throw "$Version already exists; inspect before retrying." }
}
foreach ($target in $targets) {
    $api = 'https://api.github.com/repos/fulldivegames/' + $target.Repo
    $payload = @{ tag_name=$Version; target_commitish=$Commit; name="$Version - Physical Gestures and New Power-Ups"; body=$notes; draft=$true; prerelease=$false } | ConvertTo-Json
    $release = Invoke-RestMethod -Method Post -Headers $headers -Uri "$api/releases" -ContentType 'application/json; charset=utf-8' -Body ([Text.Encoding]::UTF8.GetBytes($payload))
    $target.ReleaseId = $release.id
    foreach ($artifact in $target.Uploads) {
        $upload = ($release.upload_url -replace '\{\?name,label\}$','') + '?name=' + [Uri]::EscapeDataString($artifact.Name)
        $asset = Invoke-RestMethod -Method Post -Headers $headers -Uri $upload -ContentType $artifact.Type -InFile $artifact.Path -TimeoutSec 600
        if ($asset.size -ne $artifact.Size -or $asset.state -ne 'uploaded') { throw 'Upload validation failed.' }
        if ($asset.digest -ne ('sha256:' + $artifact.Hash)) { throw 'Uploaded SHA256 does not match the local artifact.' }
    }
    Write-Output ($target.Repo + ': draft asset SHA256 verified.')
}
# Neither release becomes public until both uploaded artifacts are verified.
foreach ($target in $targets) {
    $api = 'https://api.github.com/repos/fulldivegames/' + $target.Repo
    $payload = @{ draft=$false; make_latest='true' } | ConvertTo-Json
    $release = Invoke-RestMethod -Method Patch -Headers $headers -Uri "$api/releases/$($target.ReleaseId)" -ContentType 'application/json' -Body $payload
    $latest = Invoke-RestMethod -Headers $headers -Uri "$api/releases/latest"
    if ($latest.tag_name -ne $Version -or !($latest.assets | Where-Object { $_.name -eq $target.Name })) { throw 'Latest-release verification failed.' }
    Write-Output $release.html_url
}
