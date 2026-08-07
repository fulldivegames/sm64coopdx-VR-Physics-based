#pragma once

#include <stdbool.h>

bool vr_openxr_startup(void);
bool vr_openxr_create_session(void);
bool vr_openxr_begin_frame(void);
bool vr_openxr_end_frame(void);
bool vr_openxr_get_head_rotation(float rotation[4]);
bool vr_openxr_get_head_translation(float translation[3]);

void vr_openxr_shutdown(void);

bool vr_openxr_is_initialized(void);
bool vr_openxr_has_session(void);
bool vr_openxr_is_session_running(void);
