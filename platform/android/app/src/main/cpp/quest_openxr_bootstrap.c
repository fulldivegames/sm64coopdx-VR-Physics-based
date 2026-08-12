#include <android/log.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "quest_rom.h"
#include "quest_game_runtime.h"
#include "quest_openxr_input.h"

extern void quest_vr_bridge_update_views(
    const float positions[2][3], const float rotations[2][4],
    const float fovs[2][4], const uint32_t widths[2],
    const uint32_t heights[2]);

#define LOG_TAG "SM64CoopDXVR"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define QUEST_VIEW_COUNT 2U
#define QUEST_DEFAULT_RENDER_SCALE_PERCENT 80U

typedef struct QuestSwapchain {
    XrSwapchain handle;
    int32_t width;
    int32_t height;
    uint32_t image_count;
    XrSwapchainImageOpenGLESKHR *images;
    uint64_t verified_framebuffer_images;
} QuestSwapchain;

typedef struct QuestApp {
    struct android_app *android_app;

    EGLDisplay egl_display;
    EGLConfig egl_config;
    EGLContext egl_context;
    EGLSurface egl_surface;

    XrInstance instance;
    XrSystemId system_id;
    XrSession session;
    XrSpace local_space;
    XrSessionState session_state;
    XrEnvironmentBlendMode blend_mode;
    bool session_running;
    bool exit_requested;
    bool activity_resumed;
    bool window_ready;
    QuestRomResult rom;
    GLuint mario_program;
    GLuint mario_texture;
    GLint mario_eye_offset;

    XrViewConfigurationView view_configs[QUEST_VIEW_COUNT];
    XrView views[QUEST_VIEW_COUNT];
    QuestSwapchain swapchains[QUEST_VIEW_COUNT];
    unsigned int active_render_scale;
    unsigned int pending_render_scale;
    unsigned int render_scale_stable_frames;
    bool foveation_supported;
    bool performance_settings_supported;
    XrFoveationProfileFB foveation_profile;
    PFN_xrCreateFoveationProfileFB create_foveation_profile;
    PFN_xrDestroyFoveationProfileFB destroy_foveation_profile;
    PFN_xrUpdateSwapchainFB update_swapchain;
    PFN_xrPerfSettingsSetPerformanceLevelEXT set_performance_level;
    GLuint framebuffer;
    GLuint depth_buffer;
    int32_t depth_width;
    int32_t depth_height;
} QuestApp;

static bool openxr_extension_available(const char *name) {
    uint32_t count = 0;
    if (XR_FAILED(xrEnumerateInstanceExtensionProperties(
            NULL, 0, &count, NULL)) || count == 0) return false;
    XrExtensionProperties *properties = calloc(count, sizeof(*properties));
    if (properties == NULL) return false;
    for (uint32_t i = 0; i < count; ++i) {
        properties[i].type = XR_TYPE_EXTENSION_PROPERTIES;
    }
    bool found = false;
    if (XR_SUCCEEDED(xrEnumerateInstanceExtensionProperties(
            NULL, count, &count, properties))) {
        for (uint32_t i = 0; i < count; ++i) {
            if (strcmp(properties[i].extensionName, name) == 0) {
                found = true;
                break;
            }
        }
    }
    free(properties);
    return found;
}

static QuestApp *sActiveQuestApp;

void quest_android_request_exit(void) {
    if (sActiveQuestApp == NULL) return;
    sActiveQuestApp->exit_requested = true;
    ANativeActivity_finish(sActiveQuestApp->android_app->activity);
}

static GLuint compile_shader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
        char message[512] = {0};
        glGetShaderInfoLog(shader, sizeof(message), NULL, message);
        LOGE("Mario preview shader failed: %s", message);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool create_mario_preview(QuestApp *app) {
    if (app->rom.status != QUEST_ROM_VALID) return true;
    unsigned char pixels[64U * 32U * 4U];
    if (!quest_rom_load_rgba16_texture(&app->rom,
            0x002a65b0U, 22255U, 0x00001018U, 64U, 32U, pixels)) {
        LOGE("Could not extract the Mario portrait from the ROM.");
        return false;
    }
    const char *vertex_source =
        "#version 300 es\n"
        "uniform float eyeOffset; out vec2 uv;\n"
        "void main(){\n"
        "const vec2 p[6]=vec2[6](vec2(-.56,-.28),vec2(.56,-.28),"
        "vec2(-.56,.28),vec2(-.56,.28),vec2(.56,-.28),vec2(.56,.28));\n"
        "const vec2 t[6]=vec2[6](vec2(0.,1.),vec2(1.,1.),vec2(0.,0.),"
        "vec2(0.,0.),vec2(1.,1.),vec2(1.,0.));\n"
        "uv=t[gl_VertexID]; gl_Position=vec4(p[gl_VertexID]+vec2(eyeOffset,0.),0.,1.);}\n";
    const char *fragment_source =
        "#version 300 es\nprecision mediump float; in vec2 uv;"
        "uniform sampler2D portrait; out vec4 color;"
        "void main(){color=texture(portrait,uv);}\n";
    GLuint vertex = compile_shader(GL_VERTEX_SHADER, vertex_source);
    GLuint fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    if (vertex == 0 || fragment == 0) {
        if (vertex != 0) glDeleteShader(vertex);
        if (fragment != 0) glDeleteShader(fragment);
        return false;
    }
    app->mario_program = glCreateProgram();
    glAttachShader(app->mario_program, vertex);
    glAttachShader(app->mario_program, fragment);
    glLinkProgram(app->mario_program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked = GL_FALSE;
    glGetProgramiv(app->mario_program, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
        LOGE("Could not link the Mario preview shader.");
        return false;
    }
    app->mario_eye_offset = glGetUniformLocation(app->mario_program, "eyeOffset");
    glUseProgram(app->mario_program);
    glUniform1i(glGetUniformLocation(app->mario_program, "portrait"), 0);
    glGenTextures(1, &app->mario_texture);
    glBindTexture(GL_TEXTURE_2D, app->mario_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 32, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    LOGI("Mario portrait extracted from the ROM and uploaded to OpenGL ES.");
    return true;
}

static void handle_app_command(struct android_app *android_app, int32_t command) {
    QuestApp *app = (QuestApp *)android_app->userData;
    if (app == NULL) {
        return;
    }

    switch (command) {
        case APP_CMD_RESUME:
            app->activity_resumed = true;
            LOGI("Android activity resumed.");
            break;
        case APP_CMD_PAUSE:
            quest_game_flush_persistent_state();
            app->activity_resumed = false;
            LOGI("Android activity paused.");
            break;
        case APP_CMD_STOP:
            quest_game_flush_persistent_state();
            app->activity_resumed = false;
            app->exit_requested = true;
            LOGI("Android activity stopped; terminating standalone session.");
            break;
        case APP_CMD_INIT_WINDOW:
            app->window_ready = android_app->window != NULL;
            LOGI("Android native window is ready.");
            break;
        case APP_CMD_TERM_WINDOW:
            app->window_ready = false;
            LOGI("Android native window was released.");
            break;
        default:
            break;
    }
}

static bool wait_for_android_ready(QuestApp *app) {
    LOGI("Waiting for the Android activity and native window.");
    while (!app->android_app->destroyRequested && !app->exit_requested
           && !(app->activity_resumed && app->window_ready)) {
        int events = 0;
        struct android_poll_source *source = NULL;
        const int result =
            ALooper_pollOnce(-1, NULL, &events, (void **)&source);
        if (result >= 0 && source != NULL) {
            source->process(app->android_app, source);
        }
    }
    return !app->android_app->destroyRequested && !app->exit_requested;
}

static bool xr_check(const char *operation, XrResult result) {
    if (XR_SUCCEEDED(result)) {
        return true;
    }

    LOGE("%s failed with OpenXR result %d", operation, (int)result);
    return false;
}

static bool egl_check(const char *operation, EGLBoolean result) {
    if (result == EGL_TRUE) {
        return true;
    }

    LOGE("%s failed with EGL error 0x%04x", operation, eglGetError());
    return false;
}

static bool create_egl_context(QuestApp *app) {
    app->egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (app->egl_display == EGL_NO_DISPLAY) {
        LOGE("eglGetDisplay failed with EGL error 0x%04x", eglGetError());
        return false;
    }

    EGLint major = 0;
    EGLint minor = 0;
    if (!egl_check("eglInitialize", eglInitialize(app->egl_display, &major, &minor))) {
        return false;
    }

    const EGLint config_attributes[] = {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_STENCIL_SIZE, 0,
        EGL_SAMPLES, 0,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_NONE,
    };

    EGLint config_count = 0;
    if (!egl_check(
            "eglChooseConfig",
            eglChooseConfig(
                app->egl_display, config_attributes, &app->egl_config, 1, &config_count))
        || config_count < 1) {
        LOGE("No compatible OpenGL ES 3 EGL configuration was found.");
        return false;
    }

    if (!egl_check("eglBindAPI", eglBindAPI(EGL_OPENGL_ES_API))) {
        return false;
    }

    const EGLint context_attributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE,
    };
    app->egl_context = eglCreateContext(
        app->egl_display, app->egl_config, EGL_NO_CONTEXT, context_attributes);
    if (app->egl_context == EGL_NO_CONTEXT) {
        LOGE("eglCreateContext failed with EGL error 0x%04x", eglGetError());
        return false;
    }

    const EGLint surface_attributes[] = {
        EGL_WIDTH, 16,
        EGL_HEIGHT, 16,
        EGL_NONE,
    };
    app->egl_surface =
        eglCreatePbufferSurface(app->egl_display, app->egl_config, surface_attributes);
    if (app->egl_surface == EGL_NO_SURFACE) {
        LOGE("eglCreatePbufferSurface failed with EGL error 0x%04x", eglGetError());
        return false;
    }

    if (!egl_check(
            "eglMakeCurrent",
            eglMakeCurrent(
                app->egl_display,
                app->egl_surface,
                app->egl_surface,
                app->egl_context))) {
        return false;
    }

    LOGI("EGL %d.%d OpenGL ES context created.", major, minor);
    return true;
}

static bool create_openxr_instance(QuestApp *app) {
    PFN_xrInitializeLoaderKHR initialize_loader = NULL;
    XrResult result = xrGetInstanceProcAddr(
        XR_NULL_HANDLE,
        "xrInitializeLoaderKHR",
        (PFN_xrVoidFunction *)&initialize_loader);
    if (!xr_check("xrGetInstanceProcAddr(xrInitializeLoaderKHR)", result)
        || initialize_loader == NULL) {
        return false;
    }

    XrLoaderInitInfoAndroidKHR loader_info = {
        .type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR,
        .next = NULL,
        .applicationVM = app->android_app->activity->vm,
        .applicationContext = app->android_app->activity->clazz,
    };
    if (!xr_check(
            "xrInitializeLoaderKHR",
            initialize_loader((const XrLoaderInitInfoBaseHeaderKHR *)&loader_info))) {
        return false;
    }

    const char *extensions[6] = {
        XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
        XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
    };
    uint32_t extension_count = 2;
    app->performance_settings_supported =
        openxr_extension_available(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME);
    if (app->performance_settings_supported) {
        extensions[extension_count++] =
            XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME;
    }
    app->foveation_supported =
        openxr_extension_available(XR_FB_FOVEATION_EXTENSION_NAME)
        && openxr_extension_available(
            XR_FB_FOVEATION_CONFIGURATION_EXTENSION_NAME)
        && openxr_extension_available(
            XR_FB_SWAPCHAIN_UPDATE_STATE_EXTENSION_NAME);
    if (app->foveation_supported) {
        extensions[extension_count++] = XR_FB_FOVEATION_EXTENSION_NAME;
        extensions[extension_count++] =
            XR_FB_FOVEATION_CONFIGURATION_EXTENSION_NAME;
        extensions[extension_count++] =
            XR_FB_SWAPCHAIN_UPDATE_STATE_EXTENSION_NAME;
    }

    XrInstanceCreateInfoAndroidKHR android_info = {
        .type = XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR,
        .next = NULL,
        .applicationVM = app->android_app->activity->vm,
        .applicationActivity = app->android_app->activity->clazz,
    };
    XrInstanceCreateInfo create_info = {
        .type = XR_TYPE_INSTANCE_CREATE_INFO,
        .next = &android_info,
        .applicationInfo = {
            .applicationName = "SM64 Co-Op DX VR",
            .applicationVersion = 1,
            .engineName = "SM64 Co-Op DX",
            .engineVersion = 1,
            .apiVersion = XR_CURRENT_API_VERSION,
        },
        .enabledExtensionCount = extension_count,
        .enabledExtensionNames = extensions,
    };

    if (!xr_check("xrCreateInstance", xrCreateInstance(&create_info, &app->instance))) {
        return false;
    }

    XrSystemGetInfo system_info = {
        .type = XR_TYPE_SYSTEM_GET_INFO,
        .formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY,
    };
    if (!xr_check(
            "xrGetSystem", xrGetSystem(app->instance, &system_info, &app->system_id))) {
        return false;
    }

    LOGI("Quest OpenXR runtime detected; system id=%llu.",
         (unsigned long long)app->system_id);
    return true;
}

static bool select_blend_mode(QuestApp *app) {
    uint32_t count = 0;
    if (!xr_check(
            "xrEnumerateEnvironmentBlendModes(count)",
            xrEnumerateEnvironmentBlendModes(
                app->instance,
                app->system_id,
                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                0,
                &count,
                NULL))
        || count == 0) {
        return false;
    }

    XrEnvironmentBlendMode *modes =
        (XrEnvironmentBlendMode *)calloc(count, sizeof(*modes));
    if (modes == NULL) {
        LOGE("Could not allocate environment blend mode list.");
        return false;
    }

    const bool enumerated = xr_check(
        "xrEnumerateEnvironmentBlendModes",
        xrEnumerateEnvironmentBlendModes(
            app->instance,
            app->system_id,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
            count,
            &count,
            modes));
    if (!enumerated) {
        free(modes);
        return false;
    }

    app->blend_mode = modes[0];
    for (uint32_t index = 0; index < count; ++index) {
        if (modes[index] == XR_ENVIRONMENT_BLEND_MODE_OPAQUE) {
            app->blend_mode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
            break;
        }
    }
    free(modes);
    return true;
}

static bool create_openxr_session(QuestApp *app) {
    PFN_xrGetOpenGLESGraphicsRequirementsKHR get_requirements = NULL;
    if (!xr_check(
            "xrGetInstanceProcAddr(xrGetOpenGLESGraphicsRequirementsKHR)",
            xrGetInstanceProcAddr(
                app->instance,
                "xrGetOpenGLESGraphicsRequirementsKHR",
                (PFN_xrVoidFunction *)&get_requirements))
        || get_requirements == NULL) {
        return false;
    }

    XrGraphicsRequirementsOpenGLESKHR requirements = {
        .type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR,
    };
    if (!xr_check(
            "xrGetOpenGLESGraphicsRequirementsKHR",
            get_requirements(app->instance, app->system_id, &requirements))) {
        return false;
    }

    XrGraphicsBindingOpenGLESAndroidKHR graphics_binding = {
        .type = XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR,
        .display = app->egl_display,
        .config = app->egl_config,
        .context = app->egl_context,
    };
    XrSessionCreateInfo session_info = {
        .type = XR_TYPE_SESSION_CREATE_INFO,
        .next = &graphics_binding,
        .systemId = app->system_id,
    };
    if (!xr_check("xrCreateSession", xrCreateSession(
            app->instance, &session_info, &app->session))) {
        return false;
    }

    if (app->performance_settings_supported) {
        xrGetInstanceProcAddr(
            app->instance,
            "xrPerfSettingsSetPerformanceLevelEXT",
            (PFN_xrVoidFunction *)&app->set_performance_level
        );
        app->performance_settings_supported =
            app->set_performance_level != NULL;
    }
    if (app->performance_settings_supported) {
        const XrResult cpu_result = app->set_performance_level(
            app->session,
            XR_PERF_SETTINGS_DOMAIN_CPU_EXT,
            XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT
        );
        const XrResult gpu_result = app->set_performance_level(
            app->session,
            XR_PERF_SETTINGS_DOMAIN_GPU_EXT,
            XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT
        );
        if (XR_SUCCEEDED(cpu_result) && XR_SUCCEEDED(gpu_result)) {
            LOGI("Quest sustained-high CPU/GPU performance levels enabled.");
        } else {
            LOGI("Quest runtime declined sustained performance levels (CPU %d, GPU %d).",
                 (int)cpu_result, (int)gpu_result);
        }
    } else {
        LOGI("OpenXR performance-level control is unavailable; using runtime defaults.");
    }

    if (app->foveation_supported) {
        xrGetInstanceProcAddr(app->instance, "xrCreateFoveationProfileFB",
            (PFN_xrVoidFunction *)&app->create_foveation_profile);
        xrGetInstanceProcAddr(app->instance, "xrDestroyFoveationProfileFB",
            (PFN_xrVoidFunction *)&app->destroy_foveation_profile);
        xrGetInstanceProcAddr(app->instance, "xrUpdateSwapchainFB",
            (PFN_xrVoidFunction *)&app->update_swapchain);
        app->foveation_supported = app->create_foveation_profile != NULL
            && app->destroy_foveation_profile != NULL
            && app->update_swapchain != NULL;
    }
    if (app->foveation_supported) {
        XrFoveationLevelProfileCreateInfoFB level = {
            .type = XR_TYPE_FOVEATION_LEVEL_PROFILE_CREATE_INFO_FB,
            .level = XR_FOVEATION_LEVEL_MEDIUM_FB,
            .verticalOffset = 0.0f,
            .dynamic = XR_FOVEATION_DYNAMIC_LEVEL_ENABLED_FB,
        };
        XrFoveationProfileCreateInfoFB profile_info = {
            .type = XR_TYPE_FOVEATION_PROFILE_CREATE_INFO_FB,
            .next = &level,
        };
        if (XR_FAILED(app->create_foveation_profile(
                app->session, &profile_info, &app->foveation_profile))) {
            app->foveation_supported = false;
            app->foveation_profile = XR_NULL_HANDLE;
            LOGI("Quest fixed foveated rendering is unavailable.");
        } else {
            LOGI("Quest medium dynamic fixed foveated rendering enabled.");
        }
    }

    XrReferenceSpaceCreateInfo space_info = {
        .type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
        .referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL,
        .poseInReferenceSpace = {
            .orientation = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f},
            .position = {.x = 0.0f, .y = 0.0f, .z = 0.0f},
        },
    };
    if (!xr_check(
            "xrCreateReferenceSpace",
            xrCreateReferenceSpace(app->session, &space_info, &app->local_space))) {
        return false;
    }

    if (!select_blend_mode(app)) {
        return false;
    }

    LOGI("OpenXR OpenGL ES session created.");
    return true;
}

static int64_t select_swapchain_format(QuestApp *app) {
    uint32_t format_count = 0;
    if (!xr_check(
            "xrEnumerateSwapchainFormats(count)",
            xrEnumerateSwapchainFormats(
                app->session, 0, &format_count, NULL))
        || format_count == 0) {
        return 0;
    }

    int64_t *formats = (int64_t *)calloc(format_count, sizeof(*formats));
    if (formats == NULL) {
        LOGE("Could not allocate swapchain format list.");
        return 0;
    }

    if (!xr_check(
            "xrEnumerateSwapchainFormats",
            xrEnumerateSwapchainFormats(
                app->session, format_count, &format_count, formats))) {
        free(formats);
        return 0;
    }

    int64_t selected = formats[0];
    for (uint32_t index = 0; index < format_count; ++index) {
        if (formats[index] == GL_SRGB8_ALPHA8) {
            selected = GL_SRGB8_ALPHA8;
            break;
        }
        if (formats[index] == GL_RGBA8) {
            selected = GL_RGBA8;
        }
    }
    free(formats);
    return selected;
}

static void destroy_swapchain_render_targets(QuestApp *app) {
    // This is only called between submitted frames, when no image is acquired.
    glFinish();
    if (app->framebuffer != 0) {
        glDeleteFramebuffers(1, &app->framebuffer);
        app->framebuffer = 0;
    }
    if (app->depth_buffer != 0) {
        glDeleteRenderbuffers(1, &app->depth_buffer);
        app->depth_buffer = 0;
    }
    for (uint32_t eye = 0; eye < QUEST_VIEW_COUNT; ++eye) {
        QuestSwapchain *swapchain = &app->swapchains[eye];
        free(swapchain->images);
        swapchain->images = NULL;
        swapchain->image_count = 0;
        swapchain->verified_framebuffer_images = 0;
        if (swapchain->handle != XR_NULL_HANDLE) {
            xrDestroySwapchain(swapchain->handle);
            swapchain->handle = XR_NULL_HANDLE;
        }
    }
}

static bool create_swapchains(QuestApp *app) {
    uint32_t view_count = 0;
    if (!xr_check(
            "xrEnumerateViewConfigurationViews(count)",
            xrEnumerateViewConfigurationViews(
                app->instance,
                app->system_id,
                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                0,
                &view_count,
                NULL))
        || view_count != QUEST_VIEW_COUNT) {
        LOGE("Primary stereo reported %u views; expected %u.",
             view_count,
             QUEST_VIEW_COUNT);
        return false;
    }

    unsigned int render_scale = quest_game_render_scale_percent();
    LOGI("Quest eye render scale: %u%% (applies after app restart).", render_scale);

    for (uint32_t eye = 0; eye < QUEST_VIEW_COUNT; ++eye) {
        app->view_configs[eye].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
        app->views[eye].type = XR_TYPE_VIEW;
    }
    if (!xr_check(
            "xrEnumerateViewConfigurationViews",
            xrEnumerateViewConfigurationViews(
                app->instance,
                app->system_id,
                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                QUEST_VIEW_COUNT,
                &view_count,
                app->view_configs))) {
        return false;
    }

    const int64_t format = select_swapchain_format(app);
    if (format == 0) {
        return false;
    }

    for (uint32_t eye = 0; eye < QUEST_VIEW_COUNT; ++eye) {
        QuestSwapchain *swapchain = &app->swapchains[eye];
        swapchain->width = (int32_t)(
            (app->view_configs[eye].recommendedImageRectWidth *
             render_scale + 50U) / 100U);
        swapchain->height = (int32_t)(
            (app->view_configs[eye].recommendedImageRectHeight *
             render_scale + 50U) / 100U);

        XrSwapchainCreateInfoFoveationFB foveation_create = {
            .type = XR_TYPE_SWAPCHAIN_CREATE_INFO_FOVEATION_FB,
            .flags = 0,
        };
        XrSwapchainCreateInfo create_info = {
            .type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
            .next = app->foveation_supported ? &foveation_create : NULL,
            .usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
            .format = format,
            .sampleCount = 1,
            .width = (uint32_t)swapchain->width,
            .height = (uint32_t)swapchain->height,
            .faceCount = 1,
            .arraySize = 1,
            .mipCount = 1,
        };
        if (!xr_check(
                "xrCreateSwapchain",
                xrCreateSwapchain(
                    app->session, &create_info, &swapchain->handle))) {
            return false;
        }
        if (app->foveation_supported) {
            XrSwapchainStateFoveationFB state = {
                .type = XR_TYPE_SWAPCHAIN_STATE_FOVEATION_FB,
                .profile = app->foveation_profile,
            };
            if (XR_FAILED(app->update_swapchain(
                    swapchain->handle,
                    (const XrSwapchainStateBaseHeaderFB *)&state))) {
                LOGI("Could not apply foveation to the %s eye swapchain.",
                     eye == 0 ? "left" : "right");
            }
        }

        if (!xr_check(
                "xrEnumerateSwapchainImages(count)",
                xrEnumerateSwapchainImages(
                    swapchain->handle, 0, &swapchain->image_count, NULL))
            || swapchain->image_count == 0) {
            return false;
        }

        swapchain->images = (XrSwapchainImageOpenGLESKHR *)calloc(
            swapchain->image_count, sizeof(*swapchain->images));
        swapchain->verified_framebuffer_images = 0;
        if (swapchain->images == NULL) {
            LOGE("Could not allocate swapchain image list.");
            return false;
        }
        for (uint32_t image = 0; image < swapchain->image_count; ++image) {
            swapchain->images[image].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
        }
        if (!xr_check(
                "xrEnumerateSwapchainImages",
                xrEnumerateSwapchainImages(
                    swapchain->handle,
                    swapchain->image_count,
                    &swapchain->image_count,
                    (XrSwapchainImageBaseHeader *)swapchain->images))) {
            return false;
        }

        LOGI("%s eye swapchain: %dx%d, %u images.",
             eye == 0 ? "Left" : "Right",
             swapchain->width,
             swapchain->height,
             swapchain->image_count);
    }

    glGenFramebuffers(1, &app->framebuffer);
    glGenRenderbuffers(1, &app->depth_buffer);
    app->depth_width = app->swapchains[0].width;
    app->depth_height = app->swapchains[0].height;
    for (uint32_t eye = 1; eye < QUEST_VIEW_COUNT; ++eye) {
        if (app->swapchains[eye].width > app->depth_width)
            app->depth_width = app->swapchains[eye].width;
        if (app->swapchains[eye].height > app->depth_height)
            app->depth_height = app->swapchains[eye].height;
    }
    glBindRenderbuffer(GL_RENDERBUFFER, app->depth_buffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                          app->depth_width, app->depth_height);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    LOGI("OpenGL ES depth buffer: %dx%d, 24-bit.",
         app->depth_width, app->depth_height);
    app->active_render_scale = render_scale;
    app->pending_render_scale = render_scale;
    app->render_scale_stable_frames = 0;
    return true;
}

static bool update_render_scale_if_needed(QuestApp *app) {
    const unsigned int requested = quest_game_render_scale_percent();
    if (requested == app->active_render_scale) {
        app->pending_render_scale = requested;
        app->render_scale_stable_frames = 0;
        return true;
    }
    if (requested != app->pending_render_scale) {
        app->pending_render_scale = requested;
        app->render_scale_stable_frames = 0;
        return true;
    }

    // Wait until the slider has stopped moving so holding the stick does not
    // repeatedly allocate and destroy large OpenXR textures.
    if (++app->render_scale_stable_frames < 30U) return true;

    LOGI("Applying live Quest render scale change: %u%% -> %u%%.",
         app->active_render_scale, requested);
    destroy_swapchain_render_targets(app);
    return create_swapchains(app);
}

static bool render_eye(QuestApp *app, uint32_t eye) {
    QuestSwapchain *swapchain = &app->swapchains[eye];
    uint32_t image_index = 0;
    XrSwapchainImageAcquireInfo acquire_info = {
        .type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO,
    };
    if (!xr_check(
            "xrAcquireSwapchainImage",
            xrAcquireSwapchainImage(
                swapchain->handle, &acquire_info, &image_index))) {
        return false;
    }

    XrSwapchainImageWaitInfo wait_info = {
        .type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO,
        .timeout = XR_INFINITE_DURATION,
    };
    if (!xr_check(
            "xrWaitSwapchainImage",
            xrWaitSwapchainImage(swapchain->handle, &wait_info))) {
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, app->framebuffer);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        swapchain->images[image_index].image,
        0);
    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER,
        GL_DEPTH_ATTACHMENT,
        GL_RENDERBUFFER,
        app->depth_buffer);
    // Framebuffer completeness is invariant for a given OpenXR swapchain
    // image. Checking it every eye, every display frame can force a GLES
    // driver synchronization, so validate each image once after creation.
    const uint64_t image_bit = image_index < 64U
        ? UINT64_C(1) << image_index
        : UINT64_C(0);
    if (image_bit == 0 ||
        !(swapchain->verified_framebuffer_images & image_bit)) {
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            LOGE("%s eye framebuffer is incomplete.", eye == 0 ? "Left" : "Right");
            return false;
        }
        swapchain->verified_framebuffer_images |= image_bit;
    }

    glViewport(0, 0, swapchain->width, swapchain->height);
    if (quest_game_is_ready() && quest_game_render_eye(
            eye, (uint32_t)swapchain->width, (uint32_t)swapchain->height)) {
        /* The real game rendered the eye. */
    } else if (app->rom.status == QUEST_ROM_VALID) {
        glClearColor(eye == 0 ? 0.02f : 0.03f,
                     eye == 0 ? 0.30f : 0.18f,
                     eye == 0 ? 0.08f : 0.32f, 1.0f);
    } else if (app->rom.status == QUEST_ROM_INVALID) {
        glClearColor(eye == 0 ? 0.38f : 0.24f, 0.01f, 0.01f, 1.0f);
    } else if (eye == 0) {
        glClearColor(0.02f, 0.10f, 0.32f, 1.0f);
    } else {
        glClearColor(0.22f, 0.03f, 0.32f, 1.0f);
    }
    if (!quest_game_is_ready()) {
        glClear(GL_COLOR_BUFFER_BIT);
    }
    if (!quest_game_is_ready() && app->mario_program != 0 && app->mario_texture != 0) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(app->mario_program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, app->mario_texture);
        glUniform1f(app->mario_eye_offset, eye == 0 ? 0.012f : -0.012f);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisable(GL_BLEND);
    }
    glFlush();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    XrSwapchainImageReleaseInfo release_info = {
        .type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO,
    };
    return xr_check(
        "xrReleaseSwapchainImage",
        xrReleaseSwapchainImage(swapchain->handle, &release_info));
}

static bool render_frame(QuestApp *app) {
    if (!update_render_scale_if_needed(app)) {
        LOGE("Could not recreate eye swapchains for the requested render scale.");
        return false;
    }
    XrFrameWaitInfo wait_info = {.type = XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState frame_state = {.type = XR_TYPE_FRAME_STATE};
    if (!xr_check(
            "xrWaitFrame", xrWaitFrame(app->session, &wait_info, &frame_state))) {
        return false;
    }

    XrFrameBeginInfo begin_info = {.type = XR_TYPE_FRAME_BEGIN_INFO};
    if (!xr_check("xrBeginFrame", xrBeginFrame(app->session, &begin_info))) {
        return false;
    }

    XrCompositionLayerProjectionView projection_views[QUEST_VIEW_COUNT] = {0};
    XrCompositionLayerProjection projection_layer = {
        .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
        .space = app->local_space,
        .viewCount = QUEST_VIEW_COUNT,
        .views = projection_views,
    };
    const XrCompositionLayerBaseHeader *layers[1] = {
        (const XrCompositionLayerBaseHeader *)&projection_layer,
    };
    uint32_t layer_count = 0;

    if (frame_state.shouldRender) {
        quest_game_tick();
        XrViewLocateInfo locate_info = {
            .type = XR_TYPE_VIEW_LOCATE_INFO,
            .viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
            .displayTime = frame_state.predictedDisplayTime,
            .space = app->local_space,
        };
        XrViewState view_state = {.type = XR_TYPE_VIEW_STATE};
        uint32_t view_count = 0;
        if (!xr_check(
                "xrLocateViews",
                xrLocateViews(
                    app->session,
                    &locate_info,
                    &view_state,
                    QUEST_VIEW_COUNT,
                    &view_count,
                    app->views))) {
            return false;
        }

        const XrViewStateFlags required_flags =
            XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
        if (view_count == QUEST_VIEW_COUNT
            && (view_state.viewStateFlags & required_flags) == required_flags) {
            quest_input_update(app->session, app->local_space,
                               frame_state.predictedDisplayTime);
            float positions[2][3];
            float rotations[2][4];
            float fovs[2][4];
            uint32_t widths[2], heights[2];
            for (uint32_t eye = 0; eye < QUEST_VIEW_COUNT; ++eye) {
                positions[eye][0] = app->views[eye].pose.position.x;
                positions[eye][1] = app->views[eye].pose.position.y;
                positions[eye][2] = app->views[eye].pose.position.z;
                rotations[eye][0] = app->views[eye].pose.orientation.x;
                rotations[eye][1] = app->views[eye].pose.orientation.y;
                rotations[eye][2] = app->views[eye].pose.orientation.z;
                rotations[eye][3] = app->views[eye].pose.orientation.w;
                fovs[eye][0] = app->views[eye].fov.angleLeft;
                fovs[eye][1] = app->views[eye].fov.angleRight;
                fovs[eye][2] = app->views[eye].fov.angleDown;
                fovs[eye][3] = app->views[eye].fov.angleUp;
                widths[eye] = (uint32_t)app->swapchains[eye].width;
                heights[eye] = (uint32_t)app->swapchains[eye].height;
            }
            quest_vr_bridge_update_views(positions, rotations, fovs, widths, heights);
            quest_game_prepare_vr_frame();
            for (uint32_t eye = 0; eye < QUEST_VIEW_COUNT; ++eye) {
                if (!render_eye(app, eye)) {
                    return false;
                }
                projection_views[eye].type =
                    XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
                projection_views[eye].pose = app->views[eye].pose;
                projection_views[eye].fov = app->views[eye].fov;
                projection_views[eye].subImage.swapchain =
                    app->swapchains[eye].handle;
                projection_views[eye].subImage.imageRect.extent.width =
                    app->swapchains[eye].width;
                projection_views[eye].subImage.imageRect.extent.height =
                    app->swapchains[eye].height;
                projection_views[eye].subImage.imageArrayIndex = 0;
            }
            layer_count = 1;
        }
    }

    XrFrameEndInfo end_info = {
        .type = XR_TYPE_FRAME_END_INFO,
        .displayTime = frame_state.predictedDisplayTime,
        .environmentBlendMode = app->blend_mode,
        .layerCount = layer_count,
        .layers = layer_count > 0 ? layers : NULL,
    };
    return xr_check("xrEndFrame", xrEndFrame(app->session, &end_info));
}

static bool poll_openxr_events(QuestApp *app) {
    XrEventDataBuffer event = {.type = XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(app->instance, &event) == XR_SUCCESS) {
        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const XrEventDataSessionStateChanged *changed =
                (const XrEventDataSessionStateChanged *)&event;
            app->session_state = changed->state;
            LOGI("OpenXR session state changed to %d.", (int)changed->state);

            if (changed->state == XR_SESSION_STATE_READY
                && !app->session_running) {
                XrSessionBeginInfo begin_info = {
                    .type = XR_TYPE_SESSION_BEGIN_INFO,
                    .primaryViewConfigurationType =
                        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                };
                if (!xr_check(
                        "xrBeginSession",
                        xrBeginSession(app->session, &begin_info))) {
                    return false;
                }
                app->session_running = true;
                LOGI("OpenXR session is running and submitting stereo frames.");
            } else if (changed->state == XR_SESSION_STATE_STOPPING
                       && app->session_running) {
                if (!xr_check("xrEndSession", xrEndSession(app->session))) {
                    return false;
                }
                app->session_running = false;
            } else if (changed->state == XR_SESSION_STATE_EXITING
                       || changed->state == XR_SESSION_STATE_LOSS_PENDING) {
                app->exit_requested = true;
            }
        } else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            app->exit_requested = true;
        }

        event.type = XR_TYPE_EVENT_DATA_BUFFER;
        event.next = NULL;
    }
    return true;
}

static void destroy_quest_app(QuestApp *app) {
    quest_game_flush_persistent_state();
    quest_input_shutdown();
    if (app->mario_texture != 0) glDeleteTextures(1, &app->mario_texture);
    if (app->mario_program != 0) glDeleteProgram(app->mario_program);
    destroy_swapchain_render_targets(app);
    if (app->foveation_profile != XR_NULL_HANDLE
        && app->destroy_foveation_profile != NULL) {
        app->destroy_foveation_profile(app->foveation_profile);
        app->foveation_profile = XR_NULL_HANDLE;
    }
    if (app->local_space != XR_NULL_HANDLE) {
        xrDestroySpace(app->local_space);
    }
    if (app->session != XR_NULL_HANDLE) {
        xrDestroySession(app->session);
    }
    if (app->instance != XR_NULL_HANDLE) {
        xrDestroyInstance(app->instance);
    }
    if (app->egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(
            app->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (app->egl_surface != EGL_NO_SURFACE) {
            eglDestroySurface(app->egl_display, app->egl_surface);
        }
        if (app->egl_context != EGL_NO_CONTEXT) {
            eglDestroyContext(app->egl_display, app->egl_context);
        }
        eglTerminate(app->egl_display);
    }
}

void android_main(struct android_app *android_app) {
    QuestApp app = {
        .android_app = android_app,
        .egl_display = EGL_NO_DISPLAY,
        .egl_context = EGL_NO_CONTEXT,
        .egl_surface = EGL_NO_SURFACE,
        .instance = XR_NULL_HANDLE,
        .system_id = XR_NULL_SYSTEM_ID,
        .session = XR_NULL_HANDLE,
        .local_space = XR_NULL_HANDLE,
        .session_state = XR_SESSION_STATE_UNKNOWN,
        .blend_mode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
    };

    android_app->userData = &app;
    android_app->onAppCmd = handle_app_command;
    sActiveQuestApp = &app;

    extern void quest_android_set_user_path(const char *path);
    quest_android_set_user_path(android_app->activity->externalDataPath);

    LOGI("Starting Android/Quest game integration test.");
    const bool android_ready = wait_for_android_ready(&app);
    if (android_ready) {
        app.rom = quest_rom_find_and_validate(android_app);
        if (app.rom.status == QUEST_ROM_VALID) {
            LOGI("Valid US SM64 ROM found at %s (SHA-1 %s).",
                 app.rom.path, app.rom.sha1);
        } else if (app.rom.status == QUEST_ROM_INVALID) {
            LOGE("ROM at %s is not the supported US ROM (SHA-1 %s).",
                 app.rom.path, app.rom.sha1);
        } else {
            LOGI("No ROM found. Copy baserom.us.z64 into %s.",
                 android_app->activity->externalDataPath);
        }
    }
    if (android_ready && app.rom.status == QUEST_ROM_VALID) {
        quest_game_load_early_config();
    }
    bool initialized = android_ready && create_egl_context(&app)
        && create_mario_preview(&app)
        && create_openxr_instance(&app)
        && create_openxr_session(&app)
        && quest_input_initialize(app.instance, app.session)
        && create_swapchains(&app);
    if (initialized && app.rom.status == QUEST_ROM_VALID
        && !quest_game_initialize()) {
        LOGE("Full game initialization failed; retaining ROM preview fallback.");
    }
    if (!initialized) {
        LOGE("Quest OpenXR smoke test initialization failed.");
    }

    while (!android_app->destroyRequested && !app.exit_requested) {
        int events = 0;
        struct android_poll_source *source = NULL;
        const int timeout = initialized && app.session_running ? 0 : 50;
        while (ALooper_pollOnce(timeout, NULL, &events, (void **)&source) >= 0) {
            if (source != NULL) {
                source->process(android_app, source);
            }
            if (android_app->destroyRequested) {
                break;
            }
            if (timeout != 0) {
                break;
            }
        }

        if (!initialized) {
            continue;
        }
        if (!poll_openxr_events(&app)) {
            break;
        }
        if (app.session_running && !render_frame(&app)) {
            break;
        }
    }

    destroy_quest_app(&app);
    if (sActiveQuestApp == &app) sActiveQuestApp = NULL;
    LOGI("Android/Quest OpenXR smoke test stopped.");

    // NativeActivity normally leaves its Linux process cached after
    // android_main returns. The SM64 runtime and renderer use process-lifetime
    // static state, so reusing that cached process creates a dead black
    // session on the next launch. Both the in-game Quit command and a real
    // Android Activity destroy must end the process after OpenXR/EGL cleanup.
    fflush(NULL);
    _exit(EXIT_SUCCESS);
}
