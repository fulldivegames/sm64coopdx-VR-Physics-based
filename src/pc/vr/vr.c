#include <stdio.h>

#include "pc/configfile.h"
#include "pc/vr/vr.h"
#include "pc/vr/vr_openxr.h"

static bool sVrActive = false;
static bool sGraphicsReady = false;
static float sRenderTargetAspect = 0.0f;
/* Session loss is reported from vr_begin_frame, while rendering callbacks
 * for that frame may still be in flight. Defer object destruction until
 * vr_end_frame so closing SteamVR cannot invalidate the active frame. */
static bool sVrShutdownPending = false;

/* SteamVR can accept the OpenXR instance before its scene application is
 * ready to accept the OpenGL graphics binding. In that window it reports
 * VRInitError_Init_Retry and no application frames are submitted. Keep the
 * instance alive and retry the graphics-session handoff for a short,
 * bounded period instead of dropping back to flat mode immediately. */
static bool sVrSessionRetryPending = false;
static unsigned int sVrSessionRetryFrames = 0;
static unsigned int sVrSessionRetryAttempts = 0;

#define VR_SESSION_RETRY_DELAY_FRAMES 30
#define VR_SESSION_RETRY_MAX_ATTEMPTS 8

static bool sPhysicalPunchPending[VR_CONTROLLER_COUNT] = {
    false,
    false
};

static void vr_clear_physical_punches(void) {
    for (uint32_t hand = 0;
         hand < VR_CONTROLLER_COUNT;
         hand++) {
        sPhysicalPunchPending[hand] = false;
    }
}

static void vr_clear_session_retry(void) {
    sVrSessionRetryPending = false;
    sVrSessionRetryFrames = 0;
    sVrSessionRetryAttempts = 0;
}

static bool vr_schedule_session_retry(void) {
    if (!sGraphicsReady ||
        configGraphicsBackend != GAPI_GL ||
        !vr_openxr_is_initialized() ||
        vr_openxr_has_session() ||
        sVrSessionRetryAttempts >= VR_SESSION_RETRY_MAX_ATTEMPTS) {
        return false;
    }

    sVrSessionRetryPending = true;
    sVrSessionRetryFrames = VR_SESSION_RETRY_DELAY_FRAMES;

    printf(
        "[VR] OpenXR graphics-session handoff is not ready; "
        "retrying in %u frames (attempt %u/%u).\n",
        sVrSessionRetryFrames,
        sVrSessionRetryAttempts + 1,
        VR_SESSION_RETRY_MAX_ATTEMPTS
    );

    return true;
}

void vr_init(void) {
    printf("[VR] VR subsystem initialized.\n");
    printf("[VR] Launch in VR setting: %s\n",
           configVrAutoStart ? "ON" : "OFF");

    vr_set_active(configVrAutoStart);
}

static bool vr_start_graphics_session(void) {
    if (!sGraphicsReady) {
        return true;
    }

    if (configGraphicsBackend != GAPI_GL) {
        printf(
            "[VR] VR currently requires the "
            "OpenGL graphics backend.\n"
        );

        return false;
    }

    if (!vr_openxr_create_session()) {
        return false;
    }

    return true;
}

void vr_on_graphics_ready(void) {
    sGraphicsReady = true;

    printf("[VR] Game graphics context is ready.\n");

    if (!sVrActive) {
        return;
    }

    if (!vr_start_graphics_session()) {
        if (vr_schedule_session_retry()) {
            printf(
                "[VR] Keeping the OpenXR instance alive while SteamVR "
                "finishes scene initialization.\n"
            );
            return;
        }

        printf(
            "[VR] Could not attach VR to the "
            "game graphics context.\n"
        );

        vr_openxr_shutdown();

        sVrActive = false;
        sVrShutdownPending = false;
        vr_clear_session_retry();

        printf("[VR] VR mode state: OFF\n");
    }
}

static void vr_handle_openxr_failure(void) {
    printf(
        "[VR] OpenXR stopped unexpectedly. "
        "Returning to flat mode; deferring cleanup until the frame is complete.\n"
    );

    sRenderTargetAspect = 0.0f;
    sVrActive = false;
    sVrShutdownPending = true;
    vr_clear_session_retry();
    vr_clear_physical_punches();

    printf("[VR] VR mode state: OFF\n");
}

void vr_begin_frame(void) {
    // A motion gesture is consumed during the following game update. Never
    // carry one across unrelated render/game frames (for example, from a
    // menu into a level).
    vr_clear_physical_punches();

    if (!sVrActive) {
        return;
    }

    if (sVrSessionRetryPending) {
        if (sVrSessionRetryFrames > 0) {
            sVrSessionRetryFrames--;
            return;
        }

        sVrSessionRetryAttempts++;

        if (vr_start_graphics_session()) {
            printf(
                "[VR] OpenXR graphics-session handoff completed after "
                "%u attempt(s).\n",
                sVrSessionRetryAttempts
            );
            vr_clear_session_retry();
        } else if (!vr_schedule_session_retry()) {
            printf(
                "[VR] OpenXR graphics-session handoff did not become ready "
                "within the bounded retry window.\n"
            );
            vr_handle_openxr_failure();
            return;
        }

        if (sVrSessionRetryPending) {
            return;
        }
    }

    if (!vr_openxr_begin_frame()) {
        vr_handle_openxr_failure();
    }
}

void vr_end_frame(void) {
    if (sVrActive && !vr_openxr_end_frame()) {
        vr_handle_openxr_failure();
    }

    if (sVrShutdownPending) {
        /* Session-loss cleanup is intentionally delayed until all rendering
         * callbacks for this frame have returned. */
        vr_openxr_shutdown();
        sVrShutdownPending = false;
    }
}
bool vr_begin_eye(
    uint32_t eyeIndex,
    uint32_t* width,
    uint32_t* height
) {
    if (!sVrActive ||
        !vr_openxr_begin_eye(eyeIndex, width, height)) {
        return false;
    }

    sRenderTargetAspect =
        (float)*width / (float)*height;
    return true;
}

bool vr_end_eye(uint32_t eyeIndex) {
    if (!sVrActive) {
        return false;
    }

    const bool ended = vr_openxr_end_eye(eyeIndex);
    sRenderTargetAspect = 0.0f;
    return ended;
}

bool vr_mirror_eye(
    uint32_t eyeIndex,
    uint32_t width,
    uint32_t height
) {
    if (!sVrActive) {
        return false;
    }

    return vr_openxr_mirror_eye(
        eyeIndex,
        width,
        height
    );
}

bool vr_get_render_target_aspect(float* aspect) {
    if (!sVrActive ||
        aspect == NULL ||
        sRenderTargetAspect <= 0.0f) {
        return false;
    }

    *aspect = sRenderTargetAspect;
    return true;
}

bool vr_get_eye_offset(uint32_t eyeIndex, float offset[3]) {
    if (!sVrActive || offset == NULL) {
        return false;
    }

    return vr_openxr_get_eye_offset(eyeIndex, offset);
}

bool vr_get_eye_fov(uint32_t eyeIndex, float fov[4]) {
    if (!sVrActive || fov == NULL) {
        return false;
    }

    return vr_openxr_get_eye_fov(eyeIndex, fov);
}

bool vr_get_head_rotation(float rotation[4]) {
    if (!sVrActive || rotation == NULL) {
        return false;
    }

    return vr_openxr_get_head_rotation(rotation);
}

bool vr_get_head_translation(float translation[3]) {
    if (!sVrActive || translation == NULL) {
        return false;
    }

    return vr_openxr_get_head_translation(translation);
}

bool vr_get_calibrated_head_height(float* height) {
    if (!sVrActive || height == NULL) {
        return false;
    }

    return vr_openxr_get_calibrated_head_height(height);
}

uint32_t vr_get_tracking_origin_generation(void) {
    return vr_openxr_get_tracking_origin_generation();
}

bool vr_get_controller_state(
    uint32_t handIndex,
    struct VrControllerState* state
) {
    if (!sVrActive || state == NULL) {
        return false;
    }

    return vr_openxr_get_controller_state(handIndex, state);
}

bool vr_apply_haptic(
    uint32_t handIndex,
    float amplitude,
    float durationSeconds,
    float frequency
) {
    if (!sVrActive) {
        return false;
    }

    return vr_openxr_apply_haptic(
        handIndex,
        amplitude,
        durationSeconds,
        frequency
    );
}

void vr_queue_physical_punch(uint32_t handIndex) {
    if (sVrActive && handIndex < VR_CONTROLLER_COUNT) {
        sPhysicalPunchPending[handIndex] = true;
    }
}

bool vr_consume_physical_punch(uint32_t handIndex) {
    if (!sVrActive || handIndex >= VR_CONTROLLER_COUNT) {
        return false;
    }

    const bool pending = sPhysicalPunchPending[handIndex];
    sPhysicalPunchPending[handIndex] = false;
    return pending;
}

void vr_shutdown(void) {
    vr_openxr_shutdown();

    vr_clear_physical_punches();
    sGraphicsReady = false;
    sRenderTargetAspect = 0.0f;
    sVrActive = false;
    sVrShutdownPending = false;
    vr_clear_session_retry();

    printf("[VR] VR subsystem shut down.\n");
}

bool vr_is_active(void) {
    return sVrActive;
}

bool vr_set_active(bool active) {
    if (!active) {
        vr_openxr_shutdown();

        vr_clear_physical_punches();
        sRenderTargetAspect = 0.0f;
        sVrActive = false;
        sVrShutdownPending = false;
        vr_clear_session_retry();

        printf("[VR] VR mode state: OFF\n");

        return true;
    }

    if (sVrActive) {
        printf("[VR] VR mode state: ON\n");
        return true;
    }

    if (sVrShutdownPending) {
        vr_openxr_shutdown();
        sVrShutdownPending = false;
    }

    vr_clear_session_retry();

    printf(
        "[VR] Starting persistent "
        "OpenXR context...\n"
    );

    if (!vr_openxr_startup()) {
        sVrActive = false;

        printf(
            "[VR] VR activation failed. "
            "Staying in flat mode.\n"
        );

        printf("[VR] VR mode state: OFF\n");

        return false;
    }

    sVrActive = true;
    sVrShutdownPending = false;

    if (!vr_start_graphics_session()) {
        if (vr_schedule_session_retry()) {
            /* The OpenXR instance is valid, but SteamVR has not finished
             * accepting the game's OpenGL graphics binding yet. Keep VR
             * active while vr_begin_frame performs the bounded retry. */
            vr_request_recenter();
            printf("[VR] VR mode state: ON (waiting for SteamVR handoff)\n");
            return true;
        }

        vr_openxr_shutdown();

        sVrActive = false;
        sVrShutdownPending = false;
        vr_clear_session_retry();

        printf(
            "[VR] VR graphics session could "
            "not be created.\n"
        );

        printf("[VR] VR mode state: OFF\n");
        return false;
    }
    // Establish the application's neutral headset height and yaw from the
    // first valid pose after OpenXR reaches FOCUSED. Valid poses can arrive
    // earlier while the headset is still waking, which previously left a
    // stale vertical offset until the player used the runtime's recenter.
    vr_request_recenter();

    printf("[VR] VR mode state: ON\n");

    return true;
}

void vr_request_recenter(void) {
    if (!sVrActive) {
        return;
    }

    vr_openxr_request_recenter();
}

void vr_request_horizontal_recenter(void) {
    if (!sVrActive) {
        return;
    }

    vr_openxr_request_horizontal_recenter();
}
