#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

bool pc_speech_recognition_start(void);
void pc_speech_recognition_cancel(void);
bool pc_speech_recognition_poll(char* text, size_t textSize);
bool pc_speech_recognition_is_listening(void);

#ifdef __cplusplus
}
#endif
