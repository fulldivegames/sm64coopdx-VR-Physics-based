[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Commit)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ($Commit -notmatch '^[0-9a-f]{40}$') { throw 'Expected a complete release commit.' }
$targets = @(
    @{ Repo='sm64coopdx-VR-Physics-based'; File='dist/release-v089/SM64-Co-Op-DX-VR-Windows-v0.8.9.zip'; Name='SM64-Co-Op-DX-VR-Windows-v0.8.9.zip'; Type='application/zip' },
    @{ Repo='sm64coopdx-VR-Standalone-Physics-based'; File='platform/android/app/build/outputs/apk/debug/app-debug.apk'; Name='SM64-Co-Op-DX-VR-Quest-v0.8.9.apk'; Type='application/vnd.android.package-archive' }
)
foreach ($target in $targets) {
    $target.Path = Join-Path $root $target.File
    $target.Hash = (Get-FileHash -LiteralPath $target.Path -Algorithm SHA256).Hash.ToLowerInvariant()
    $target.Size = (Get-Item -LiteralPath $target.Path).Length
}
# Use the existing credential manager only for the authorized GitHub release.
# Never print or persist its credential response.
$credential = "protocol=https`nhost=github.com`n`n" | git credential fill
if ($LASTEXITCODE -ne 0) { throw 'GitHub credential lookup failed.' }
$passwordLine = $credential | Where-Object { $_ -like 'password=*' } | Select-Object -First 1
if (!$passwordLine) { throw 'No GitHub credential available.' }
$headers = @{ Authorization=('Bearer ' + $passwordLine.Substring(9)); Accept='application/vnd.github+json'; 'X-GitHub-Api-Version'='2022-11-28'; 'User-Agent'='sm64coopdx-vr-release' }
$notes = ((Get-Content -LiteralPath (Join-Path $root 'release_notes.txt') -Raw) -split '## v0.8.4')[0].Trim()
$notes += "`n`nValidation: PC and Quest compilation passed; renderer state/vertex trace and lifecycle regression tests passed. The large-map improvement was tested on Quest. The final Big Hands reach/contact adjustments still need in-headset confirmation."
foreach ($target in $targets) {
    $api = 'https://api.github.com/repos/fulldivegames/' + $target.Repo
    $null = Invoke-RestMethod -Headers $headers -Uri "$api/commits/$Commit"
    $releases = @(Invoke-RestMethod -Headers $headers -Uri "$api/releases?per_page=100")
    if ($releases | Where-Object { $_.tag_name -eq 'v0.8.9' }) { throw 'v0.8.9 already exists; refusing to overwrite.' }
    $payload = @{ tag_name='v0.8.9'; target_commitish=$Commit; name='v0.8.9 - Large-Map Performance Update'; body=$notes; draft=$true; prerelease=$false } | ConvertTo-Json
    $release = Invoke-RestMethod -Method Post -Headers $headers -Uri "$api/releases" -ContentType 'application/json' -Body $payload
    $target.ReleaseId = $release.id
    $upload = ($release.upload_url -replace '\{\?name,label\}$','') + '?name=' + [Uri]::EscapeDataString($target.Name)
    $asset = Invoke-RestMethod -Method Post -Headers $headers -Uri $upload -ContentType $target.Type -InFile $target.Path -TimeoutSec 600
    if ($asset.size -ne $target.Size -or $asset.state -ne 'uploaded') { throw 'Upload validation failed.' }
    if ($asset.digest -ne ('sha256:' + $target.Hash)) { throw 'Uploaded SHA256 does not match the local artifact.' }
    Write-Output ($target.Repo + ': draft asset SHA256 verified.')
}
# Publish only after BOTH draft uploads passed verification.
foreach ($target in $targets) {
    $api = 'https://api.github.com/repos/fulldivegames/' + $target.Repo
    $payload = @{ draft=$false; make_latest='true' } | ConvertTo-Json
    $release = Invoke-RestMethod -Method Patch -Headers $headers -Uri "$api/releases/$($target.ReleaseId)" -ContentType 'application/json' -Body $payload
    $latest = Invoke-RestMethod -Headers $headers -Uri "$api/releases/latest"
    if ($latest.tag_name -ne 'v0.8.9' -or !($latest.assets | Where-Object { $_.name -eq $target.Name })) { throw 'Latest-release verification failed.' }
    Write-Output $release.html_url
}
