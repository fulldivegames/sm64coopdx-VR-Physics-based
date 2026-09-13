$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$addition=(Get-Content (Join-Path $root 'release_notes.txt') | Where-Object {$_ -like '- Added Speed Running Mode*'} | Select-Object -First 1)
if(!$addition){throw 'Release addition missing'}
$credential="protocol=https`nhost=github.com`n`n" | git credential fill
if($LASTEXITCODE){throw 'Credential lookup failed'}
$line=$credential | Where-Object {$_ -like 'password=*'} | Select-Object -First 1
if(!$line){throw 'Credential missing'}
$headers=@{Authorization=('Bearer '+$line.Substring(9));Accept='application/vnd.github+json';'User-Agent'='sm64coopdx-release-docs';'X-GitHub-Api-Version'='2022-11-28'}
$identity=@{name='fulldivegames';email='fulldivegames@users.noreply.github.com'}
foreach($target in @(@{repo='sm64coopdx-VR-Physics-based';branch='vr'},@{repo='sm64coopdx-VR-Standalone-Physics-based';branch='main'})) {
    $api="https://api.github.com/repos/fulldivegames/$($target.repo)"
    foreach($file in @('README.md','release_notes.txt')) {
        $existing=Invoke-RestMethod -Headers $headers -Uri "$api/contents/${file}?ref=$($target.branch)" -TimeoutSec 30
        $old=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($existing.content))
        $pattern=if($file -eq 'README.md'){'(?m)^### New in v0\.9\.2 —[^\r\n]*'}else{'\A## v0\.9\.2 —[^\r\n]*'}
        if([regex]::Matches($old,$pattern).Count -ne 1){throw 'Public documentation changed; inspect first'}
        $updated=[regex]::Replace($old,$pattern,[Text.RegularExpressions.MatchEvaluator]{param($m) $m.Value+"`n`n"+$addition})
        $updated=[regex]::Replace($updated,'v0\.9\.2(?!\d)','v0.9.21')
        $payload=@{message="Update $file for v0.9.21";branch=$target.branch;sha=$existing.sha;content=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($updated));author=$identity;committer=$identity}|ConvertTo-Json -Depth 4
        $null=Invoke-RestMethod -Method Put -Headers $headers -Uri "$api/contents/$file" -ContentType 'application/json; charset=utf-8' -Body ([Text.Encoding]::UTF8.GetBytes($payload)) -TimeoutSec 60
        $verify=Invoke-RestMethod -Headers $headers -Uri "$api/contents/${file}?ref=$($target.branch)" -TimeoutSec 30
        if([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($verify.content)) -cne $updated){throw 'Documentation verification failed'}
        "$($target.repo)/$file updated; prior notes preserved."
    }
}
