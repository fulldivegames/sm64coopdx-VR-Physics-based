#include <stdio.h>

#include "pc/configfile.h"
#include "pc/vr/vr.h"
#include "pc/vr/vr_openxr.h"

static bool sVrActive = false;
static bool sGraphicsReady = false;

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
        printf(
            "[VR] Could not attach VR to the "
            "game graphics context.\n"
        );

        vr_openxr_shutdown();

        sVrActive = false;

        printf("[VR] VR mode state: OFF\n");
    }
}

static void vr_handle_openxr_failure(void) {
    printf(
        "[VR] OpenXR stopped unexpectedly. "
        "Returning to flat mode.\n"
    );

    vr_openxr_shutdown();
    sVrActive = false;

    printf("[VR] VR mode state: OFF\n");
}

void vr_begin_frame(void) {
    if (!sVrActive) {
        return;
    }

    if (!vr_openxr_begin_frame()) {
        vr_handle_openxr_failure();
    }
}

void vr_end_frame(void) {
    if (!sVrActive) {
        return;
    }

    if (!vr_openxr_end_frame()) {
        vr_handle_openxr_failure();
    }
}

void vr_shutdown(void) {
    vr_openxr_shutdown();

    sGraphicsReady = false;
    sVrActive = false;

    printf("[VR] VR subsystem shut down.\n");
}

bool vr_is_active(void) {
    return sVrActive;
}

bool vr_set_active(bool active) {
    if (!active) {
        vr_openxr_shutdown();

        sVrActive = false;

        printf("[VR] VR mode state: OFF\n");

        return true;
    }

    if (sVrActive) {
        printf("[VR] VR mode state: ON\n");
        return true;
    }

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

    if (!vr_start_graphics_session()) {
        vr_openxr_shutdown();

        sVrActive = false;

        printf(
            "[VR] VR graphics session could "
            "not be created.\n"
        );

        printf("[VR] VR mode state: OFF\n");

        return false;
    }

    printf("[VR] VR mode state: ON\n");

    return true;
}
