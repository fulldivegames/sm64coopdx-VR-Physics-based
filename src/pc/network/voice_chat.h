#pragma once

#include <PR/ultratypes.h>
#include <stdbool.h>
#include <stddef.h>

struct Packet;

void voice_chat_init(void);
void voice_chat_shutdown(void);
void voice_chat_update(void);
void voice_chat_mix_output(s16* stereoSamples, size_t frameCount);
void voice_chat_receive_capability(struct Packet* packet);
void voice_chat_receive_audio(struct Packet* packet);
bool voice_chat_player_muted(u8 globalIndex);
bool voice_chat_player_speaking(u8 globalIndex);
void voice_chat_set_player_muted(u8 globalIndex, bool muted);
bool voice_chat_session_allowed(void);
u32 voice_chat_input_device_count(void);
const char* voice_chat_input_device_name(u32 index);
void voice_chat_select_input_device(u32 index);
