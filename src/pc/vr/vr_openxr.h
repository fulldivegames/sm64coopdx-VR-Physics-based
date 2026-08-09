#pragma once

#include <stdbool.h>
#include <stdint.h>

struct VrControllerState;

bool vr_openxr_startup(void);
bool vr_openxr_create_session(void);
bool vr_openxr_begin_frame(void);
bool vr_openxr_end_frame(void);
bool vr_openxr_begin_eye(
    uint32_t eyeIndex,
    uint32_t* width,
    uint32_t* height
);
bool vr_openxr_end_eye(uint32_t eyeIndex);
bool vr_openxr_mirror_eye(
    uint32_t eyeIndex,
    uint32_t width,
    uint32_t height
);
bool vr_openxr_get_eye_offset(
    uint32_t eyeIndex,
    float offset[3]
);
bool vr_openxr_get_eye_fov(
    uint32_t eyeIndex,
    float fov[4]
);
bool vr_openxr_get_head_rotation(float rotation[4]);
bool vr_openxr_get_head_translation(float translation[3]);
bool vr_openxr_get_calibrated_head_height(float* height);
uint32_t vr_openxr_get_tracking_origin_generation(void);
void vr_openxr_request_recenter(void);
bool vr_openxr_get_controller_state(
    uint32_t handIndex,
    struct VrControllerState* state
);
bool vr_openxr_apply_haptic(
    uint32_t handIndex,
    float amplitude,
    float durationSeconds,
    float frequency
);

void vr_openxr_shutdown(void);

bool vr_openxr_is_initialized(void);
bool vr_openxr_has_session(void);
bool vr_openxr_is_session_running(void);
