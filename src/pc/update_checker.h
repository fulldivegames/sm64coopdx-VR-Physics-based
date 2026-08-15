#ifndef _UPDATE_CHECKER_H
#define _UPDATE_CHECKER_H

#include <stdbool.h>

#define VR_RELEASES_URL \
    "https://github.com/fulldivegames/sm64coopdx-VR-Standalone/releases/latest"

enum VrUpdateStatus {
    VR_UPDATE_NOT_CHECKED,
    VR_UPDATE_CHECKING,
    VR_UPDATE_UP_TO_DATE,
    VR_UPDATE_AVAILABLE,
    VR_UPDATE_CHECK_FAILED,
};

extern bool gUpdateMessage;

enum VrUpdateStatus vr_update_get_status(void);
const char* vr_update_get_latest_version(void);
void show_update_popup(void);
void check_for_updates(void);

#endif
