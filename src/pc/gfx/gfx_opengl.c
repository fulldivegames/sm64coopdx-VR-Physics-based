#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _LANGUAGE_C
# define _LANGUAGE_C
#endif
#include <PR/gbi.h>

#ifdef __MINGW32__
# define FOR_WINDOWS 1
#else
# define FOR_WINDOWS 0
#endif

#if FOR_WINDOWS || defined(OSX_BUILD)
# define GLEW_STATIC
# include <GL/glew.h>
#endif

#define GL_GLEXT_PROTOTYPES 1

#if defined(__ANDROID__)
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
#include <android/log.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>
#elif defined(USE_GLES)
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengles2.h>
#else
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#endif

#include "../platform.h"
#include "../configfile.h"
#include "gfx_cc.h"
#include "gfx_rendering_api.h"
#include "gfx_pc.h"
#include "../vr/vr.h"

#define TEX_CACHE_STEP 512
#define SHADER_LOOKUP_CACHE_SIZE 256

#if defined(__ANDROID__)
#define SHADER_BINARY_CACHE_MAGIC 0x534D5652u
// Increment whenever generated GLSL or the on-disk format changes.
#define SHADER_BINARY_CACHE_VERSION 5u
#define SHADER_BINARY_CACHE_MAX_BYTES (1024u * 1024u)

struct ShaderBinaryCacheHeader {
    uint32_t magic;
    uint32_t version;
    uint64_t shader_hash;
    uint64_t driver_hash;
    uint32_t binary_format;
    uint32_t binary_length;
};

static int sShaderBinaryCacheSupported = -1;
static uint32_t sShaderBinaryCacheHits;
static uint32_t sShaderBinaryCacheMisses;
static uint32_t sShaderBinaryCacheWrites;
static bool sShaderBinaryCacheWriteWarningShown;
static uint32_t sShaderSourceCompileCount;
static uint64_t sShaderSourceCompileNs;
static uint32_t sTextureUploadCount;
static uint64_t sTextureUploadBytes;
static uint64_t sTextureUploadNs;
static uint64_t sDrawCallCount;
static uint64_t sTriangleCount;
static uint64_t sVertexUploadBytes;

const char *gfx_opengl_shader_cache_directory_path(void) {
    static char selected_directory[SYS_MAX_PATH];
    static bool selected;
    if (selected) return selected_directory;
    selected = true;

    const char *shared = quest_android_shared_shader_cache_path();
    if (shared != NULL && shared[0] != '\0') {
        mkdir(shared, 0770);
        char probe_path[SYS_MAX_PATH];
        snprintf(probe_path, sizeof(probe_path), "%s/.native-write-test", shared);
        FILE *probe = fopen(probe_path, "wb");
        if (probe != NULL) {
            fclose(probe);
            remove(probe_path);
            snprintf(selected_directory, sizeof(selected_directory), "%s", shared);
            __android_log_print(ANDROID_LOG_INFO, "SM64CoopDXVR",
                "Persistent GLES shader binaries use %s.", selected_directory);
            return selected_directory;
        }
        __android_log_print(ANDROID_LOG_WARN, "SM64CoopDXVR",
            "Native writes to %s failed (%d); using app storage for shader binaries.",
            shared, errno);
    }

    snprintf(selected_directory, sizeof(selected_directory),
             "%s/shader-cache", sys_user_path());
    mkdir(selected_directory, 0700);
    __android_log_print(ANDROID_LOG_INFO, "SM64CoopDXVR",
        "Persistent GLES shader binaries use fallback %s.", selected_directory);
    return selected_directory;
}

static uint64_t gfx_opengl_monotonic_ns(void) {
    struct timespec value;
    clock_gettime(CLOCK_MONOTONIC, &value);
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
           (uint64_t)value.tv_nsec;
}

static uint64_t gfx_opengl_hash_string(uint64_t hash, const char *value) {
    if (value == NULL) return hash;
    while (*value != '\0') {
        hash ^= (uint8_t)*value++;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t gfx_opengl_driver_hash(void) {
    static uint64_t driver_hash;
    if (driver_hash != 0) return driver_hash;

    driver_hash = UINT64_C(1469598103934665603);
    driver_hash = gfx_opengl_hash_string(
        driver_hash, (const char *)glGetString(GL_VENDOR));
    driver_hash = gfx_opengl_hash_string(
        driver_hash, (const char *)glGetString(GL_RENDERER));
    driver_hash = gfx_opengl_hash_string(
        driver_hash, (const char *)glGetString(GL_VERSION));
    driver_hash ^= SHADER_BINARY_CACHE_VERSION;
    driver_hash *= UINT64_C(1099511628211);
    return driver_hash;
}

static bool gfx_opengl_program_binary_supported(void) {
    if (sShaderBinaryCacheSupported < 0) {
        GLint format_count = 0;
        glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &format_count);
        sShaderBinaryCacheSupported = format_count > 0;
        __android_log_print(
            ANDROID_LOG_INFO, "SM64CoopDXVR",
            "GLES program-binary shader cache %s (%d format(s)).",
            sShaderBinaryCacheSupported ? "enabled" : "unavailable",
            format_count
        );
    }
    return sShaderBinaryCacheSupported != 0;
}

static bool gfx_opengl_shader_binary_path(
    char *path, size_t path_size, uint64_t shader_hash, bool temporary
) {
    const char *directory = gfx_opengl_shader_cache_directory_path();
    if (directory == NULL || directory[0] == '\0') return false;
    return snprintf(
        path,
        path_size,
        "%s/%016" PRIx64 ".glbin%s",
        directory,
        shader_hash,
        temporary ? ".tmp" : ""
    ) > 0;
}

static bool gfx_opengl_try_program_binary(
    GLuint program, uint64_t shader_hash
) {
    if (!gfx_opengl_program_binary_supported()) return false;

    char path[4096];
    if (!gfx_opengl_shader_binary_path(
            path, sizeof(path), shader_hash, false)) {
        sShaderBinaryCacheMisses++;
        return false;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        sShaderBinaryCacheMisses++;
        return false;
    }

    struct ShaderBinaryCacheHeader header = { 0 };
    const bool valid_header =
        fread(&header, sizeof(header), 1, file) == 1 &&
        header.magic == SHADER_BINARY_CACHE_MAGIC &&
        header.version == SHADER_BINARY_CACHE_VERSION &&
        header.shader_hash == shader_hash &&
        header.driver_hash == gfx_opengl_driver_hash() &&
        header.binary_length > 0 &&
        header.binary_length <= SHADER_BINARY_CACHE_MAX_BYTES;
    if (!valid_header) {
        fclose(file);
        remove(path);
        sShaderBinaryCacheMisses++;
        return false;
    }

    void *binary = malloc(header.binary_length);
    if (binary == NULL ||
        fread(binary, header.binary_length, 1, file) != 1) {
        free(binary);
        fclose(file);
        remove(path);
        sShaderBinaryCacheMisses++;
        return false;
    }
    fclose(file);

    glProgramBinary(
        program,
        (GLenum)header.binary_format,
        binary,
        (GLsizei)header.binary_length
    );
    free(binary);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        remove(path);
        sShaderBinaryCacheMisses++;
        return false;
    }

    sShaderBinaryCacheHits++;
    return true;
}

static void gfx_opengl_save_program_binary(
    GLuint program, uint64_t shader_hash
) {
    if (!gfx_opengl_program_binary_supported()) return;

    GLint binary_length = 0;
    glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &binary_length);
    if (binary_length <= 0 ||
        binary_length > (GLint)SHADER_BINARY_CACHE_MAX_BYTES) return;

    void *binary = malloc((size_t)binary_length);
    if (binary == NULL) return;

    GLsizei written = 0;
    GLenum format = 0;
    glGetProgramBinary(
        program, binary_length, &written, &format, binary);
    if (written <= 0 || written > binary_length) {
        free(binary);
        return;
    }

    char path[4096];
    char temporary_path[4096];
    if (!gfx_opengl_shader_binary_path(
            path, sizeof(path), shader_hash, false) ||
        !gfx_opengl_shader_binary_path(
            temporary_path, sizeof(temporary_path), shader_hash, true)) {
        free(binary);
        return;
    }

    const struct ShaderBinaryCacheHeader header = {
        .magic = SHADER_BINARY_CACHE_MAGIC,
        .version = SHADER_BINARY_CACHE_VERSION,
        .shader_hash = shader_hash,
        .driver_hash = gfx_opengl_driver_hash(),
        .binary_format = (uint32_t)format,
        .binary_length = (uint32_t)written,
    };

    FILE *file = fopen(temporary_path, "wb");
    const bool wrote = file != NULL &&
        fwrite(&header, sizeof(header), 1, file) == 1 &&
        fwrite(binary, (size_t)written, 1, file) == 1 &&
        fflush(file) == 0;
    if (file != NULL) fclose(file);
    free(binary);

    if (!wrote || (remove(path), rename(temporary_path, path)) != 0) {
        remove(temporary_path);
        if (!sShaderBinaryCacheWriteWarningShown) {
            __android_log_print(ANDROID_LOG_ERROR, "SM64CoopDXVR",
                "Could not persist GLES shader binaries in %s (error %d).",
                gfx_opengl_shader_cache_directory_path(), errno);
            sShaderBinaryCacheWriteWarningShown = true;
        }
        return;
    }
    sShaderBinaryCacheWrites++;
}

void gfx_opengl_shader_cache_reset_stats(void) {
    sShaderBinaryCacheHits = 0;
    sShaderBinaryCacheMisses = 0;
    sShaderBinaryCacheWrites = 0;
}

void gfx_opengl_shader_cache_get_stats(
    uint32_t *hits, uint32_t *misses, uint32_t *writes
) {
    if (hits != NULL) *hits = sShaderBinaryCacheHits;
    if (misses != NULL) *misses = sShaderBinaryCacheMisses;
    if (writes != NULL) *writes = sShaderBinaryCacheWrites;
}

void gfx_opengl_shader_cache_finish_warmup(void) {
    // Linking can be deferred internally by the mobile driver. The dedicated
    // post-ROM boot is the one safe time to pay that cost before gameplay.
    glFinish();
}

void gfx_opengl_performance_stats_get(
    uint32_t *source_compiles,
    uint64_t *source_compile_ns,
    uint32_t *texture_uploads,
    uint64_t *texture_upload_bytes,
    uint64_t *texture_upload_ns,
    uint64_t *draw_calls,
    uint64_t *triangles,
    uint64_t *vertex_upload_bytes
) {
    if (source_compiles != NULL) {
        *source_compiles = sShaderSourceCompileCount;
    }
    if (source_compile_ns != NULL) {
        *source_compile_ns = sShaderSourceCompileNs;
    }
    if (texture_uploads != NULL) {
        *texture_uploads = sTextureUploadCount;
    }
    if (texture_upload_bytes != NULL) {
        *texture_upload_bytes = sTextureUploadBytes;
    }
    if (texture_upload_ns != NULL) {
        *texture_upload_ns = sTextureUploadNs;
    }
    if (draw_calls != NULL) *draw_calls = sDrawCallCount;
    if (triangles != NULL) *triangles = sTriangleCount;
    if (vertex_upload_bytes != NULL) {
        *vertex_upload_bytes = sVertexUploadBytes;
    }
}
#endif

struct ShaderProgram {
    uint64_t hash;
    GLuint opengl_program_id;
    uint8_t num_inputs;
    bool used_textures[2];
    uint8_t num_floats;
    GLint attrib_locations[7];
    GLint uniform_locations[9];
    uint8_t attrib_sizes[7];
    uint8_t num_attribs;
    bool used_noise;
    bool used_lightmap;
    bool world_geometry;
};

struct GLTexture {
    GLuint gltex;
    GLfloat size[2];
    bool filter;
    bool sampler_initialized;
    bool sampler_linear;
};

static struct ShaderProgram shader_program_pool[CC_MAX_SHADERS];
static uint16_t shader_program_pool_size = 0;
static uint16_t shader_program_pool_index = 0;
static struct ShaderProgram *shader_lookup_cache[
    SHADER_LOOKUP_CACHE_SIZE
];
static GLuint opengl_vbo;
static GLuint opengl_vao;

static int tex_cache_size = 0;
static int num_textures = 0;
static struct GLTexture *tex_cache = NULL;

static struct ShaderProgram *opengl_prg = NULL;
static struct GLTexture *opengl_tex[2];
static int opengl_curtex = 0;

static uint32_t frame_count;

static bool gfx_opengl_z_is_from_0_to_1(void) {
    return false;
}

static void gfx_opengl_vertex_array_set_attribs(struct ShaderProgram *prg) {
    size_t num_floats = prg->num_floats;
    size_t pos = 0;

    for (int i = 0; i < prg->num_attribs; i++) {
        glEnableVertexAttribArray(prg->attrib_locations[i]);
        glVertexAttribPointer(prg->attrib_locations[i], prg->attrib_sizes[i], GL_FLOAT, GL_FALSE, num_floats * sizeof(float), (void *) (pos * sizeof(float)));
        pos += prg->attrib_sizes[i];
    }
}

static inline void gfx_opengl_set_shader_uniforms(struct ShaderProgram *prg) {
    if (prg->used_noise) { glUniform1f(prg->uniform_locations[4], (float)frame_count); }
    if (prg->used_lightmap) { glUniform3f(prg->uniform_locations[5], gVertexColor[0] / 255.0f, gVertexColor[1] / 255.0f, gVertexColor[2] / 255.0f); }
    if (prg->world_geometry) {
        if (vr_is_active() && (configVrBrightness != 100U
                              || configVrSaturation != 100U
                              || configVrContrast != 100U)) {
            int vrShaderFlags[SHADER_FLAG_MAX];
            float vrShaderValues[SHADER_FLAG_MAX];
            for (int i = 0; i < SHADER_FLAG_MAX; ++i) {
                vrShaderFlags[i] = gShaderFlags[i];
                vrShaderValues[i] = gShaderFlagValues[i];
            }
            vrShaderFlags[SHADER_FLAG_BRIGHTNESS] = 1;
            const float baseBrightness =
                gShaderFlags[SHADER_FLAG_BRIGHTNESS]
                    ? gShaderFlagValues[SHADER_FLAG_BRIGHTNESS]
                    : 1.0f;
            vrShaderValues[SHADER_FLAG_BRIGHTNESS] =
                baseBrightness * (float)configVrBrightness / 100.0f;
            vrShaderFlags[SHADER_FLAG_SATURATION] = 1;
            const float baseSaturation =
                gShaderFlags[SHADER_FLAG_SATURATION]
                    ? gShaderFlagValues[SHADER_FLAG_SATURATION]
                    : 1.0f;
            vrShaderValues[SHADER_FLAG_SATURATION] =
                baseSaturation * (float)configVrSaturation / 100.0f;
            vrShaderFlags[SHADER_FLAG_CONTRAST] = 1;
            const float baseContrast =
                gShaderFlags[SHADER_FLAG_CONTRAST]
                    ? gShaderFlagValues[SHADER_FLAG_CONTRAST]
                    : 1.0f;
            vrShaderValues[SHADER_FLAG_CONTRAST] =
                baseContrast * (float)configVrContrast / 100.0f;
            glUniform1iv(prg->uniform_locations[6], SHADER_FLAG_MAX, vrShaderFlags);
            glUniform1fv(prg->uniform_locations[7], SHADER_FLAG_MAX, vrShaderValues);
        } else {
            glUniform1iv(prg->uniform_locations[6], SHADER_FLAG_MAX, gShaderFlags);
            glUniform1fv(prg->uniform_locations[7], SHADER_FLAG_MAX, gShaderFlagValues);
        }
    }

    // Three-point N64 filtering performs three texture samples for most
    // filtered pixels. Ultra mode retains bilinear sampler filtering while
    // avoiding that extra fragment cost; it never removes geometry or actors.
    const int activeFiltering =
        configVrUltraPerformanceMode && configFiltering == 2
            ? 1
            : configFiltering;
    glUniform1i(prg->uniform_locations[8], activeFiltering);
}

static inline void gfx_opengl_set_texture_uniforms(struct ShaderProgram *prg, const int tile) {
    if (prg->used_textures[tile] && opengl_tex[tile]) {
        // Three-point filtering samples exact texel centers itself. Keeping
        // GL_LINEAR enabled made every one of its three taps a four-texel
        // bilinear lookup, multiplying bandwidth in terrain-heavy views.
        // GL_NEAREST produces the same center samples for the three-point
        // reconstruction. Mode 1 remains ordinary hardware bilinear.
        const bool samplerLinear =
            opengl_tex[tile]->filter && configFiltering == 1;
        if (!opengl_tex[tile]->sampler_initialized ||
            opengl_tex[tile]->sampler_linear != samplerLinear) {
            glActiveTexture(GL_TEXTURE0 + tile);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                            samplerLinear ? GL_LINEAR : GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                            samplerLinear ? GL_LINEAR : GL_NEAREST);
            opengl_tex[tile]->sampler_initialized = true;
            opengl_tex[tile]->sampler_linear = samplerLinear;
        }
        glUniform2f(prg->uniform_locations[tile*2 + 0], opengl_tex[tile]->size[0], opengl_tex[tile]->size[1]);
        glUniform1i(prg->uniform_locations[tile*2 + 1], opengl_tex[tile]->filter);
    }
}

static void gfx_opengl_unload_shader(struct ShaderProgram *old_prg) {
    if (old_prg != NULL) {
        for (int i = 0; i < old_prg->num_attribs; i++)
            glDisableVertexAttribArray(old_prg->attrib_locations[i]);
        if (old_prg == opengl_prg)
            opengl_prg = NULL;
    } else {
        opengl_prg = NULL;
    }
}

static void gfx_opengl_load_shader(struct ShaderProgram *new_prg) {
    opengl_prg = new_prg;
    glUseProgram(new_prg->opengl_program_id);
    gfx_opengl_vertex_array_set_attribs(new_prg);
    gfx_opengl_set_shader_uniforms(new_prg);
    gfx_opengl_set_texture_uniforms(new_prg, 0);
    gfx_opengl_set_texture_uniforms(new_prg, 1);
}

static void append_str(char *buf, size_t *len, const char *str) {
    while (*str != '\0') buf[(*len)++] = *str++;
}

static void append_line(char *buf, size_t *len, const char *str) {
    while (*str != '\0') buf[(*len)++] = *str++;
    buf[(*len)++] = '\n';
}

static const char *shader_item_to_str(uint32_t item, bool with_alpha, bool only_alpha, bool inputs_have_alpha, bool hint_single_element) {
    if (!only_alpha) {
        switch (item) {
            case SHADER_0:
                return with_alpha ? "vec4(0.0, 0.0, 0.0, 0.0)" : "vec3(0.0, 0.0, 0.0)";
            case SHADER_1:
                return with_alpha ? "vec4(1.0, 1.0, 1.0, 1.0)" : "vec3(1.0, 1.0, 1.0)";
            case SHADER_INPUT_1:
                return with_alpha || !inputs_have_alpha ? "vInput1" : "vInput1.rgb";
            case SHADER_INPUT_2:
                return with_alpha || !inputs_have_alpha ? "vInput2" : "vInput2.rgb";
            case SHADER_INPUT_3:
                return with_alpha || !inputs_have_alpha ? "vInput3" : "vInput3.rgb";
            case SHADER_INPUT_4:
                return with_alpha || !inputs_have_alpha ? "vInput4" : "vInput4.rgb";
            case SHADER_INPUT_5:
                return with_alpha || !inputs_have_alpha ? "vInput5" : "vInput5.rgb";
            case SHADER_INPUT_6:
                return with_alpha || !inputs_have_alpha ? "vInput6" : "vInput6.rgb";
            case SHADER_INPUT_7:
                return with_alpha || !inputs_have_alpha ? "vInput7" : "vInput7.rgb";
            case SHADER_INPUT_8:
                return with_alpha || !inputs_have_alpha ? "vInput8" : "vInput8.rgb";
            case SHADER_TEXEL0:
                return with_alpha ? "texVal0" : "texVal0.rgb";
            case SHADER_TEXEL0A:
                return hint_single_element ? "texVal0.a" :
                    (with_alpha ? "vec4(texVal0.a, texVal0.a, texVal0.a, texVal0.a)" : "vec3(texVal0.a, texVal0.a, texVal0.a)");
            case SHADER_TEXEL1:
                return with_alpha ? "texVal1" : "texVal1.rgb";
            case SHADER_TEXEL1A:
                return hint_single_element ? "texVal1.a" :
                    (with_alpha ? "vec4(texVal1.a, texVal1.a, texVal1.a, texVal1.a)" : "vec3(texVal1.a, texVal1.a, texVal1.a)");
            case SHADER_COMBINED:
                return with_alpha ? "texel" : "texel.rgb";
            case SHADER_COMBINEDA:
                return hint_single_element ? "texel.a" :
                    (with_alpha ? "vec4(texel.a, texel.a, texel.a, texel.a)" : "vec3(texel.a, texel.a, texel.a)");
            case SHADER_NOISE:
                return with_alpha ? "vec4(noise)" : "vec3(noise)";
        }
    } else {
        switch (item) {
            case SHADER_0:
                return "0.0";
            case SHADER_1:
                return "1.0";
            case SHADER_INPUT_1:
                return "vInput1.a";
            case SHADER_INPUT_2:
                return "vInput2.a";
            case SHADER_INPUT_3:
                return "vInput3.a";
            case SHADER_INPUT_4:
                return "vInput4.a";
            case SHADER_INPUT_5:
                return "vInput5.a";
            case SHADER_INPUT_6:
                return "vInput6.a";
            case SHADER_INPUT_7:
                return "vInput7.a";
            case SHADER_INPUT_8:
                return "vInput8.a";
            case SHADER_TEXEL0:
                return "texVal0.a";
            case SHADER_TEXEL0A:
                return "texVal0.a";
            case SHADER_TEXEL1:
                return "texVal1.a";
            case SHADER_TEXEL1A:
                return "texVal1.a";
            case SHADER_COMBINED:
                return "texel.a";
            case SHADER_COMBINEDA:
                return "texel.a";
            case SHADER_NOISE:
                return "noise.a";
        }
    }
    return "unknown";
}

static void append_formula(char *buf, size_t *len, uint8_t* cmd, bool do_single, bool do_multiply, bool do_mix, bool with_alpha, bool only_alpha, bool opt_alpha) {
    if (do_single) {
        append_str(buf, len, shader_item_to_str(cmd[only_alpha * 4 + 3], with_alpha, only_alpha, opt_alpha, false));
    } else if (do_multiply) {
        append_str(buf, len, shader_item_to_str(cmd[only_alpha * 4 + 0], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, " * ");
        append_str(buf, len, shader_item_to_str(cmd[only_alpha * 4 + 2], with_alpha, only_alpha, opt_alpha, true));
    } else if (do_mix) {
        append_str(buf, len, "mix(");
        append_str(buf, len, shader_item_to_str(cmd[only_alpha * 4 + 1], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, ", ");
        append_str(buf, len, shader_item_to_str(cmd[only_alpha * 4 + 0], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, ", ");
        append_str(buf, len, shader_item_to_str(cmd[only_alpha * 4 + 2], with_alpha, only_alpha, opt_alpha, true));
        append_str(buf, len, ")");
    } else {
        append_str(buf, len, "(");
        append_str(buf, len, shader_item_to_str(cmd[only_alpha * 4 + 0], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, " - ");
        append_str(buf, len, shader_item_to_str(cmd[only_alpha * 4 + 1], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, ") * ");
        append_str(buf, len, shader_item_to_str(cmd[only_alpha * 4 + 2], with_alpha, only_alpha, opt_alpha, true));
        append_str(buf, len, " + ");
        append_str(buf, len, shader_item_to_str(cmd[only_alpha * 4 + 3], with_alpha, only_alpha, opt_alpha, false));
    }
}

static struct ShaderProgram *gfx_opengl_create_and_load_new_shader(struct ColorCombiner* cc) {
    struct CCFeatures ccf = { 0 };
    gfx_cc_get_features(cc, &ccf);

    bool opt_alpha = cc->cm.use_alpha;
    bool opt_fog = cc->cm.use_fog;
    bool opt_texture_edge = cc->cm.texture_edge;
    bool opt_2cycle = cc->cm.use_2cycle;
    bool opt_light_map = cc->cm.light_map;
    bool world_geometry = cc->cm.world_geometry;

#ifdef USE_GLES
    bool opt_dither = false;
#else
    bool opt_dither = cc->cm.use_dither;
#endif

    char vs_buf[8192];
    char fs_buf[8192];
    size_t vs_len = 0;
    size_t fs_len = 0;
    size_t num_floats = 4;

    // Vertex shader
#ifdef USE_GLES
    append_line(vs_buf, &vs_len, "#version 100");
#else
    append_line(vs_buf, &vs_len, "#version 120");
#endif
    append_line(vs_buf, &vs_len, "attribute vec4 aVtxPos;");
    for (int t = 0; t < 2; t++) {
        if (ccf.used_textures[t]) {
            vs_len += sprintf(vs_buf + vs_len, "attribute vec2 aTexCoord%d;\n", t);
            vs_len += sprintf(vs_buf + vs_len, "varying vec2 vTexCoord%d;\n", t);
            num_floats += 2;
        }
    }
    if (opt_fog) {
        append_line(vs_buf, &vs_len, "attribute vec4 aFog;");
        append_line(vs_buf, &vs_len, "varying vec4 vFog;");
        num_floats += 4;
    }
    if (opt_light_map) {
        append_line(vs_buf, &vs_len, "attribute vec2 aLightMap;");
        append_line(vs_buf, &vs_len, "varying vec2 vLightMap;");
        num_floats += 2;
    }
    for (int i = 0; i < ccf.num_inputs; i++) {
        vs_len += sprintf(vs_buf + vs_len, "attribute vec%d aInput%d;\n", opt_alpha ? 4 : 3, i + 1);
        vs_len += sprintf(vs_buf + vs_len, "varying vec%d vInput%d;\n", opt_alpha ? 4 : 3, i + 1);
        num_floats += opt_alpha ? 4 : 3;
    }
    append_line(vs_buf, &vs_len, "void main() {");
    for (int t = 0; t < 2; t++) {
        if (ccf.used_textures[t]) {
            vs_len += sprintf(vs_buf + vs_len, "vTexCoord%d = aTexCoord%d;\n", t, t);
        }
    }
    if (opt_fog) {
        append_line(vs_buf, &vs_len, "vFog = aFog;");
    }
    if (opt_light_map) {
        append_line(vs_buf, &vs_len, "vLightMap = aLightMap;");
    }
    for (int i = 0; i < ccf.num_inputs; i++) {
        vs_len += sprintf(vs_buf + vs_len, "vInput%d = aInput%d;\n", i + 1, i + 1);
    }
    append_line(vs_buf, &vs_len, "gl_Position = aVtxPos;");
    append_line(vs_buf, &vs_len, "}");

    // Fragment shader
#ifdef USE_GLES
    append_line(fs_buf, &fs_len, "#version 100");
    append_line(fs_buf, &fs_len, "precision mediump float;");
#else
    append_line(fs_buf, &fs_len, "#version 120");
#endif

    for (int t = 0; t < 2; t++) {
        if (ccf.used_textures[t]) {
            fs_len += sprintf(fs_buf + fs_len, "varying vec2 vTexCoord%d;\n", t);
        }
    }
    if (opt_fog) {
        append_line(fs_buf, &fs_len, "varying vec4 vFog;");
    }
    if (opt_light_map) {
        append_line(fs_buf, &fs_len, "varying vec2 vLightMap;");
    }
    for (int i = 0; i < ccf.num_inputs; i++) {
        fs_len += sprintf(fs_buf + fs_len, "varying vec%d vInput%d;\n", opt_alpha ? 4 : 3, i + 1);
    }

    for (int t = 0; t < 2; t++) {
        if (ccf.used_textures[t]) {
            fs_len += sprintf(fs_buf + fs_len, "uniform sampler2D uTex%d;\n", t);
            fs_len += sprintf(fs_buf + fs_len, "uniform vec2 uTex%dSize;\n", t);
            fs_len += sprintf(fs_buf + fs_len, "uniform bool uTex%dFilter;\n", t);
        }
    }

    // 3 point texture filtering
    // Original author: ArthurCarvalho
    // Modified GLSL implementation by twinaphex, mupen64plus-libretro project.
    if (ccf.used_textures[0] || ccf.used_textures[1]) {
        append_line(fs_buf, &fs_len, "#define TEX_OFFSET(off) texture2D(tex, texCoord - (off)/texSize)");
        append_line(fs_buf, &fs_len, "vec4 filter3point(in sampler2D tex, in vec2 texCoord, in vec2 texSize) {");
        append_line(fs_buf, &fs_len, "    vec2 offset = fract(texCoord*texSize - vec2(0.5));");
        append_line(fs_buf, &fs_len, "    offset -= step(1.0, offset.x + offset.y);");
        append_line(fs_buf, &fs_len, "    vec4 c0 = TEX_OFFSET(offset);");
        append_line(fs_buf, &fs_len, "    vec4 c1 = TEX_OFFSET(vec2(offset.x - sign(offset.x), offset.y));");
        append_line(fs_buf, &fs_len, "    vec4 c2 = TEX_OFFSET(vec2(offset.x, offset.y - sign(offset.y)));");
        append_line(fs_buf, &fs_len, "    return c0 + abs(offset.x)*(c1-c0) + abs(offset.y)*(c2-c0);");
        append_line(fs_buf, &fs_len, "}");
        append_line(fs_buf, &fs_len, "vec4 sampleTex(in sampler2D tex, in vec2 uv, in vec2 texSize, in bool dofilter, in int filter) {");
        append_line(fs_buf, &fs_len, "    if (dofilter && filter == 2)");
        append_line(fs_buf, &fs_len, "        return filter3point(tex, uv, texSize);");
        append_line(fs_buf, &fs_len, "    else");
        append_line(fs_buf, &fs_len, "        return texture2D(tex, uv);");
        append_line(fs_buf, &fs_len, "}");
    }

    if (world_geometry) {
        append_line(fs_buf, &fs_len, "float dither4x4(vec2 position, float brightness) {");
        append_line(fs_buf, &fs_len, "    int x = int(mod(position.x, 4.0));");
        append_line(fs_buf, &fs_len, "    int y = int(mod(position.y, 4.0));");
        append_line(fs_buf, &fs_len, "    int index = x + y * 4;");
        append_line(fs_buf, &fs_len, "    float limit = 0.0;");
        append_line(fs_buf, &fs_len, "    if (x < 8) {");
        append_line(fs_buf, &fs_len, "        if (index == 0) limit = 0.0625;");
        append_line(fs_buf, &fs_len, "        if (index == 1) limit = 0.5625;");
        append_line(fs_buf, &fs_len, "        if (index == 2) limit = 0.1875;");
        append_line(fs_buf, &fs_len, "        if (index == 3) limit = 0.6875;");
        append_line(fs_buf, &fs_len, "        if (index == 4) limit = 0.8125;");
        append_line(fs_buf, &fs_len, "        if (index == 5) limit = 0.3125;");
        append_line(fs_buf, &fs_len, "        if (index == 6) limit = 0.9375;");
        append_line(fs_buf, &fs_len, "        if (index == 7) limit = 0.4375;");
        append_line(fs_buf, &fs_len, "        if (index == 8) limit = 0.25;");
        append_line(fs_buf, &fs_len, "        if (index == 9) limit = 0.75;");
        append_line(fs_buf, &fs_len, "        if (index == 10) limit = 0.125;");
        append_line(fs_buf, &fs_len, "        if (index == 11) limit = 0.625;");
        append_line(fs_buf, &fs_len, "        if (index == 12) limit = 1.0;");
        append_line(fs_buf, &fs_len, "        if (index == 13) limit = 0.5;");
        append_line(fs_buf, &fs_len, "        if (index == 14) limit = 0.875;");
        append_line(fs_buf, &fs_len, "        if (index == 15) limit = 0.375;");
        append_line(fs_buf, &fs_len, "    }");
        append_line(fs_buf, &fs_len, "    return brightness < limit ? 0.0 : 1.0;");
        append_line(fs_buf, &fs_len, "}");

        append_line(fs_buf, &fs_len, "vec3 rgb2hsv(vec3 c) {");
        append_line(fs_buf, &fs_len, "    vec4 K = vec4(0.0, -1.0/3.0, 2.0/3.0, -1.0);");
        append_line(fs_buf, &fs_len, "    vec4 p = mix(vec4(c.bg, K.wz),");
        append_line(fs_buf, &fs_len, "                 vec4(c.gb, K.xy),");
        append_line(fs_buf, &fs_len, "                 step(c.b, c.g));");
        append_line(fs_buf, &fs_len, "    vec4 q = mix(vec4(p.xyw, c.r),");
        append_line(fs_buf, &fs_len, "                 vec4(c.r, p.yzx),");
        append_line(fs_buf, &fs_len, "                 step(p.x, c.r));");
        append_line(fs_buf, &fs_len, "    float d = q.x - min(q.w, q.y);");
        append_line(fs_buf, &fs_len, "    float e = 1.0e-10;");
        append_line(fs_buf, &fs_len, "    return vec3(");
        append_line(fs_buf, &fs_len, "        abs(q.z + (q.w - q.y) / (6.0 * d + e)), // hue");
        append_line(fs_buf, &fs_len, "        d / (q.x + e),                          // saturation");
        append_line(fs_buf, &fs_len, "        q.x                                     // value");
        append_line(fs_buf, &fs_len, "    );");
        append_line(fs_buf, &fs_len, "}");
        append_line(fs_buf, &fs_len, "");
        append_line(fs_buf, &fs_len, "vec3 hsv2rgb(vec3 c) {");
        append_line(fs_buf, &fs_len, "    vec3 p = abs(fract(c.xxx + vec3(0.0, 2.0/3.0, 1.0/3.0)) * 6.0 - 3.0);");
        append_line(fs_buf, &fs_len, "    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);");
        append_line(fs_buf, &fs_len, "}");
    }

    if ((opt_alpha && opt_dither) || ccf.do_noise) {
        append_line(fs_buf, &fs_len, "uniform float uFrameCount;");

        append_line(fs_buf, &fs_len, "float random(in vec3 value) {");
        append_line(fs_buf, &fs_len, "    float random = dot(sin(value), vec3(12.9898, 78.233, 37.719));");
        append_line(fs_buf, &fs_len, "    return fract(sin(random) * 143758.5453);");
        append_line(fs_buf, &fs_len, "}");
    }

    if (opt_light_map) {
        append_line(fs_buf, &fs_len, "uniform vec3 uLightmapColor;");
    }

    if (world_geometry) {
        fs_len += sprintf(fs_buf + fs_len, "uniform int uShaderFlags[%d];\n", SHADER_FLAG_MAX);
        fs_len += sprintf(fs_buf + fs_len, "uniform float uShaderFlagValues[%d];\n", SHADER_FLAG_MAX);
    }

    append_line(fs_buf, &fs_len, "uniform int uFilter;");

    append_line(fs_buf, &fs_len, "void main() {");

    if ((opt_alpha && opt_dither) || ccf.do_noise) {
        append_line(fs_buf, &fs_len, "float noise = floor(random(floor(vec3(gl_FragCoord.xy, uFrameCount))) + 0.5);");
    }

    if (ccf.used_textures[0]) {
        append_line(fs_buf, &fs_len, "vec4 texVal0 = sampleTex(uTex0, vTexCoord0, uTex0Size, uTex0Filter, uFilter);");
    }
    if (ccf.used_textures[1]) {
        if (opt_light_map) {
            append_line(fs_buf, &fs_len, "vec4 texVal1 = sampleTex(uTex1, vLightMap, uTex1Size, uTex1Filter, uFilter);");
            append_line(fs_buf, &fs_len, "texVal0.rgb *= uLightmapColor.rgb;");
            append_line(fs_buf, &fs_len, "texVal1.rgb = texVal1.rgb * texVal1.rgb + texVal1.rgb;");
        } else {
            append_line(fs_buf, &fs_len, "vec4 texVal1 = sampleTex(uTex1, vTexCoord1, uTex1Size, uTex1Filter, uFilter);");
        }
    }

    append_str(fs_buf, &fs_len, (opt_alpha) ? "vec4 texel = " : "vec3 texel = ");
    for (int i = 0; i < (opt_2cycle + 1); i++) {
        u8* cmd = &cc->shader_commands[i * 8];
        if (!ccf.color_alpha_same[i] && opt_alpha) {
            append_str(fs_buf, &fs_len, "vec4(");
            append_formula(fs_buf, &fs_len, cmd, ccf.do_single[i*2+0], ccf.do_multiply[i*2+0], ccf.do_mix[i*2+0], false, false, true);
            append_str(fs_buf, &fs_len, ", ");
            append_formula(fs_buf, &fs_len, cmd, ccf.do_single[i*2+1], ccf.do_multiply[i*2+1], ccf.do_mix[i*2+1], true, true, true);
            append_str(fs_buf, &fs_len, ")");
        } else {
            append_formula(fs_buf, &fs_len, cmd, ccf.do_single[i*2+0], ccf.do_multiply[i*2+0], ccf.do_mix[i*2+0], opt_alpha, false, opt_alpha);
        }
        append_line(fs_buf, &fs_len, ";");

        if (i == 0 && opt_2cycle) {
            append_str(fs_buf, &fs_len, "texel = ");
        }
    }

    if (opt_texture_edge && opt_alpha) {
        append_line(fs_buf, &fs_len, "if (texel.a > 0.3) texel.a = 1.0; else discard;");
    }

    // TODO discard if alpha is 0?

    if (world_geometry) {
        // hue
        append_line(fs_buf, &fs_len, "if (uShaderFlags[0] == 1) {");
        append_line(fs_buf, &fs_len, "vec3 hsv = rgb2hsv(texel.rgb);");
        append_line(fs_buf, &fs_len, "hsv.x = fract(hsv.x + uShaderFlagValues[0]);");
        append_line(fs_buf, &fs_len, "vec3 finalColor = hsv2rgb(hsv);");
        append_line(fs_buf, &fs_len, "texel.rgb = finalColor;");
        append_line(fs_buf, &fs_len, "}");

        // saturation
        append_line(fs_buf, &fs_len, "if (uShaderFlags[1] == 1) {");
        append_line(fs_buf, &fs_len, "const vec3 w = vec3(0.2125, 0.7154, 0.0721);");
        append_line(fs_buf, &fs_len, "vec3 intensity = vec3(dot(texel.rgb, w));");
        append_line(fs_buf, &fs_len, "texel.rgb = mix(intensity, texel.rgb, uShaderFlagValues[1]);");
        append_line(fs_buf, &fs_len, "}");

        // brightness
        append_line(fs_buf, &fs_len, "if (uShaderFlags[2] == 1) {");
        append_line(fs_buf, &fs_len, "texel.rgb *= uShaderFlagValues[2];");
        append_line(fs_buf, &fs_len, "}");

        // contrast
        append_line(fs_buf, &fs_len, "if (uShaderFlags[3] == 1) {");
        append_line(fs_buf, &fs_len, "texel.rgb = 0.5 + uShaderFlagValues[3] * (texel.rgb - 0.5);");
        append_line(fs_buf, &fs_len, "}");

        // exposure
        append_line(fs_buf, &fs_len, "if (uShaderFlags[4] == 1) {");
        append_line(fs_buf, &fs_len, "texel.rgb = texel.rgb + (uShaderFlagValues[4] - 2.0) * texel.rgb + texel.rgb;");
        append_line(fs_buf, &fs_len, "}");

        // dithering
        append_line(fs_buf, &fs_len, "if (uShaderFlags[5] == 1) {");
        append_line(fs_buf, &fs_len, "texel.rgb *= dither4x4(gl_FragCoord.xy, dot(texel.rgb, vec3(0.299, 0.587, 0.114)));");
        append_line(fs_buf, &fs_len, "}");

        // posterization
        append_line(fs_buf, &fs_len, "if (uShaderFlags[6] == 1) {");
        append_line(fs_buf, &fs_len, "float levels = max(1.0, uShaderFlagValues[6]);");
        append_line(fs_buf, &fs_len, "texel.rgb = floor(texel.rgb * levels) / levels;");
        append_line(fs_buf, &fs_len, "}");

        // scan lines
        append_line(fs_buf, &fs_len, "if (uShaderFlags[7] == 1) {");
        append_line(fs_buf, &fs_len, "float scan = sin(gl_FragCoord.y * 1.5) * 0.04;");
        append_line(fs_buf, &fs_len, "texel.rgb -= scan * uShaderFlagValues[7];");
        append_line(fs_buf, &fs_len, "}");
    }

    if (opt_fog) {
        if (opt_alpha) {
            append_line(fs_buf, &fs_len, "texel = vec4(mix(texel.rgb, vFog.rgb, vFog.a), texel.a);");
        } else {
            append_line(fs_buf, &fs_len, "texel = mix(texel, vFog.rgb, vFog.a);");
        }
    }

    if (opt_alpha && opt_dither) {
        append_line(fs_buf, &fs_len, "texel.a *= noise;");
    }

    if (opt_alpha) {
        append_line(fs_buf, &fs_len, "gl_FragColor = texel;");
    } else {
        append_line(fs_buf, &fs_len, "gl_FragColor = vec4(texel, 1.0);");
    }
    append_line(fs_buf, &fs_len, "}");

    vs_buf[vs_len] = '\0';
    fs_buf[fs_len] = '\0';

    /*puts("Vertex shader:");
    puts(vs_buf);
    puts("Fragment shader:");
    puts(fs_buf);
    puts("End");*/

    const GLchar *sources[2] = { vs_buf, fs_buf };
    const GLint lengths[2] = { vs_len, fs_len };
    GLint success;

    GLuint shader_program = glCreateProgram();
#if defined(__ANDROID__)
    const bool loaded_program_binary =
        gfx_opengl_try_program_binary(shader_program, cc->hash);
#else
    const bool loaded_program_binary = false;
#endif

    if (!loaded_program_binary) {
#if defined(__ANDROID__)
        const uint64_t source_compile_start = gfx_opengl_monotonic_ns();
#endif
        GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertex_shader, 1, &sources[0], &lengths[0]);
        glCompileShader(vertex_shader);
        glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char error_log[1024];
            GLsizei log_length = 0;
            fprintf(stderr, "Vertex shader compilation failed\n");
            glGetShaderInfoLog(
                vertex_shader,
                (GLsizei)sizeof(error_log),
                &log_length,
                error_log
            );
            fprintf(stderr, "%s\n", error_log);
#ifdef __ANDROID__
            sys_fatal("Vertex shader compilation failed: %s\nSource:\n%s", error_log, vs_buf);
#else
            sys_fatal("vertex shader compilation failed (see terminal)");
#endif
        }

        GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragment_shader, 1, &sources[1], &lengths[1]);
        glCompileShader(fragment_shader);
        glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char error_log[1024];
            GLsizei log_length = 0;
            fprintf(stderr, "Fragment shader compilation failed\n");
            glGetShaderInfoLog(
                fragment_shader,
                (GLsizei)sizeof(error_log),
                &log_length,
                error_log
            );
            fprintf(stderr, "%s\n", error_log);
#ifdef __ANDROID__
            sys_fatal("Fragment shader compilation failed: %s\nSource:\n%s", error_log, fs_buf);
#else
            sys_fatal("fragment shader compilation failed (see terminal)");
#endif
        }

#if defined(__ANDROID__)
        if (gfx_opengl_program_binary_supported()) {
            glProgramParameteri(
                shader_program,
                GL_PROGRAM_BINARY_RETRIEVABLE_HINT,
                GL_TRUE
            );
        }
#endif
        glAttachShader(shader_program, vertex_shader);
        glAttachShader(shader_program, fragment_shader);
        glLinkProgram(shader_program);
        glGetProgramiv(shader_program, GL_LINK_STATUS, &success);
        if (!success) {
            char error_log[1024];
            GLsizei log_length = 0;
            fprintf(stderr, "Shader program linking failed\n");
            glGetProgramInfoLog(
                shader_program,
                (GLsizei)sizeof(error_log),
                &log_length,
                error_log
            );
            fprintf(stderr, "%s\n", error_log);
            sys_fatal("shader program linking failed (see terminal)");
        }

        // Once linked, the program owns the compiled code. Keeping every
        // source shader object alive wastes memory in long modded sessions.
        glDetachShader(shader_program, vertex_shader);
        glDetachShader(shader_program, fragment_shader);
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
#if defined(__ANDROID__)
        gfx_opengl_save_program_binary(shader_program, cc->hash);
        sShaderSourceCompileCount++;
        sShaderSourceCompileNs +=
            gfx_opengl_monotonic_ns() - source_compile_start;
#endif
    }

    size_t cnt = 0;

    struct ShaderProgram *prg = &shader_program_pool[shader_program_pool_index];
    if (shader_program_pool_size == CC_MAX_SHADERS &&
        prg->opengl_program_id != 0) {
        glDeleteProgram(prg->opengl_program_id);
    }
    shader_program_pool_index = (shader_program_pool_index + 1) % CC_MAX_SHADERS;
    if (shader_program_pool_size < CC_MAX_SHADERS) { shader_program_pool_size++; }

    prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, "aVtxPos");
    prg->attrib_sizes[cnt] = 4;
    ++cnt;

    for (int t = 0; t < 2; t++) {
        if (ccf.used_textures[t]) {
            char name[16];
            sprintf(name, "aTexCoord%d", t);
            prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, name);
            prg->attrib_sizes[cnt] = 2;
            ++cnt;
        }
    }

    if (opt_fog) {
        prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, "aFog");
        prg->attrib_sizes[cnt] = 4;
        ++cnt;
    }

    if (opt_light_map) {
        prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, "aLightMap");
        prg->attrib_sizes[cnt] = 2;
        ++cnt;
    }

    for (int i = 0; i < ccf.num_inputs; i++) {
        char name[16];
        sprintf(name, "aInput%d", i + 1);
        prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, name);
        prg->attrib_sizes[cnt] = opt_alpha ? 4 : 3;
        ++cnt;
    }

    prg->hash = cc->hash;
    prg->opengl_program_id = shader_program;
    prg->num_inputs = ccf.num_inputs;
    prg->used_textures[0] = ccf.used_textures[0];
    prg->used_textures[1] = ccf.used_textures[1];
    prg->num_floats = num_floats;
    prg->num_attribs = cnt;

    glUseProgram(shader_program);
    for (int t = 0; t < 2; t++) {
        if (ccf.used_textures[t]) {
            char name[16];
            sprintf(name, "uTex%d", t);
            GLint sampler_location = glGetUniformLocation(shader_program, name);
            sprintf(name, "uTex%dSize", t);
            prg->uniform_locations[t * 2] = glGetUniformLocation(shader_program, name);
            sprintf(name, "uTex%dFilter", t);
            prg->uniform_locations[t * 2 + 1] = glGetUniformLocation(shader_program, name);
            glUniform1i(sampler_location, t);
        }
    }

    if ((opt_alpha && opt_dither) || ccf.do_noise) {
        prg->uniform_locations[4] = glGetUniformLocation(shader_program, "uFrameCount");
        prg->used_noise = true;
    } else {
        prg->used_noise = false;
    }

    if (opt_light_map) {
        prg->uniform_locations[5] = glGetUniformLocation(shader_program, "uLightmapColor");
        prg->used_lightmap = true;
    } else {
        prg->used_lightmap = false;
    }

    if (world_geometry) {
        prg->uniform_locations[6] = glGetUniformLocation(shader_program, "uShaderFlags");
        prg->uniform_locations[7] = glGetUniformLocation(shader_program, "uShaderFlagValues");
        prg->world_geometry = true;
    } else {
        prg->world_geometry = false;
    }

    prg->uniform_locations[8] = glGetUniformLocation(shader_program, "uFilter");

    shader_lookup_cache[
        (uint16_t)(prg->hash ^ (prg->hash >> 32)) &
            (SHADER_LOOKUP_CACHE_SIZE - 1)
    ] = prg;

    gfx_opengl_load_shader(prg);

    return prg;
}

static struct ShaderProgram *gfx_opengl_lookup_shader(struct ColorCombiner* cc) {
    const uint16_t cache_index =
        (uint16_t)(cc->hash ^ (cc->hash >> 32)) &
            (SHADER_LOOKUP_CACHE_SIZE - 1);
    struct ShaderProgram *cached = shader_lookup_cache[cache_index];
    if (cached != NULL && cached->hash == cc->hash &&
        cached->opengl_program_id != 0) {
        return cached;
    }
    for (size_t i = 0; i < shader_program_pool_size; i++) {
        if (shader_program_pool[i].hash == cc->hash) {
            shader_lookup_cache[cache_index] = &shader_program_pool[i];
            return &shader_program_pool[i];
        }
    }
    return NULL;
}

static void gfx_opengl_shader_get_info(struct ShaderProgram *prg, uint8_t *num_inputs, bool used_textures[2]) {
    *num_inputs = prg->num_inputs;
    used_textures[0] = prg->used_textures[0];
    used_textures[1] = prg->used_textures[1];
}

static GLuint gfx_opengl_new_texture(void) {
    if (num_textures >= tex_cache_size) {
        tex_cache_size += TEX_CACHE_STEP;
        tex_cache = realloc(tex_cache, sizeof(struct GLTexture) * tex_cache_size);
        if (!tex_cache) sys_fatal("out of memory allocating texture cache");
        // invalidate these because they might be pointing to garbage now
        opengl_tex[0] = NULL;
        opengl_tex[1] = NULL;
    }
    glGenTextures(1, &tex_cache[num_textures].gltex);
    return num_textures++;
}

static void gfx_opengl_select_texture(int tile, GLuint texture_id) {
    opengl_tex[tile] = tex_cache + texture_id;
    opengl_curtex = tile;
    glActiveTexture(GL_TEXTURE0 + tile);
    glBindTexture(GL_TEXTURE_2D, opengl_tex[tile]->gltex);
    gfx_opengl_set_texture_uniforms(opengl_prg, tile);
}

static void gfx_opengl_upload_texture(const uint8_t *rgba32_buf, int width, int height) {
#if defined(__ANDROID__)
    const uint64_t upload_start = gfx_opengl_monotonic_ns();
#endif
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba32_buf);
#if defined(__ANDROID__)
    sTextureUploadCount++;
    sTextureUploadBytes += (uint64_t)(uint32_t)width *
                           (uint64_t)(uint32_t)height * 4U;
    sTextureUploadNs += gfx_opengl_monotonic_ns() - upload_start;
#endif
    opengl_tex[opengl_curtex]->size[0] = width;
    opengl_tex[opengl_curtex]->size[1] = height;
}

static uint32_t gfx_cm_to_opengl(uint32_t val) {
    if (val & G_TX_CLAMP) {
        return GL_CLAMP_TO_EDGE;
    }
    return (val & G_TX_MIRROR) ? GL_MIRRORED_REPEAT : GL_REPEAT;
}

static void gfx_opengl_set_sampler_parameters(int tile, bool linear_filter, uint32_t cms, uint32_t cmt) {
    const bool samplerLinear = linear_filter && configFiltering == 1;
    const GLenum filter = samplerLinear ? GL_LINEAR : GL_NEAREST;
    glActiveTexture(GL_TEXTURE0 + tile);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gfx_cm_to_opengl(cms));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gfx_cm_to_opengl(cmt));
    opengl_curtex = tile;
    if (opengl_tex[tile]) {
        opengl_tex[tile]->filter = linear_filter;
        opengl_tex[tile]->sampler_initialized = true;
        opengl_tex[tile]->sampler_linear = samplerLinear;
        gfx_opengl_set_texture_uniforms(opengl_prg, tile);
    }
}

static void gfx_opengl_set_depth_test(bool depth_test) {
    if (depth_test) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
}

static void gfx_opengl_set_depth_mask(bool z_upd) {
    glDepthMask(z_upd ? GL_TRUE : GL_FALSE);
}

static void gfx_opengl_set_zmode_decal(bool zmode_decal) {
    if (zmode_decal) {
        glPolygonOffset(-2, -2);
        glEnable(GL_POLYGON_OFFSET_FILL);
    } else {
        glPolygonOffset(0, 0);
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
}

static void gfx_opengl_set_viewport(int x, int y, int width, int height) {
#ifdef __ANDROID__
    // Some title/menu display lists begin with an empty N64 viewport and rely
    // on the desktop window's existing full viewport until the scene viewport
    // arrives. Applying that empty viewport to an OpenXR framebuffer makes
    // every subsequent 3D draw rasterize zero pixels. The eye target was
    // already set to its full dimensions by the Quest host, so preserve it.
    if (width <= 0 || height <= 0) {
        return;
    }
#endif
    glViewport(x, y, width, height);
#ifdef __ANDROID__
    static bool logged = false;
    if (!logged) {
        const GLenum error = glGetError();
        __android_log_print(error == GL_NO_ERROR ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR,
            "SM64CoopDXVR", "GLES viewport %d,%d %dx%d -> 0x%04x.",
            x, y, width, height, error);
        logged = true;
    }
#endif
}

static void gfx_opengl_set_scissor(int x, int y, int width, int height) {
    glScissor(x, y, width, height);
#ifdef __ANDROID__
    static bool logged = false;
    if (!logged) {
        const GLenum error = glGetError();
        __android_log_print(error == GL_NO_ERROR ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR,
            "SM64CoopDXVR", "GLES scissor %d,%d %dx%d -> 0x%04x.",
            x, y, width, height, error);
        logged = true;
    }
#endif
}

static void gfx_opengl_set_use_alpha(bool use_alpha) {
    if (use_alpha) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
    }
}

static void gfx_opengl_draw_triangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) {
    //printf("flushing %d tris\n", buf_vbo_num_tris);
#ifdef __ANDROID__
    static bool logged_world = false;
    static bool logged_overlay = false;
    bool *logged = opengl_prg != NULL && opengl_prg->world_geometry
        ? &logged_world : &logged_overlay;
    if (!*logged && opengl_prg != NULL && opengl_prg->num_floats >= 4) {
        const size_t vertices = buf_vbo_num_tris * 3;
        size_t clip_visible = 0;
        float min_x = 1e30f, max_x = -1e30f;
        float min_y = 1e30f, max_y = -1e30f;
        float min_z = 1e30f, max_z = -1e30f;
        for (size_t vertex = 0; vertex < vertices; ++vertex) {
            const float *p = buf_vbo + vertex * opengl_prg->num_floats;
            const float w = p[3];
            if (w != 0.0f) {
                const float x = p[0] / w;
                const float y = p[1] / w;
                const float z = p[2] / w;
                if (x < min_x) min_x = x; if (x > max_x) max_x = x;
                if (y < min_y) min_y = y; if (y > max_y) max_y = y;
                if (z < min_z) min_z = z; if (z > max_z) max_z = z;
                if (x >= -1.0f && x <= 1.0f && y >= -1.0f && y <= 1.0f &&
                    z >= -1.0f && z <= 1.0f) ++clip_visible;
            }
        }
        __android_log_print(ANDROID_LOG_INFO, "SM64CoopDXVR",
            "%s draw reached GLES: %zu tris, %zu/%zu vertices in clip; NDC x %.2f..%.2f y %.2f..%.2f z %.2f..%.2f.",
            opengl_prg->world_geometry ? "World" : "Overlay",
            buf_vbo_num_tris, clip_visible, vertices,
            min_x, max_x, min_y, max_y, min_z, max_z);
        *logged = true;
    }
#endif
#ifdef __ANDROID__
    static bool completed_draw_diagnostics = false;
    if (!completed_draw_diagnostics) {
        const GLenum pending_error = glGetError();
        if (pending_error != GL_NO_ERROR) {
            __android_log_print(ANDROID_LOG_ERROR, "SM64CoopDXVR",
                                "GLES error before vertex upload: 0x%04x.", pending_error);
        }
    }
#endif
#ifdef __ANDROID__
    sDrawCallCount++;
    sTriangleCount += buf_vbo_num_tris;
    sVertexUploadBytes += sizeof(float) * buf_vbo_len;
#endif
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * buf_vbo_len,
                 buf_vbo, GL_STREAM_DRAW);
#ifdef __ANDROID__
    if (!completed_draw_diagnostics) {
        const GLenum upload_error = glGetError();
        if (upload_error != GL_NO_ERROR) {
            __android_log_print(ANDROID_LOG_ERROR, "SM64CoopDXVR",
                                "GLES vertex upload error: 0x%04x (%zu floats).",
                                upload_error, buf_vbo_len);
        }
    }
#endif
    glDrawArrays(GL_TRIANGLES, 0, 3 * buf_vbo_num_tris);
#ifdef __ANDROID__
    if (!completed_draw_diagnostics) {
        const GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            __android_log_print(ANDROID_LOG_ERROR, "SM64CoopDXVR",
                                "First GLES draw error: 0x%04x.", error);
        }
        completed_draw_diagnostics = true;
    }
#endif
}

static inline bool gl_version_is_supported(int major, int minor, bool is_es) {
    if (is_es) {
        return major >= 2;
    }
    return (major > 2) || (major == 2 && minor >= 1);
}

static inline bool gl_get_version(int *major, int *minor, bool *is_es) {
    const char *vstr = (const char *)glGetString(GL_VERSION);
    if (!vstr || !vstr[0]) return false;

    if (!strncmp(vstr, "OpenGL ES ", 10)) {
        vstr += 10;
        *is_es = true;
    } else if (!strncmp(vstr, "OpenGL ES-CM ", 13)) {
        vstr += 13;
        *is_es = true;
    }

    return (sscanf(vstr, "%d.%d", major, minor) == 2);
}

static void gfx_opengl_init(void) {
#if FOR_WINDOWS || defined(OSX_BUILD)
    GLenum err;
    if ((err = glewInit()) != GLEW_OK)
        sys_fatal("could not init GLEW:\n%s", glewGetErrorString(err));
#endif

    // Reserve the address-cache capacity up front. Growing this array during
    // gameplay invalidates the active texture pointers and can create a
    // visible one-frame allocator hitch when a mod introduces texture 513,
    // 1025, and so on.
    tex_cache_size = MAX_CACHED_TEXTURES;
    tex_cache = calloc(tex_cache_size, sizeof(struct GLTexture));
    if (!tex_cache) sys_fatal("out of memory allocating texture cache");

    // check GL version
    int vmajor = 0;
    int vminor = 0;
    bool is_es = false;
    if (!gl_get_version(&vmajor, &vminor, &is_es) || !gl_version_is_supported(vmajor, vminor, is_es)) {
        sys_fatal("OpenGL 2.1+ is required.\nReported version: %s%d.%d", is_es ? "ES" : "", vmajor, vminor);
    }

    glGenBuffers(1, &opengl_vbo);

    glBindBuffer(GL_ARRAY_BUFFER, opengl_vbo);

    if (vmajor >= 3 && !is_es) {
        glGenVertexArrays(1, &opengl_vao);
        glBindVertexArray(opengl_vao);
    }

    glDepthFunc(GL_LEQUAL);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

bool gfx_opengl_check_compatibility(void) {
    // check GL version
    int vmajor = 0;
    int vminor = 0;
    bool is_es = false;
    if (!gl_get_version(&vmajor, &vminor, &is_es)) {
        return false;
    }
    return gl_version_is_supported(vmajor, vminor, is_es);
}

static void gfx_opengl_on_resize(void) {
}

static void gfx_opengl_start_frame(void) {
    frame_count++;

    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE); // Must be set to clear Z-buffer
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
}

static void gfx_opengl_end_frame(void) {
}

static void gfx_opengl_finish_render(void) {
}

static const char* gfx_opengl_get_name(void) {
    return "OpenGL";
}

static void gfx_opengl_shutdown(void) {
    for (uint16_t i = 0; i < shader_program_pool_size; i++) {
        if (shader_program_pool[i].opengl_program_id != 0) {
            glDeleteProgram(shader_program_pool[i].opengl_program_id);
        }
    }
    if (num_textures > 0 && tex_cache != NULL) {
        for (int i = 0; i < num_textures; i++) {
            glDeleteTextures(1, &tex_cache[i].gltex);
        }
    }
    if (opengl_vbo != 0) {
        glDeleteBuffers(1, &opengl_vbo);
    }
    if (opengl_vao != 0) {
        glDeleteVertexArrays(1, &opengl_vao);
    }
    free(tex_cache);
    tex_cache = NULL;
    tex_cache_size = 0;
    num_textures = 0;
    shader_program_pool_size = 0;
    shader_program_pool_index = 0;
    for (size_t i = 0; i < SHADER_LOOKUP_CACHE_SIZE; i++) {
        shader_lookup_cache[i] = NULL;
    }
    opengl_prg = NULL;
}

struct GfxRenderingAPI gfx_opengl_api = {
    gfx_opengl_z_is_from_0_to_1,
    gfx_opengl_unload_shader,
    gfx_opengl_load_shader,
    gfx_opengl_create_and_load_new_shader,
    gfx_opengl_lookup_shader,
    gfx_opengl_shader_get_info,
    gfx_opengl_new_texture,
    gfx_opengl_select_texture,
    gfx_opengl_upload_texture,
    gfx_opengl_set_sampler_parameters,
    gfx_opengl_set_depth_test,
    gfx_opengl_set_depth_mask,
    gfx_opengl_set_zmode_decal,
    gfx_opengl_set_viewport,
    gfx_opengl_set_scissor,
    gfx_opengl_set_use_alpha,
    gfx_opengl_draw_triangles,
    gfx_opengl_init,
    gfx_opengl_on_resize,
    gfx_opengl_start_frame,
    gfx_opengl_end_frame,
    gfx_opengl_finish_render,
    gfx_opengl_get_name,
    gfx_opengl_shutdown
};
