#include <stdio.h>

#include "pc/configfile.h"
#include "pc/vr/vr.h"
#include "pc/vr/vr_openxr.h"

static bool sVrActive = false;

void vr_init(void) {
    printf("[VR] VR subsystem initialized.\n");
    printf("[VR] Launch in VR setting: %s\n",
           configVrAutoStart ? "ON" : "OFF");

    vr_set_active(configVrAutoStart);
}

void vr_shutdown(void) {
    vr_openxr_shutdown();

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
        "[VR] Starting persistent OpenXR context...\n"
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

    printf("[VR] VR mode state: ON\n");

    return true;
}