#ifndef VR_H
#define VR_H

#include <stdbool.h>

void vr_init(void);
void vr_shutdown(void);

bool vr_is_active(void);
bool vr_set_active(bool active);

#endif