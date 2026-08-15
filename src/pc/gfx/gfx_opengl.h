#ifndef GFX_OPENGL_H
#define GFX_OPENGL_H

#include "gfx_rendering_api.h"

extern struct GfxRenderingAPI gfx_opengl_api;

#if defined(__ANDROID__)
const char *gfx_opengl_shader_cache_directory_path(void);
void gfx_opengl_shader_cache_reset_stats(void);
void gfx_opengl_shader_cache_get_stats(
    uint32_t *hits, uint32_t *misses, uint32_t *writes);
void gfx_opengl_shader_cache_finish_warmup(void);
void gfx_opengl_performance_stats_get(
    uint32_t *source_compiles,
    uint64_t *source_compile_ns,
    uint32_t *texture_uploads,
    uint64_t *texture_upload_bytes,
    uint64_t *texture_upload_ns,
    uint64_t *draw_calls,
    uint64_t *triangles,
    uint64_t *vertex_upload_bytes
);
#endif

bool gfx_opengl_check_compatibility(void);

#endif
