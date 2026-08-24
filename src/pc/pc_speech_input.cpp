#include "pc_speech_input.h"

extern "C" bool pc_speech_recognition_start(void) { return false; }
extern "C" void pc_speech_recognition_cancel(void) { }
extern "C" bool pc_speech_recognition_poll(char* text, size_t textSize) {
    (void)text; (void)textSize; return false;
}
extern "C" bool pc_speech_recognition_is_listening(void) { return false; }
