#include "quest_game_runtime.h"

#include <android/log.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "data/dynos.h"
#include "game/game_init.h"
#include "game/level_update.h"
#include "game/rendering_graph_node.h"
#include "game/save_file.h"
#include "pc/configfile.h"
#include "pc/chat_commands.h"
#include "pc/audio/audio_api.h"
#include "pc/djui/djui.h"
#include "pc/djui/djui_inputbox.h"
#include "pc/djui/djui_panel_pause.h"
#include "pc/djui/djui_unicode.h"
#include "pc/fs/fs.h"
#include "pc/gfx/gfx_dummy.h"
#include "pc/gfx/gfx_opengl.h"
#include "pc/gfx/gfx_pc.h"
#include "pc/lua/smlua.h"
#include "pc/lua/utils/smlua_text_utils.h"
#include "pc/mods/mods.h"
#include "pc/network/network.h"
#include "pc/network/network_player.h"
#include "pc/network/sync_object.h"
#include "pc/platform.h"
#include "pc/rom_assets.h"
#include "pc/vr/vr.h"
#include "audio/external.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "SM64CoopDXVR", __VA_ARGS__)

extern void thread5_game_loop(void *arg);
extern void game_loop_one_iteration(void);
extern void patch_mtx_vr_shared(void);
extern void patch_mtx_vr_projection(float delta, uint32_t eye);
extern void patch_mtx_vr_ui_projection(uint32_t eye);
extern void patch_mtx_before(void);
extern void patch_screen_transition_before(void);
extern void patch_title_screen_before(void);
extern void patch_dialog_before(void);
extern void patch_hud_before(void);
extern void patch_paintings_before(void);
extern void patch_bubble_particles_before(void);
extern void patch_snow_particles_before(void);
extern void patch_djui_before(void);
extern void patch_djui_hud_before(void);
extern void patch_scroll_targets_before(void);
extern void patch_mtx_interpolated(float delta);
extern void patch_screen_transition_interpolated(float delta);
extern void patch_title_screen_interpolated(float delta);
extern void patch_dialog_interpolated(float delta);
extern void patch_hud_interpolated(float delta);
extern void patch_paintings_interpolated(float delta);
extern void patch_bubble_particles_interpolated(float delta);
extern void patch_snow_particles_interpolated(float delta);
extern void patch_djui_interpolated(float delta);
extern void patch_djui_hud(float delta);
extern void patch_scroll_targets_interpolated(float delta);
extern bool gSkipInterpolationTitleScreen;
extern bool gGameInited;
extern float gRenderingDelta;
extern float gFramePercentage;
extern uint8_t gRenderingInterpolated;
extern struct AudioAPI *gAudioApi;
extern void create_next_audio_buffer(s16 *samples, u32 num_samples);

#ifdef VERSION_EU
#define QUEST_SAMPLES_HIGH 560
#define QUEST_SAMPLES_LOW 528
#else
#define QUEST_SAMPLES_HIGH 544
#define QUEST_SAMPLES_LOW 528
#endif

static bool sReady;
static bool sConfigLoaded;
static bool sCompletedGameTick;
static int64_t sLastTickNs;
static s16 sAudioBuffer[QUEST_SAMPLES_HIGH * 2 * 2];
static int64_t sPerfWindowStartNs;
static uint64_t sPerfTickTotalNs;
static uint64_t sPerfTickMaxNs;
static uint32_t sPerfTickCount;
static uint64_t sPerfEyeTotalNs[2];
static uint64_t sPerfEyeMaxNs[2];
static uint32_t sPerfEyeCount[2];
static uint32_t sPerfLastShaderCompiles;
static uint64_t sPerfLastShaderCompileNs;
static uint32_t sPerfLastTextureUploads;
static uint64_t sPerfLastTextureBytes;
static uint64_t sPerfLastTextureUploadNs;
static uint64_t sPerfLastDrawCalls;
static uint64_t sPerfLastTriangles;
static uint64_t sPerfLastVertexUploadBytes;

static void quest_patch_before_game_tick(void) {
    patch_mtx_before();
    patch_screen_transition_before();
    patch_title_screen_before();
    patch_dialog_before();
    patch_hud_before();
    patch_paintings_before();
    patch_bubble_particles_before();
    patch_snow_particles_before();
    patch_djui_before();
    patch_djui_hud_before();
    patch_scroll_targets_before();
}

static void quest_patch_interpolated_frame(float delta) {
    patch_mtx_interpolated(delta);
    patch_screen_transition_interpolated(delta);
    patch_title_screen_interpolated(delta);
    patch_dialog_interpolated(delta);
    patch_hud_interpolated(delta);
    patch_paintings_interpolated(delta);
    patch_bubble_particles_interpolated(delta);
    patch_snow_particles_interpolated(delta);
    // DJUI rewrites its display-list tail during desktop interpolation. On
    // Quest that same list is consumed twice with live per-eye head matrices,
    // which makes the main and pause panels shake violently. Render DJUI once
    // per game tick; the head-locked OpenXR projection remains full-rate.
    patch_djui_hud(delta);
    patch_scroll_targets_interpolated(delta);
}

static void quest_game_buffer_audio(void) {
    if (gAudioApi == NULL) return;
    const int buffered = gAudioApi->buffered();
    const u32 samples = buffered < gAudioApi->get_desired_buffered()
        ? QUEST_SAMPLES_HIGH : QUEST_SAMPLES_LOW;
    for (s32 block = 0; block < 2; ++block) {
        create_next_audio_buffer(
            sAudioBuffer + block * (samples * 2), samples);
    }
    gAudioApi->play((const uint8_t *)sAudioBuffer, 2u * samples * 4u);
}

static int64_t monotonic_ns(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000000000LL + now.tv_nsec;
}

static void quest_game_log_performance_if_ready(int64_t now) {
    uint32_t shaderCompiles = 0;
    uint64_t shaderCompileNs = 0;
    uint32_t textureUploads = 0;
    uint64_t textureBytes = 0;
    uint64_t textureUploadNs = 0;
    uint64_t drawCalls = 0;
    uint64_t triangles = 0;
    uint64_t vertexUploadBytes = 0;
    gfx_opengl_performance_stats_get(
        &shaderCompiles,
        &shaderCompileNs,
        &textureUploads,
        &textureBytes,
        &textureUploadNs,
        &drawCalls,
        &triangles,
        &vertexUploadBytes
    );

    if (sPerfWindowStartNs == 0) {
        sPerfWindowStartNs = now;
        sPerfLastShaderCompiles = shaderCompiles;
        sPerfLastShaderCompileNs = shaderCompileNs;
        sPerfLastTextureUploads = textureUploads;
        sPerfLastTextureBytes = textureBytes;
        sPerfLastTextureUploadNs = textureUploadNs;
        sPerfLastDrawCalls = drawCalls;
        sPerfLastTriangles = triangles;
        sPerfLastVertexUploadBytes = vertexUploadBytes;
        return;
    }
    if (now - sPerfWindowStartNs < 1000000000LL) return;

    const uint32_t newShaders =
        shaderCompiles - sPerfLastShaderCompiles;
    const uint64_t newShaderNs =
        shaderCompileNs - sPerfLastShaderCompileNs;
    const uint32_t newTextures =
        textureUploads - sPerfLastTextureUploads;
    const uint64_t newTextureBytes =
        textureBytes - sPerfLastTextureBytes;
    const uint64_t newTextureNs =
        textureUploadNs - sPerfLastTextureUploadNs;
    const uint64_t newDrawCalls = drawCalls - sPerfLastDrawCalls;
    const uint64_t newTriangles = triangles - sPerfLastTriangles;
    const uint64_t newVertexBytes =
        vertexUploadBytes - sPerfLastVertexUploadBytes;
    const double tickAverageMs = sPerfTickCount > 0
        ? (double)sPerfTickTotalNs / (double)sPerfTickCount / 1000000.0
        : 0.0;
    const double leftAverageMs = sPerfEyeCount[0] > 0
        ? (double)sPerfEyeTotalNs[0] / (double)sPerfEyeCount[0] / 1000000.0
        : 0.0;
    const double rightAverageMs = sPerfEyeCount[1] > 0
        ? (double)sPerfEyeTotalNs[1] / (double)sPerfEyeCount[1] / 1000000.0
        : 0.0;

    LOGI(
        "PERF level=%d area=%d | tick %.2f/%.2f ms | eyes "
        "L %.2f/%.2f R %.2f/%.2f ms | first-use shaders %u/%.2f ms "
        "textures %u/%.2f MiB/%.2f ms | frames %u | draws %.1f tris %.1f vbo %.3f MiB/eye",
        gCurrLevelNum,
        gCurrAreaIndex,
        tickAverageMs,
        (double)sPerfTickMaxNs / 1000000.0,
        leftAverageMs,
        (double)sPerfEyeMaxNs[0] / 1000000.0,
        rightAverageMs,
        (double)sPerfEyeMaxNs[1] / 1000000.0,
        newShaders,
        (double)newShaderNs / 1000000.0,
        newTextures,
        (double)newTextureBytes / (1024.0 * 1024.0),
        (double)newTextureNs / 1000000.0,
        sPerfEyeCount[1],
        sPerfEyeCount[1] > 0
            ? (double)newDrawCalls / (double)sPerfEyeCount[1] / 2.0 : 0.0,
        sPerfEyeCount[1] > 0
            ? (double)newTriangles / (double)sPerfEyeCount[1] / 2.0 : 0.0,
        sPerfEyeCount[1] > 0
            ? (double)newVertexBytes / (1024.0 * 1024.0) /
              (double)sPerfEyeCount[1] / 2.0 : 0.0
    );

    sPerfWindowStartNs = now;
    sPerfTickTotalNs = 0;
    sPerfTickMaxNs = 0;
    sPerfTickCount = 0;
    memset(sPerfEyeTotalNs, 0, sizeof(sPerfEyeTotalNs));
    memset(sPerfEyeMaxNs, 0, sizeof(sPerfEyeMaxNs));
    memset(sPerfEyeCount, 0, sizeof(sPerfEyeCount));
    sPerfLastShaderCompiles = shaderCompiles;
    sPerfLastShaderCompileNs = shaderCompileNs;
    sPerfLastTextureUploads = textureUploads;
    sPerfLastTextureBytes = textureBytes;
    sPerfLastTextureUploadNs = textureUploadNs;
    sPerfLastDrawCalls = drawCalls;
    sPerfLastTriangles = triangles;
    sPerfLastVertexUploadBytes = vertexUploadBytes;
}

void quest_game_load_early_config(void) {
    if (sConfigLoaded) return;
    fs_init(sys_user_path());
    configfile_load();
#ifdef __ANDROID__
    // Standalone ships with English assets and should open directly on the
    // Host / Join / Options menu. Language remains changeable in Options.
    if (configLanguage[0] == '\0') {
        snprintf(configLanguage, MAX_CONFIG_STRING, "%s", "English");
        configfile_save(configfile_name());
    }
#endif
    sConfigLoaded = true;
}

void quest_game_flush_persistent_state(void) {
    if (!sConfigLoaded) return;

    if (gGameInited && gCurrSaveFileNum >= 1
        && gCurrSaveFileNum <= NUM_SAVE_FILES) {
        // Star/key collection normally writes immediately. Force the current
        // slot once more when Android pauses so progress also survives the OS
        // stopping the NativeActivity without the desktop shutdown path.
        save_file_do_save(gCurrSaveFileNum - 1, TRUE);
    }
    configfile_save(configfile_name());
    LOGI("Persistent save and settings flushed to %s.", sys_user_path());
}

unsigned int quest_game_render_scale_percent(void) {
    quest_game_load_early_config();
    if (configVrRenderScale < VR_RENDER_SCALE_MIN
        || configVrRenderScale > VR_RENDER_SCALE_MAX) {
        return 50U;
    }
    // Keep text and controls crisp regardless of gameplay resolution. The
    // OpenXR bootstrap changes swapchains after the menu transition settles.
    if (quest_game_ui_requires_full_quality()) return 100U;
    return configVrRenderScale;
}

bool quest_game_ultra_performance_enabled(void) {
    quest_game_load_early_config();
    return configVrUltraPerformanceMode;
}

bool quest_game_ui_requires_full_quality(void) {
    // The menu, pause panels, and on-screen keyboard contain small text that
    // fixed foveated rendering makes unreadable away from the exact eye
    // center. Start at full quality too, before the main-menu flags settle.
    return !sReady
        || gDjuiInMainMenu
        || gDjuiPanelPauseCreated
        || djui_inputbox_onscreen_keyboard_is_active();
}

unsigned int quest_game_refresh_rate_index(void) {
    quest_game_load_early_config();
    return configVrQuestRefreshRate <= 2
        ? configVrQuestRefreshRate
        : 2;
}

bool quest_game_initialize(void) {
    if (sReady) return true;
    LOGI("Initializing full SM64 game runtime.");

    quest_game_load_early_config();
    // Standalone Quest has no flat/desktop presentation mode. Keep stale
    // configuration copied from another platform from disabling OpenXR or
    // exposing desktop-mirror behavior that cannot exist here.
    configVrAutoStart = true;
    configVrDesktopMirror = false;
    configVrCameraMode = VR_CAMERA_MODE_FIRST_PERSON;
    configSkipIntro = true;
    // OpenXR owns presentation timing on standalone. Do not add desktop
    // VSync or a software frame limiter on top of the headset compositor.
    configWindow.vsync = false;
    configFramerateMode = RRM_UNLIMITED;
    vr_init();
    vr_set_active(true);
    gfx_init(&gfx_dummy_wm_api, &gfx_opengl_api, "SM64 Co-Op DX VR");

    dynos_gfx_init();
    enable_queued_dynos_packs();
    sync_objects_init_system();
    rom_assets_load();
    smlua_text_utils_init();
    mods_init();
    enable_queued_mods();
    dynos_gfx_warmup_loaded_pack_shaders();
    audio_init();
    sound_init();
    if (gAudioApi == NULL || !gAudioApi->init()) {
        LOGI("Android audio device is unavailable; continuing without sound.");
    }
    network_player_init();
    // Match the desktop startup contract before the first game-loop tick.
    // Lua hooks intentionally refuse to execute while this is false; leaving
    // it unset made registered mod commands appear unavailable on standalone.
    gGameInited = true;
    thread5_game_loop(NULL);
    LOGI("Loaded EEPROM progress: A=%d B=%d C=%d D=%d stars.",
         save_file_get_total_star_count(0, COURSE_MIN - 1, COURSE_MAX - 1),
         save_file_get_total_star_count(1, COURSE_MIN - 1, COURSE_MAX - 1),
         save_file_get_total_star_count(2, COURSE_MIN - 1, COURSE_MAX - 1),
         save_file_get_total_star_count(3, COURSE_MIN - 1, COURSE_MAX - 1));
    djui_init();
    djui_unicode_init();
    djui_init_late();
    network_init(NT_NONE, false);

    sLastTickNs = monotonic_ns();
    sCompletedGameTick = false;
    sReady = true;
    LOGI("Full SM64 game runtime initialized.");
    return true;
}

bool quest_game_is_ready(void) { return sReady; }

void quest_game_prepare_vr_frame(void) {
    if (!sReady || !sCompletedGameTick) return;
    const int64_t now = monotonic_ns();
    float delta = (float)(now - sLastTickNs) / 33333333.0f;
    if (delta < 0.0f) delta = 0.0f;
    if (delta > 1.0f) delta = 1.0f;
    gRenderingInterpolated = 1;
    gRenderingDelta = delta;
    gFramePercentage = delta;
    if (gSkipInterpolationTitleScreen) return;
    quest_patch_interpolated_frame(delta);
    patch_mtx_vr_shared();
}

void quest_game_tick(void) {
    if (!sReady) return;
    const int64_t now = monotonic_ns();
    if (now - sLastTickNs < 33333333LL) return;
    sLastTickNs += 33333333LL;
    if (now - sLastTickNs > 100000000LL) sLastTickNs = now;
    const int64_t tickStart = monotonic_ns();
    // Reset the logical game canvas before the engine builds its display
    // list. Leaving the previous portrait eye dimensions here clips/culls the
    // 3D title scene even though the later DJUI projection remains visible.
    gfx_start_frame();
    gRenderingInterpolated = 0;
    quest_patch_before_game_tick();
    exec_queued_chat_command();
    network_update();
    game_loop_one_iteration();
    // Interpolation tables only contain valid current/previous pointers after
    // the engine has completed its first real display-list-producing tick.
    // OpenXR can request frames earlier during Activity recreation.
    sCompletedGameTick = true;
    smlua_update();
    quest_game_buffer_audio();
    const uint64_t tickNs = (uint64_t)(monotonic_ns() - tickStart);
    sPerfTickTotalNs += tickNs;
    if (tickNs > sPerfTickMaxNs) sPerfTickMaxNs = tickNs;
    sPerfTickCount++;
}

bool quest_game_render_eye(uint32_t eye, uint32_t width, uint32_t height) {
    Gfx *commands = gGfxSPTask != NULL
        ? (Gfx *)gGfxSPTask->task.t.data_ptr
        : NULL;
    if (!sReady || commands == NULL || width == 0 || height == 0) return false;
    const int64_t renderStart = monotonic_ns();

    gfx_start_frame();
    gfx_current_dimensions.width = width;
    gfx_current_dimensions.height = height;
    gfx_current_dimensions.aspect_ratio = (float)width / (float)height;
    gfx_current_dimensions.x_adjust_ratio = 1.0f;
    gfx_current_dimensions.x_adjust_4by3 = 0.0f;
    if (!gSkipInterpolationTitleScreen) {
        patch_mtx_vr_projection(1.0f, eye);
    }
    patch_mtx_vr_ui_projection(eye);
    gfx_run(commands);
#ifdef __ANDROID__
    static bool logged_display_list = false;
    if (!logged_display_list) {
        LOGI("Game display list submitted: %u bytes; eye target %ux%u.",
             (unsigned)gGfxSPTask->task.t.data_size, width, height);
        const unsigned error = glGetError();
        if (error != GL_NO_ERROR) LOGI("GLES error after game display list: 0x%04x.", error);
        logged_display_list = true;
    }
#endif
    gfx_end_frame_render();
    if (eye < 2) {
        const uint64_t renderNs =
            (uint64_t)(monotonic_ns() - renderStart);
        sPerfEyeTotalNs[eye] += renderNs;
        if (renderNs > sPerfEyeMaxNs[eye]) {
            sPerfEyeMaxNs[eye] = renderNs;
        }
        sPerfEyeCount[eye]++;
        if (eye == 1) {
            quest_game_log_performance_if_ready(monotonic_ns());
        }
    }
    return true;
}
