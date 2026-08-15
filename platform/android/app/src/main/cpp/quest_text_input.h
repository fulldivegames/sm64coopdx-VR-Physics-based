#pragma once

#include <android/native_activity.h>

void quest_text_input_initialize(ANativeActivity *activity);
void quest_text_input_shutdown(void);
void quest_text_input_show(const char *initial_text, int max_length);
void quest_text_input_hide(void);
void quest_text_input_poll(void);
char *quest_text_input_get_clipboard(void);
void quest_text_input_set_clipboard(const char *text);
