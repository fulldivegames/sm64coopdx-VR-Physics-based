#ifndef VR_H
#define VR_H

#include <stdbool.h>
#include <stdint.h>

#define VR_CONTROLLER_COUNT 2

enum VrControllerHand {
    VR_CONTROLLER_LEFT = 0,
    VR_CONTROLLER_RIGHT = 1
};

struct VrControllerState {
    bool gripPoseValid;
    float gripPosition[3];
    float gripRotation[4];
    bool gripLinearVelocityValid;
    float gripLinearVelocity[3];
    bool gripAngularVelocityValid;
    float gripAngularVelocity[3];
    bool aimPoseValid;
    float aimPosition[3];
    float aimRotation[4];
    float trigger;
    float squeeze;
    float thumbstick[2];
    bool primaryButton;
    bool secondaryButton;
    bool menuButton;
    bool thumbstickButton;
};

void vr_init(void);
void vr_begin_frame(void);
void vr_end_frame(void);

bool vr_begin_eye(
    uint32_t eyeIndex,
    uint32_t* width,
    uint32_t* height
);
bool vr_end_eye(uint32_t eyeIndex);
bool vr_mirror_eye(
    uint32_t eyeIndex,
    uint32_t width,
    uint32_t height
);
bool vr_get_render_target_aspect(float* aspect);
bool vr_get_eye_offset(uint32_t eyeIndex, float offset[3]);
bool vr_get_eye_fov(uint32_t eyeIndex, float fov[4]);
bool vr_get_head_rotation(float rotation[4]);
bool vr_get_head_translation(float translation[3]);
bool vr_get_controller_state(
    uint32_t handIndex,
    struct VrControllerState* state
);
bool vr_apply_haptic(
    uint32_t handIndex,
    float amplitude,
    float durationSeconds,
    float frequency
);
void vr_shutdown(void);

void vr_on_graphics_ready(void);

bool vr_is_active(void);
bool vr_set_active(bool active);

#endif
