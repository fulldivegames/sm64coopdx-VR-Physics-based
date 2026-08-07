#pragma once

#include <stdbool.h>

bool vr_openxr_startup(void);
bool vr_openxr_create_session(void);
bool vr_openxr_update(void);

void vr_openxr_shutdown(void);

bool vr_openxr_is_initialized(void);
bool vr_openxr_has_session(void);
bool vr_openxr_is_session_running(void);
