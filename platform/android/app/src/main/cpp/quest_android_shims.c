#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <android/log.h>

#include "pc/platform.h"
#include "pc/controller/controller_mouse.h"
#include "quest_audio.h"
#include "pc/gfx/gfx_dummy.h"
#include "pc/gfx/gfx_opengl.h"
#include "pc/crash_handler.h"
#include "game/main.h"
#include "game/game_init.h"
#include "pc/configfile.h"
#include "pc/network/version.h"
#include "pc/update_checker.h"

bool mouse_init_ok = false;
bool mouse_relative_enabled = false;
uint32_t mouse_buttons;
int32_t mouse_x, mouse_y;
uint32_t mouse_window_buttons, mouse_window_buttons_pressed, mouse_window_buttons_released;
int32_t mouse_window_x, mouse_window_y;
uint32_t mouse_scroll_timestamp;
float mouse_scroll_x, mouse_scroll_y;
float gRenderingDelta;
uint8_t gRenderingInterpolated;
static char sQuestUserPath[4096] =
    "/sdcard/Android/data/com.fulldivegames.sm64coopdxvr/files";
static const char sQuestSharedModPath[] = "/sdcard/SM64VR/mods";
char gRomFilename[4096] =
    "/sdcard/Android/data/com.fulldivegames.sm64coopdxvr/files/baserom.us.z64";
bool gGameInited;
bool gGfxInited;
bool gUpdateMessage;
static enum VrUpdateStatus sVrUpdateStatus = VR_UPDATE_CHECKING;
static char sVrLatestVersion[32];
bool gRomIsValid = true;
int8_t gDebugLevelSelect;
int8_t gShowProfiler;
int8_t gShowDebugText;
int8_t gResetTimer;
uint32_t gNumVblanks;
int32_t gRumblePakPfs;
OSMesg D_80339BEC;
OSMesgQueue gSIEventMesgQueue;
float gFramePercentage;
uint8_t gLuaVolumeMaster = 127;
uint8_t gLuaVolumeLevel = 127;
uint8_t gLuaVolumeSfx = 127;
uint8_t gLuaVolumeEnv = 127;
struct PcDebug gPcDebug;
char gLastRemoteBhv[256];
float gMasterVolume = 1.0f;
struct AudioAPI *gAudioApi = &audio_quest;
struct GfxWindowManagerAPI *gWindowApi = &gfx_dummy_wm_api;
struct GfxRenderingAPI *gRenderApi = &gfx_opengl_api;
struct GfxWindowManagerAPI *wm_api = &gfx_dummy_wm_api;

static void quest_parse_version(
    const char *value, int *major, int *minor, int *patch);

static void quest_refresh_update_status(void) {
    char status_path[4096];
    snprintf(status_path, sizeof(status_path), "%s/vr-update-status.txt",
             sQuestUserPath);
    FILE *file = fopen(status_path, "rb");
    if (file == NULL) return;
    char result[16] = { 0 };
    char version[32] = { 0 };
    if (fgets(result, sizeof(result), file) == NULL) {
        fclose(file);
        return;
    }
    fgets(version, sizeof(version), file);
    fclose(file);
    result[strcspn(result, "\r\n")] = '\0';
    version[strcspn(version, "\r\n")] = '\0';
    if (strcmp(result, "failed") == 0) {
        sVrUpdateStatus = VR_UPDATE_CHECK_FAILED;
        return;
    }
    if (strcmp(result, "ok") != 0) return;
    snprintf(sVrLatestVersion, sizeof(sVrLatestVersion), "%s", version);
    if (version[0] == '\0') {
        sVrUpdateStatus = VR_UPDATE_UP_TO_DATE;
        gUpdateMessage = false;
        return;
    }
    int currentMajor, currentMinor, currentPatch;
    int latestMajor, latestMinor, latestPatch;
    quest_parse_version(get_vr_version(), &currentMajor, &currentMinor, &currentPatch);
    quest_parse_version(version, &latestMajor, &latestMinor, &latestPatch);
    const bool newer = latestMajor > currentMajor ||
            (latestMajor == currentMajor && latestMinor > currentMinor) ||
            (latestMajor == currentMajor && latestMinor == currentMinor && latestPatch > currentPatch);
    sVrUpdateStatus = newer ? VR_UPDATE_AVAILABLE : VR_UPDATE_UP_TO_DATE;
    gUpdateMessage = newer;
}

enum VrUpdateStatus vr_update_get_status(void) {
    quest_refresh_update_status();
    return sVrUpdateStatus;
}
const char *vr_update_get_latest_version(void) { return sVrLatestVersion; }
void show_update_popup(void) {}
void check_for_updates(void) {}

static void quest_parse_version(const char *value, int *major, int *minor, int *patch) {
    *major = *minor = *patch = 0;
    if (value == NULL) return;
    if (*value == 'v' || *value == 'V') value++;
    sscanf(value, "%d.%d.%d", major, minor, patch);
}


extern void quest_android_request_exit(void);
void game_deinit(void) { gGameInited = false; }
void game_exit(void) {
    if (gGameInited) configfile_save(configfile_name());
    game_deinit();
    quest_android_request_exit();
}
void produce_one_dummy_frame(void (*callback)(void), uint8_t r, uint8_t g, uint8_t b) {
    (void)r; (void)g; (void)b;
    if (callback) callback();
}
void crash_handler_init(void) {}
void set_vblank_handler(int32_t index, struct VblankHandler *handler,
                        OSMesgQueue *queue, OSMesg *message) {
    (void)index; (void)handler; (void)queue; (void)message;
}
extern void gfx_run(Gfx *commands);
static Gfx *sQuestDisplayList;
void quest_game_capture_display_list(Gfx *commands) { sQuestDisplayList = commands; }
Gfx *quest_game_get_display_list(void) { return sQuestDisplayList; }
void send_display_list(struct SPTask *task) {
    if (task) quest_game_capture_display_list((Gfx *)task->task.t.data_ptr);
}
void dispatch_audio_sptask(struct SPTask *task) { (void)task; }

void controller_mouse_read_window(void) {}
void controller_mouse_read_relative(void) {}
void controller_mouse_enter_relative(void) { mouse_relative_enabled = true; }
void controller_mouse_leave_relative(void) { mouse_relative_enabled = false; }
void mouse_on_scroll(float x, float y) { mouse_scroll_x = x; mouse_scroll_y = y; }

void controller_bind_init(void) {}
int translate_sdl_scancode(int scancode) { return scancode; }
const char *translate_bind_to_name(int bind) {
    static char name[24];
    snprintf(name, sizeof(name), "Button %d", bind);
    return name;
}

char *sys_strdup(const char *src) { return src ? strdup(src) : NULL; }
char *sys_strlwr(char *src) {
    for (char *p = src; p && *p; ++p) if (*p >= 'A' && *p <= 'Z') *p += 'a' - 'A';
    return src;
}
int sys_strcasecmp(const char *a, const char *b) { return strcasecmp(a, b); }
void quest_android_set_user_path(const char *path) {
    if (path == NULL || path[0] == '\0') return;
    snprintf(sQuestUserPath, sizeof(sQuestUserPath), "%s", path);
    mkdir(sQuestUserPath, 0700);
    snprintf(gRomFilename, sizeof(gRomFilename), "%s/baserom.us.z64",
             sQuestUserPath);
    __android_log_print(ANDROID_LOG_INFO, "SM64CoopDXVR",
                        "Persistent user path: %s", sQuestUserPath);
}

const char *sys_user_path(void) { return sQuestUserPath; }
const char *quest_android_shared_mod_path(void) { return sQuestSharedModPath; }
const char *sys_resource_path(void) { return sys_user_path(); }
const char *sys_exe_path_dir(void) { return sys_user_path(); }
const char *sys_exe_path_file(void) { return "/proc/self/exe"; }
const char *sys_file_extension(const char *path) {
    if (path == NULL) return "";
    const char *dot = strrchr(path, '.');
    const char *slash = strrchr(path, '/');
    // Dots in an Android package directory (for example
    // com.fulldivegames.sm64coopdxvr/files) are not file extensions. The old
    // implementation made fs_mount() reject the entire user-data directory,
    // so EEPROM/config writes succeeded by absolute path but subsequent reads
    // could never find them through the virtual filesystem.
    return (dot != NULL && (slash == NULL || dot > slash)) ? dot + 1 : "";
}
const char *sys_file_name(const char *path) {
    const char *slash = path ? strrchr(path, '/') : NULL;
    return slash ? slash + 1 : path;
}
void sys_swap_backslashes(char *buffer) {
    for (; buffer && *buffer; ++buffer) if (*buffer == '\\') *buffer = '/';
}
void sys_fatal(const char *fmt, ...) {
    char message[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    __android_log_print(ANDROID_LOG_FATAL, "SM64CoopDXVR", "%s", message);
    abort();
}
