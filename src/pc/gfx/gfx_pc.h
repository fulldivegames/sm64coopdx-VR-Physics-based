#ifndef GFX_PC_H
#define GFX_PC_H

#include "types.h"
#include "pc/gfx/gfx.h"

enum ShaderFlag {
    SHADER_FLAG_HUE,
    SHADER_FLAG_SATURATION,
    SHADER_FLAG_BRIGHTNESS,
    SHADER_FLAG_CONTRAST,
    SHADER_FLAG_EXPOSURE,
    SHADER_FLAG_DITHERING,
    SHADER_FLAG_POSTERIZATION,
    SHADER_FLAG_SCANLINES,
    SHADER_FLAG_MAX
};

struct GfxRenderingAPI;
struct GfxWindowManagerAPI;

// Aggregate reasons that force the CPU-side triangle buffer to submit. These
// counters are diagnostic only; they do not alter rendering order or state.
enum GfxFlushReason {
    GFX_FLUSH_UNKNOWN = 0,
    GFX_FLUSH_DEPTH_TEST,
    GFX_FLUSH_DEPTH_MASK,
    GFX_FLUSH_ZMODE_DECAL,
    GFX_FLUSH_VIEWPORT,
    GFX_FLUSH_SCISSOR,
    GFX_FLUSH_SHADER,
    GFX_FLUSH_ALPHA,
    GFX_FLUSH_TEXTURE,
    GFX_FLUSH_SAMPLER,
    GFX_FLUSH_BUFFER_FULL,
    GFX_FLUSH_REASON_COUNT,
};

extern Vec3f gLightingDir;
extern Color gLightingColor[2];
extern Color gVertexColor;
extern Color gFogColor;
extern f32 gFogIntensity;

extern bool gFullbright;

extern int gShaderFlags[SHADER_FLAG_MAX];
extern f32 gDefaultShaderFlagValues[SHADER_FLAG_MAX];
extern f32 gShaderFlagValues[SHADER_FLAG_MAX];
extern bool gShaderFlagsEnabled;

#ifdef __cplusplus
extern "C" {
#endif

void gfx_init(struct GfxWindowManagerAPI *wapi, struct GfxRenderingAPI *rapi, const char *window_title);
struct GfxRenderingAPI *gfx_get_current_rendering_api(void);
void gfx_start_frame(void);
// Select the current stereo eye for the CPU-side vertex reuse path. Pass 0
// for the first eye, 1 for the second eye, and any value >= 2 to disable it.
void gfx_set_stereo_eye(uint32_t eye);

// Diagnostic counters for the CPU-side stereo submission path. Counters are
// cumulative for the lifetime of the graphics context and are intended for
// periodic snapshots by the platform-specific performance logger.
struct GfxStereoDiagnostics {
    uint64_t gfx_runs[2];
    uint64_t display_list_commands[2];
    uint64_t display_list_calls[2];
    uint64_t unknown_commands[2];
    uint64_t vertex_commands[2];
    uint64_t vertex_loads[2];
    uint64_t vertex_function_calls[2];
    uint64_t vertex_function_ns[2];
    uint64_t flush_reasons[2][GFX_FLUSH_REASON_COUNT];
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t cache_miss_no_valid;
    uint64_t cache_source_mismatches;
    uint64_t cache_dest_mismatches;
    uint64_t cache_lua_mismatches;
    uint64_t cache_serial_mismatches;
    uint64_t cache_overflow_frames;
    uint64_t cache_count_mismatch_frames;
    uint64_t cache_replay_success_frames;
    uint64_t cache_replay_attempt_frames;
    uint64_t triangles[2];
    uint64_t triangles_clip_rejected[2];
    uint64_t triangles_cull_rejected[2];
    uint64_t triangles_submitted[2];
    uint64_t flush_calls[2];
    uint64_t draw_batches[2];
    uint64_t vertices_lighting[2];
    uint64_t vertices_lighting_engine[2];
    uint64_t vertices_texture_gen[2];
    uint64_t vertices_fog[2];
    uint64_t vertices_fresnel[2];
    uint64_t vertices_packed_normals[2];
    uint64_t opcode_counts[2][256];
};

void gfx_get_stereo_diagnostics(struct GfxStereoDiagnostics *out);
void gfx_run(Gfx *commands);
void gfx_end_frame_render(void);
void gfx_display_frame(void);
void gfx_end_frame(void);
void gfx_shutdown(void);
void gfx_pc_precomp_shader(uint32_t rgb1, uint32_t alpha1, uint32_t rgb2, uint32_t alpha2, uint32_t flags);

#ifdef __cplusplus
}
#endif

#endif
