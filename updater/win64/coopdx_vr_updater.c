#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <string.h>

/*
 * The upstream updater is tied to the stock SM64 Co-Op DX repository. The VR
 * build needs a separate entry point because its release assets live in a
 * different repository and use a different Windows executable. PowerShell
 * performs the HTTPS, JSON, and ZIP work available on supported Windows.
 */
static void escape_single_quotes(const char* input, char* output, size_t size) {
    size_t written = 0;
    while (*input != '\0' && written + 1 < size) {
        if (*input == '\'') {
            if (written + 2 >= size) break;
            output[written++] = '\'';
            output[written++] = '\'';
        } else {
            output[written++] = *input;
        }
        input++;
    }
    output[written] = '\0';
}

static BOOL start_update_script(const char* install_dir, const char* script_path) {
    char command_line[MAX_PATH * 3] = { 0 };
    STARTUPINFOA startup = { 0 };
    PROCESS_INFORMATION process = { 0 };
    startup.cb = sizeof(startup);
    _snprintf_s(command_line, sizeof(command_line), _TRUNCATE,
        "powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"%s\"",
        script_path);
    if (!CreateProcessA(NULL, command_line, NULL, NULL, FALSE,
                        CREATE_NEW_CONSOLE, NULL, install_dir,
                        &startup, &process)) {
        return FALSE;
    }
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    return TRUE;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous_instance,
                   LPSTR command_line, int show_command) {
    (void)instance; (void)previous_instance; (void)command_line; (void)show_command;
    char module_path[MAX_PATH] = { 0 };
    DWORD module_length = GetModuleFileNameA(NULL, module_path, sizeof(module_path));
    if (module_length == 0 || module_length >= sizeof(module_path)) return 1;
    char* separator = strrchr(module_path, '\\');
    if (separator == NULL) return 1;
    *separator = '\0';

    char escaped_install_dir[MAX_PATH * 2] = { 0 };
    escape_single_quotes(module_path, escaped_install_dir, sizeof(escaped_install_dir));
    char temp_dir[MAX_PATH] = { 0 };
    DWORD temp_length = GetTempPathA(sizeof(temp_dir), temp_dir);
    if (temp_length == 0 || temp_length >= sizeof(temp_dir)) return 1;
    char script_path[MAX_PATH] = { 0 };
    _snprintf_s(script_path, sizeof(script_path), _TRUNCATE,
        "%ssm64coopdx-vr-updater-%lu.ps1", temp_dir,
        (unsigned long)GetCurrentProcessId());
    FILE* script = fopen(script_path, "wb");
    if (script == NULL) return 1;

    /* GitHub returns releases newest-first; no tag or filename is hardcoded. */
    fprintf(script,
        "$ErrorActionPreference='Stop'; "
        "$Host.UI.RawUI.WindowTitle='SM64 Co-Op DX VR Updater'; "
        "trap { Write-Host ('Update failed: ' + $_.Exception.Message) -ForegroundColor Red; Read-Host 'Press Enter to close'; exit 1 }; "
        "Write-Host 'Updating... please wait.' -ForegroundColor Cyan; "
        "$install='%s'; "
        "Start-Sleep -Seconds 1; "
        "$headers=@{Accept='application/vnd.github+json';'User-Agent'='SM64-Co-Op-DX-VR-Updater'}; "
        "$releases=Invoke-RestMethod -Headers $headers -Uri 'https://api.github.com/repos/fulldivegames/sm64coopdx-VR-Physics-based/releases?per_page=100'; "
        "$release=$releases | Where-Object { -not $_.draft -and -not $_.prerelease } | ForEach-Object { "
            "$asset=$_.assets | Where-Object { $_.name -match '(?i)windows.*\\.zip$' } | Select-Object -First 1; "
            "if ($null -ne $asset) { [pscustomobject]@{Tag=$_.tag_name;Url=$asset.browser_download_url} } "
        "} | Select-Object -First 1; "
        "if ($null -eq $release) { throw 'No Windows VR release asset is currently available.' }; "
        "Write-Host ('Selected PCVR release ' + $release.Tag + '.') -ForegroundColor Cyan; "
        "$nonce=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds(); "
        "$zip=Join-Path $env:TEMP ('sm64coopdx-vr-update-' + $nonce + '.zip'); "
        "$stage=Join-Path $env:TEMP ('sm64coopdx-vr-stage-' + $nonce); "
        "Write-Host 'Downloading PCVR Windows package...'; "
        "Invoke-WebRequest -UseBasicParsing -Headers $headers -Uri $release.Url -OutFile $zip; "
        "Write-Host 'Download complete. Installing files...'; "
        "Expand-Archive -LiteralPath $zip -DestinationPath $stage -Force; "
        "$payload=Get-ChildItem -LiteralPath $stage -Directory | Select-Object -First 1; "
        "if ($null -eq $payload) { $payload=Get-Item -LiteralPath $stage }; "
        "Start-Sleep -Seconds 2; "
        "Get-ChildItem -LiteralPath $payload.FullName -Force | Copy-Item -Destination $install -Recurse -Force; "
        "Write-Host 'Files installed. Launching game...'; "
        "Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue; "
        "Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue; "
        "$game=Start-Process -FilePath (Join-Path $install 'SM64-Co-Op-DX-VR.exe') -PassThru; "
        "Start-Sleep -Milliseconds 500; "
        "if ($game.HasExited) { throw 'The game closed immediately after launch.' }; "
        "Write-Host 'Game launched.' -ForegroundColor Green; "
        "Remove-Item -LiteralPath $MyInvocation.MyCommand.Path -Force -ErrorAction SilentlyContinue; exit 0;",
        escaped_install_dir);
    fclose(script);
    if (!start_update_script(module_path, script_path)) {
        DeleteFileA(script_path);
        return 1;
    }
    return 0;
}
