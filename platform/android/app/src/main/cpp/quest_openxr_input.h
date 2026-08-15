#pragma once

#include <stdbool.h>
#include <openxr/openxr.h>

bool quest_input_initialize(XrInstance instance, XrSession session);
void quest_input_update(XrSession session, XrSpace base_space, XrTime time);
void quest_input_suspend(void);
void quest_input_shutdown(void);
