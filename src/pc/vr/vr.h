#ifndef VR_H
#define VR_H

#include <stdbool.h>
#include <stdint.h>

void vr_init(void);
void vr_begin_frame(void);
void vr_end_frame(void);

bool vr_begin_eye(
    uint32_t eyeIndex,
    uint32_t* width,
    uint32_t* height
);
bool vr_end_eye(uint32_t eyeIndex);
bool vr_get_render_target_aspect(float* aspect);
bool vr_get_head_rotation(float rotation[4]);
bool vr_get_head_translation(float translation[3]);
void vr_shutdown(void);

void vr_on_graphics_ready(void);

bool vr_is_active(void);
bool vr_set_active(bool active);

#endif
