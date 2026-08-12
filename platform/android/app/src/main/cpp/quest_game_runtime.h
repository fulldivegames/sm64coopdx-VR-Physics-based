#pragma once

#include <stdbool.h>
#include <stdint.h>

bool quest_game_initialize(void);
void quest_game_load_early_config(void);
unsigned int quest_game_render_scale_percent(void);
void quest_game_tick(void);
bool quest_game_render_eye(uint32_t eye, uint32_t width, uint32_t height);
void quest_game_prepare_vr_frame(void);
bool quest_game_is_ready(void);
