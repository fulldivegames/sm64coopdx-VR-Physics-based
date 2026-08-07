#pragma once

#include <stdbool.h>

bool vr_openxr_startup(void);
void vr_openxr_shutdown(void);

bool vr_openxr_is_initialized(void);