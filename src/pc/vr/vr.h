#ifndef VR_H
#define VR_H

#include <stdbool.h>

void vr_init(void);
void vr_begin_frame(void);
void vr_end_frame(void);

bool vr_get_head_rotation(float rotation[4]);
bool vr_get_head_translation(float translation[3]);
void vr_shutdown(void);

void vr_on_graphics_ready(void);

bool vr_is_active(void);
bool vr_set_active(bool active);

#endif
