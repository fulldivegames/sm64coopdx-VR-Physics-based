#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#include <wininet.h>
#else
#include <curl/curl.h>
#endif

#include "update_checker.h"
#include "pc/djui/djui.h"
#include "pc/network/version.h"
#include "pc/loading.h"

#define VR_UPDATE_API_URL \
    "https://api.github.com/repos/fulldivegames/sm64coopdx-VR-Physics-based/releases/latest"
#define VR_UPDATE_RESPONSE_LIMIT (512 * 1024)
#define VR_UPDATE_VERSION_MAX 32

struct Version {
    int maj;
    int min;
    int fix;
};

static char sVersionUpdateTextBuffer[256] = { 0 };
static char sRemoteVersionStr[VR_UPDATE_VERSION_MAX] = { 0 };
static enum VrUpdateStatus sVrUpdateStatus = VR_UPDATE_NOT_CHECKED;

bool gUpdateMessage = false;

enum VrUpdateStatus vr_update_get_status(void) {
    return sVrUpdateStatus;
}

const char* vr_update_get_latest_version(void) {
    return sRemoteVersionStr;
}

void show_update_popup(void) {
    if (sVrUpdateStatus != VR_UPDATE_AVAILABLE ||
        sVersionUpdateTextBuffer[0] == '\0') {
        return;
    }
    djui_popup_create(sVersionUpdateTextBuffer, 3);
}

static bool string_to_version(const char* string, struct Version* version) {
    if (string == NULL || version == NULL) {
        return false;
    }

    const char* cursor = string;
    if (*cursor == 'v' || *cursor == 'V') {
        cursor++;
    }
    if (!isdigit((unsigned char)*cursor)) {
        return false;
    }

    char* end = NULL;
    long components[3] = { 0, 0, 0 };
    for (u32 index = 0; index < 3; index++) {
        components[index] = strtol(cursor, &end, 10);
        if (end == cursor || components[index] < 0 ||
            components[index] > INT_MAX) {
            return false;
        }
        cursor = end;
        if (index < 2 && *cursor == '.') {
            cursor++;
            if (!isdigit((unsigned char)*cursor)) {
                return false;
            }
        } else {
            break;
        }
    }

    if (*cursor != '\0' && *cursor != '-' && *cursor != '+') {
        return false;
    }

    version->maj = (int)components[0];
    version->min = (int)components[1];
    version->fix = (int)components[2];
    return true;
}

static bool is_version_newer(
    struct Version client,
    struct Version remote
) {
    if (remote.maj != client.maj) {
        return remote.maj > client.maj;
    }
    if (remote.min != client.min) {
        return remote.min > client.min;
    }
    return remote.fix > client.fix;
}

static bool parse_remote_version(const char* data) {
    static const char key[] = "\"tag_name\"";
    const char* tag = data == NULL ? NULL : strstr(data, key);
    if (tag == NULL) {
        return false;
    }

    tag = strchr(tag + sizeof(key) - 1, ':');
    if (tag == NULL) {
        return false;
    }
    tag = strchr(tag + 1, '"');
    if (tag == NULL) {
        return false;
    }
    tag++;

    const char* end = strchr(tag, '"');
    if (end == NULL) {
        return false;
    }
    const size_t length = (size_t)(end - tag);
    if (length == 0 || length >= sizeof(sRemoteVersionStr)) {
        return false;
    }

    memcpy(sRemoteVersionStr, tag, length);
    sRemoteVersionStr[length] = '\0';

    struct Version parsed = { 0 };
    if (!string_to_version(sRemoteVersionStr, &parsed)) {
        sRemoteVersionStr[0] = '\0';
        return false;
    }
    return true;
}

#if defined(_WIN32)
static bool get_version_remote(void) {
    bool success = false;
    HINTERNET internet = NULL;
    HINTERNET request = NULL;
    char* response = NULL;
    sRemoteVersionStr[0] = '\0';

    internet = InternetOpenA(
        "SM64-Co-Op-DX-VR/" SM64COOPDX_VR_VERSION,
        INTERNET_OPEN_TYPE_PRECONFIG,
        NULL,
        NULL,
        0
    );
    if (internet == NULL) {
        goto cleanup;
    }

    DWORD timeoutMs = 3000;
    InternetSetOptionA(
        internet,
        INTERNET_OPTION_CONNECT_TIMEOUT,
        &timeoutMs,
        sizeof(timeoutMs)
    );
    InternetSetOptionA(
        internet,
        INTERNET_OPTION_RECEIVE_TIMEOUT,
        &timeoutMs,
        sizeof(timeoutMs)
    );

    static const char headers[] =
        "Accept: application/vnd.github+json\r\n"
        "X-GitHub-Api-Version: 2026-03-10\r\n";
    request = InternetOpenUrlA(
        internet,
        VR_UPDATE_API_URL,
        headers,
        (DWORD)-1L,
        INTERNET_FLAG_RELOAD |
            INTERNET_FLAG_NO_CACHE_WRITE |
            INTERNET_FLAG_SECURE,
        0
    );
    if (request == NULL) {
        goto cleanup;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (!HttpQueryInfoA(
            request,
            HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
            &statusCode,
            &statusSize,
            NULL
        ) || statusCode != HTTP_STATUS_OK) {
        goto cleanup;
    }

    response = malloc(VR_UPDATE_RESPONSE_LIMIT);
    if (response == NULL) {
        goto cleanup;
    }

    size_t total = 0;
    while (total < VR_UPDATE_RESPONSE_LIMIT - 1) {
        const size_t remaining =
            VR_UPDATE_RESPONSE_LIMIT - 1 - total;
        const DWORD requestSize = (DWORD)(
            remaining < 4096 ? remaining : 4096
        );
        DWORD bytesRead = 0;
        if (!InternetReadFile(
                request,
                response + total,
                requestSize,
                &bytesRead
            )) {
            goto cleanup;
        }
        if (bytesRead == 0) {
            break;
        }
        total += bytesRead;
    }
    response[total] = '\0';
    success = parse_remote_version(response);

cleanup:
    free(response);
    if (request != NULL) {
        InternetCloseHandle(request);
    }
    if (internet != NULL) {
        InternetCloseHandle(internet);
    }
    return success;
}
#else
struct DownloadBuffer {
    char* data;
    size_t size;
};

static size_t write_callback(
    char* contents,
    size_t size,
    size_t count,
    void* userdata
) {
    const size_t received = size * count;
    struct DownloadBuffer* buffer = userdata;
    if (received > VR_UPDATE_RESPONSE_LIMIT - buffer->size - 1) {
        return 0;
    }

    char* resized = realloc(
        buffer->data,
        buffer->size + received + 1
    );
    if (resized == NULL) {
        return 0;
    }
    buffer->data = resized;
    memcpy(buffer->data + buffer->size, contents, received);
    buffer->size += received;
    buffer->data[buffer->size] = '\0';
    return received;
}

static bool get_version_remote(void) {
    bool success = false;
    struct DownloadBuffer response = { 0 };
    struct curl_slist* headers = NULL;
    sRemoteVersionStr[0] = '\0';

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return false;
    }
    CURL* curl = curl_easy_init();
    if (curl == NULL) {
        curl_global_cleanup();
        return false;
    }

    headers = curl_slist_append(
        headers,
        "Accept: application/vnd.github+json"
    );
    headers = curl_slist_append(
        headers,
        "X-GitHub-Api-Version: 2026-03-10"
    );
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                     "SM64-Co-Op-DX-VR/" SM64COOPDX_VR_VERSION);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_URL, VR_UPDATE_API_URL);

    if (curl_easy_perform(curl) == CURLE_OK &&
        response.data != NULL) {
        success = parse_remote_version(response.data);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(response.data);
    curl_global_cleanup();
    return success;
}
#endif

void check_for_updates(void) {
    LOADING_SCREEN_MUTEX(
        loading_screen_set_segment_text("Checking For VR Updates")
    );

    gUpdateMessage = false;
    sVersionUpdateTextBuffer[0] = '\0';
    sVrUpdateStatus = VR_UPDATE_CHECKING;

    if (!get_version_remote()) {
        sVrUpdateStatus = VR_UPDATE_CHECK_FAILED;
        printf("[VR] Could not check GitHub for updates.\n");
        return;
    }

    struct Version client = { 0 };
    struct Version remote = { 0 };
    if (!string_to_version(get_vr_version(), &client) ||
        !string_to_version(sRemoteVersionStr, &remote)) {
        sVrUpdateStatus = VR_UPDATE_CHECK_FAILED;
        printf("[VR] GitHub returned an invalid release version.\n");
        return;
    }

    if (is_version_newer(client, remote)) {
        sVrUpdateStatus = VR_UPDATE_AVAILABLE;
        gUpdateMessage = true;
        snprintf(
            sVersionUpdateTextBuffer,
            sizeof(sVersionUpdateTextBuffer),
            "\\#ffffa0\\SM64 Co-Op DX VR update available!\n"
            "\\#dcdcdc\\Latest: %s\nInstalled: %s",
            sRemoteVersionStr,
            get_vr_version()
        );
        printf(
            "[VR] Update available: %s (installed: %s).\n",
            sRemoteVersionStr,
            get_vr_version()
        );
    } else {
        sVrUpdateStatus = VR_UPDATE_UP_TO_DATE;
        printf(
            "[VR] SM64 Co-Op DX VR %s is up to date.\n",
            get_vr_version()
        );
    }
}
