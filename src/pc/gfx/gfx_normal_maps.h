#ifndef GFX_NORMAL_MAPS_H
#define GFX_NORMAL_MAPS_H

#include <stdbool.h>
#include <stdint.h>

// Normal maps are optional player resources. They are enabled only after the
// ROM assets have loaded, and only an exact builtin texture-name association
// can request one.
void gfx_normal_maps_on_rom_loaded(void);
void gfx_normal_maps_set_pending_texture_name(const char *name);
void gfx_normal_maps_clear_pending_texture_name(void);
bool gfx_normal_maps_load_pending(uint8_t **rgba32, int *width, int *height,
                                  int expected_width, int expected_height);

#endif
