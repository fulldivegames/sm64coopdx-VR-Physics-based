#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pc/vr/vr_openxr.h"

#ifdef _WIN32

#include <windows.h>

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

struct VrOpenXrFunctions {
    PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr;
    PFN_xrEnumerateInstanceExtensionProperties xrEnumerateInstanceExtensionProperties;
    PFN_xrCreateInstance xrCreateInstance;
    PFN_xrDestroyInstance xrDestroyInstance;
    PFN_xrGetInstanceProperties xrGetInstanceProperties;
    PFN_xrGetSystem xrGetSystem;
    PFN_xrGetSystemProperties xrGetSystemProperties;
    PFN_xrGetOpenGLGraphicsRequirementsKHR xrGetOpenGLGraphicsRequirementsKHR;
    PFN_xrCreateSession xrCreateSession;
    PFN_xrDestroySession xrDestroySession;
    PFN_xrPollEvent xrPollEvent;
    PFN_xrBeginSession xrBeginSession;
    PFN_xrEndSession xrEndSession;
    PFN_xrWaitFrame xrWaitFrame;
    PFN_xrBeginFrame xrBeginFrame;
    PFN_xrEndFrame xrEndFrame;
    PFN_xrEnumerateEnvironmentBlendModes xrEnumerateEnvironmentBlendModes;
    PFN_xrCreateReferenceSpace xrCreateReferenceSpace;
    PFN_xrDestroySpace xrDestroySpace;
    PFN_xrLocateViews xrLocateViews;
    PFN_xrEnumerateViewConfigurationViews xrEnumerateViewConfigurationViews;
};

static HMODULE sLoader = NULL;
static XrInstance sInstance = XR_NULL_HANDLE;
static XrSystemId sSystemId = XR_NULL_SYSTEM_ID;
static XrSession sSession = XR_NULL_HANDLE;
static XrSpace sViewSpace = XR_NULL_HANDLE;
static XrSessionState sSessionState = XR_SESSION_STATE_UNKNOWN;
static bool sSessionRunning = false;
static XrEnvironmentBlendMode sEnvironmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
static XrView sViews[2] = { 0 };
static XrViewConfigurationView sViewConfigurationViews[2] = { 0 };
static bool sViewPoseValid = false;
static uint32_t sPoseLogFrame = 0;
static struct VrOpenXrFunctions sXr = { 0 };

static const char* vr_openxr_result_name(XrResult result) {
    switch (result) {
        case XR_SUCCESS: return "XR_SUCCESS";
        case XR_ERROR_RUNTIME_UNAVAILABLE: return "XR_ERROR_RUNTIME_UNAVAILABLE";
        case XR_ERROR_FORM_FACTOR_UNAVAILABLE: return "XR_ERROR_FORM_FACTOR_UNAVAILABLE";
        case XR_ERROR_FORM_FACTOR_UNSUPPORTED: return "XR_ERROR_FORM_FACTOR_UNSUPPORTED";
        case XR_ERROR_API_VERSION_UNSUPPORTED: return "XR_ERROR_API_VERSION_UNSUPPORTED";
        case XR_ERROR_INITIALIZATION_FAILED: return "XR_ERROR_INITIALIZATION_FAILED";
        case XR_ERROR_RUNTIME_FAILURE: return "XR_ERROR_RUNTIME_FAILURE";
        case XR_ERROR_EXTENSION_NOT_PRESENT: return "XR_ERROR_EXTENSION_NOT_PRESENT";
        case XR_ERROR_FUNCTION_UNSUPPORTED: return "XR_ERROR_FUNCTION_UNSUPPORTED";
        case XR_ERROR_GRAPHICS_DEVICE_INVALID: return "XR_ERROR_GRAPHICS_DEVICE_INVALID";
        case XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING: return "XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING";
        default: return "UNKNOWN_OPENXR_ERROR";
    }
}

static const char* vr_openxr_session_state_name(XrSessionState state) {
    switch (state) {
        case XR_SESSION_STATE_IDLE: return "IDLE";
        case XR_SESSION_STATE_READY: return "READY";
        case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
        case XR_SESSION_STATE_VISIBLE: return "VISIBLE";
        case XR_SESSION_STATE_FOCUSED: return "FOCUSED";
        case XR_SESSION_STATE_STOPPING: return "STOPPING";
        case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
        case XR_SESSION_STATE_EXITING: return "EXITING";
        default: return "UNKNOWN";
    }
}

static HMODULE vr_openxr_load_loader(void) {
    HMODULE loader = LoadLibraryA("libopenxr_loader.dll");
    if (loader == NULL) {
        loader = LoadLibraryA("openxr_loader.dll");
    }
    return loader;
}

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif

static bool vr_openxr_load_bootstrap_function(void) {
    FARPROC function = GetProcAddress(sLoader, "xrGetInstanceProcAddr");
    if (function == NULL) {
        printf("[VR] Could not find xrGetInstanceProcAddr.\n");
        return false;
    }

    sXr.xrGetInstanceProcAddr = (PFN_xrGetInstanceProcAddr)function;
    return true;
}

static bool vr_openxr_get_function(XrInstance instance, const char* name, PFN_xrVoidFunction* function) {
    *function = NULL;

    XrResult result = sXr.xrGetInstanceProcAddr(instance, name, function);
    if (XR_FAILED(result) || *function == NULL) {
        printf(
            "[VR] Could not load OpenXR function %s: %s (%d)\n",
            name,
            vr_openxr_result_name(result),
            (int)result
        );
        return false;
    }

    return true;
}

static bool vr_openxr_load_global_functions(void) {
    PFN_xrVoidFunction function = NULL;

    if (!vr_openxr_get_function(XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties", &function)) {
        return false;
    }
    sXr.xrEnumerateInstanceExtensionProperties =
        (PFN_xrEnumerateInstanceExtensionProperties)function;

    if (!vr_openxr_get_function(XR_NULL_HANDLE, "xrCreateInstance", &function)) {
        return false;
    }
    sXr.xrCreateInstance = (PFN_xrCreateInstance)function;

    return true;
}

static bool vr_openxr_load_instance_functions(void) {
    PFN_xrVoidFunction function = NULL;

#define LOAD_XR_FUNCTION(field, name, type) \
    do { \
        if (!vr_openxr_get_function(sInstance, name, &function)) { \
            return false; \
        } \
        sXr.field = (type)function; \
    } while (0)

    LOAD_XR_FUNCTION(xrDestroyInstance, "xrDestroyInstance", PFN_xrDestroyInstance);
    LOAD_XR_FUNCTION(xrGetInstanceProperties, "xrGetInstanceProperties", PFN_xrGetInstanceProperties);
    LOAD_XR_FUNCTION(xrGetSystem, "xrGetSystem", PFN_xrGetSystem);
    LOAD_XR_FUNCTION(xrGetSystemProperties, "xrGetSystemProperties", PFN_xrGetSystemProperties);
    LOAD_XR_FUNCTION(xrGetOpenGLGraphicsRequirementsKHR, "xrGetOpenGLGraphicsRequirementsKHR", PFN_xrGetOpenGLGraphicsRequirementsKHR);
    LOAD_XR_FUNCTION(xrCreateSession, "xrCreateSession", PFN_xrCreateSession);
    LOAD_XR_FUNCTION(xrDestroySession, "xrDestroySession", PFN_xrDestroySession);
    LOAD_XR_FUNCTION(xrPollEvent, "xrPollEvent", PFN_xrPollEvent);
    LOAD_XR_FUNCTION(xrBeginSession, "xrBeginSession", PFN_xrBeginSession);
    LOAD_XR_FUNCTION(xrEndSession, "xrEndSession", PFN_xrEndSession);
    LOAD_XR_FUNCTION(xrWaitFrame, "xrWaitFrame", PFN_xrWaitFrame);
    LOAD_XR_FUNCTION(xrBeginFrame, "xrBeginFrame", PFN_xrBeginFrame);
    LOAD_XR_FUNCTION(xrEndFrame, "xrEndFrame", PFN_xrEndFrame);
    LOAD_XR_FUNCTION(xrEnumerateEnvironmentBlendModes, "xrEnumerateEnvironmentBlendModes", PFN_xrEnumerateEnvironmentBlendModes);
    LOAD_XR_FUNCTION(xrCreateReferenceSpace, "xrCreateReferenceSpace", PFN_xrCreateReferenceSpace);
    LOAD_XR_FUNCTION(xrDestroySpace, "xrDestroySpace", PFN_xrDestroySpace);
    LOAD_XR_FUNCTION(xrLocateViews, "xrLocateViews", PFN_xrLocateViews);
    LOAD_XR_FUNCTION(xrEnumerateViewConfigurationViews, "xrEnumerateViewConfigurationViews", PFN_xrEnumerateViewConfigurationViews);

#undef LOAD_XR_FUNCTION

    return true;
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static bool vr_openxr_has_opengl_extension(void) {
    uint32_t extensionCount = 0;

    XrResult result =
        sXr.xrEnumerateInstanceExtensionProperties(NULL, 0, &extensionCount, NULL);

    if (XR_FAILED(result)) {
        printf(
            "[VR] Could not enumerate OpenXR extensions: %s (%d)\n",
            vr_openxr_result_name(result),
            (int)result
        );
        return false;
    }

    XrExtensionProperties* extensions =
        calloc(extensionCount, sizeof(XrExtensionProperties));

    if (extensions == NULL) {
        printf("[VR] Could not allocate OpenXR extension list.\n");
        return false;
    }

    for (uint32_t i = 0; i < extensionCount; i++) {
        extensions[i].type = XR_TYPE_EXTENSION_PROPERTIES;
    }

    result =
        sXr.xrEnumerateInstanceExtensionProperties(
            NULL,
            extensionCount,
            &extensionCount,
            extensions
        );

    if (XR_FAILED(result)) {
        printf(
            "[VR] Could not read OpenXR extensions: %s (%d)\n",
            vr_openxr_result_name(result),
            (int)result
        );
        free(extensions);
        return false;
    }

    bool foundOpenGL = false;

    for (uint32_t i = 0; i < extensionCount; i++) {
        if (strcmp(
                extensions[i].extensionName,
                XR_KHR_OPENGL_ENABLE_EXTENSION_NAME
            ) == 0) {
            foundOpenGL = true;
            break;
        }
    }

    free(extensions);
    return foundOpenGL;
}

static bool vr_openxr_choose_blend_mode(void) {
    uint32_t modeCount = 0;

    XrResult result =
        sXr.xrEnumerateEnvironmentBlendModes(
            sInstance,
            sSystemId,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
            0,
            &modeCount,
            NULL
        );

    if (XR_FAILED(result) || modeCount == 0) {
        printf("[VR] Could not enumerate environment blend modes.\n");
        return false;
    }

    XrEnvironmentBlendMode* modes =
        calloc(modeCount, sizeof(XrEnvironmentBlendMode));

    if (modes == NULL) {
        printf("[VR] Could not allocate environment blend mode list.\n");
        return false;
    }

    result =
        sXr.xrEnumerateEnvironmentBlendModes(
            sInstance,
            sSystemId,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
            modeCount,
            &modeCount,
            modes
        );

    if (XR_FAILED(result)) {
        free(modes);
        printf("[VR] Could not read environment blend modes.\n");
        return false;
    }

    sEnvironmentBlendMode = modes[0];

    for (uint32_t i = 0; i < modeCount; i++) {
        if (modes[i] == XR_ENVIRONMENT_BLEND_MODE_OPAQUE) {
            sEnvironmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
            break;
        }
    }

    free(modes);

    printf("[VR] OpenXR environment blend mode selected.\n");
    return true;
}

static bool vr_openxr_create_view_space(void) {
    XrReferenceSpaceCreateInfo spaceInfo = { 0 };
    spaceInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;

    XrResult result =
        sXr.xrCreateReferenceSpace(
            sSession,
            &spaceInfo,
            &sViewSpace
        );

    if (XR_FAILED(result)) {
        printf(
            "[VR] xrCreateReferenceSpace failed: %s (%d)\n",
            vr_openxr_result_name(result),
            (int)result
        );
        sViewSpace = XR_NULL_HANDLE;
        return false;
    }

    printf("[VR] OpenXR local reference space created.\n");
    return true;
}

static bool vr_openxr_query_view_configuration(void) {
    uint32_t viewCount = 0;

    XrResult result =
        sXr.xrEnumerateViewConfigurationViews(
            sInstance,
            sSystemId,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
            0,
            &viewCount,
            NULL
        );

    if (XR_FAILED(result)) {
        printf(
            "[VR] Could not enumerate stereo view configuration: "
            "%s (%d)\n",
            vr_openxr_result_name(result),
            (int)result
        );
        return false;
    }

    if (viewCount != 2) {
        printf(
            "[VR] Primary stereo configuration reported %u views; "
            "expected 2.\n",
            viewCount
        );
        return false;
    }

    for (uint32_t i = 0; i < 2; i++) {
        sViewConfigurationViews[i].type =
            XR_TYPE_VIEW_CONFIGURATION_VIEW;
        sViewConfigurationViews[i].next = NULL;
    }

    result =
        sXr.xrEnumerateViewConfigurationViews(
            sInstance,
            sSystemId,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
            2,
            &viewCount,
            sViewConfigurationViews
        );

    if (XR_FAILED(result)) {
        printf(
            "[VR] Could not read stereo view configuration: "
            "%s (%d)\n",
            vr_openxr_result_name(result),
            (int)result
        );
        return false;
    }

    if (viewCount != 2) {
        printf(
            "[VR] Stereo view count changed to %u while reading it.\n",
            viewCount
        );
        return false;
    }

    printf("[VR] Primary stereo view configuration: 2 eyes.\n");

    for (uint32_t i = 0; i < 2; i++) {
        const XrViewConfigurationView* view =
            &sViewConfigurationViews[i];
        const char* eyeName = i == 0 ? "left" : "right";

        printf(
            "[VR] %s eye target: recommended %ux%u at %u sample(s); "
            "maximum %ux%u at %u sample(s).\n",
            eyeName,
            view->recommendedImageRectWidth,
            view->recommendedImageRectHeight,
            view->recommendedSwapchainSampleCount,
            view->maxImageRectWidth,
            view->maxImageRectHeight,
            view->maxSwapchainSampleCount
        );
    }

    return true;
}

bool vr_openxr_startup(void) {
    if (sInstance != XR_NULL_HANDLE) {
        return true;
    }

    sLoader = vr_openxr_load_loader();

    if (sLoader == NULL) {
        printf("[VR] Could not load the OpenXR loader DLL.\n");
        return false;
    }

    printf("[VR] OpenXR loader DLL found.\n");

    if (!vr_openxr_load_bootstrap_function()) {
        vr_openxr_shutdown();
        return false;
    }

    if (!vr_openxr_load_global_functions()) {
        vr_openxr_shutdown();
        return false;
    }

    printf("[VR] Checking for OpenGL OpenXR support...\n");

    if (!vr_openxr_has_opengl_extension()) {
        printf("[VR] OpenXR runtime does not provide XR_KHR_opengl_enable.\n");
        vr_openxr_shutdown();
        return false;
    }

    printf("[VR] XR_KHR_opengl_enable is supported.\n");

    const char* enabledExtensions[] = {
        XR_KHR_OPENGL_ENABLE_EXTENSION_NAME
    };

    XrInstanceCreateInfo createInfo = { 0 };
    createInfo.type = XR_TYPE_INSTANCE_CREATE_INFO;

    snprintf(
        createInfo.applicationInfo.applicationName,
        XR_MAX_APPLICATION_NAME_SIZE,
        "%s",
        "sm64coopdx VR"
    );
    createInfo.applicationInfo.applicationVersion = 1;

    snprintf(
        createInfo.applicationInfo.engineName,
        XR_MAX_ENGINE_NAME_SIZE,
        "%s",
        "sm64coopdx"
    );
    createInfo.applicationInfo.engineVersion = 1;
    createInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);

    createInfo.enabledExtensionCount = 1;
    createInfo.enabledExtensionNames = enabledExtensions;

    XrResult result =
        sXr.xrCreateInstance(&createInfo, &sInstance);

    if (XR_FAILED(result)) {
        printf(
            "[VR] xrCreateInstance failed: %s (%d)\n",
            vr_openxr_result_name(result),
            (int)result
        );
        sInstance = XR_NULL_HANDLE;
        vr_openxr_shutdown();
        return false;
    }

    printf("[VR] OpenXR instance created successfully.\n");

    if (!vr_openxr_load_instance_functions()) {
        vr_openxr_shutdown();
        return false;
    }

    XrInstanceProperties instanceProperties = { 0 };
    instanceProperties.type = XR_TYPE_INSTANCE_PROPERTIES;

    result =
        sXr.xrGetInstanceProperties(sInstance, &instanceProperties);

    if (XR_SUCCEEDED(result)) {
        printf("[VR] OpenXR runtime: %s\n", instanceProperties.runtimeName);
    }

    XrSystemGetInfo systemInfo = { 0 };
    systemInfo.type = XR_TYPE_SYSTEM_GET_INFO;
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    result =
        sXr.xrGetSystem(sInstance, &systemInfo, &sSystemId);

    if (XR_FAILED(result)) {
        printf(
            "[VR] Could not find an HMD: %s (%d)\n",
            vr_openxr_result_name(result),
            (int)result
        );
        sSystemId = XR_NULL_SYSTEM_ID;
        vr_openxr_shutdown();
        return false;
    }

    XrSystemProperties systemProperties = { 0 };
    systemProperties.type = XR_TYPE_SYSTEM_PROPERTIES;

    result =
        sXr.xrGetSystemProperties(sInstance, sSystemId, &systemProperties);

    if (XR_SUCCEEDED(result)) {
        printf("[VR] OpenXR headset/system: %s\n", systemProperties.systemName);
    }

    if (!vr_openxr_query_view_configuration()) {
        vr_openxr_shutdown();
        return false;
    }

    XrGraphicsRequirementsOpenGLKHR graphicsRequirements = { 0 };
    graphicsRequirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR;

    result =
        sXr.xrGetOpenGLGraphicsRequirementsKHR(
            sInstance,
            sSystemId,
            &graphicsRequirements
        );

    if (XR_FAILED(result)) {
        printf(
            "[VR] Could not query OpenGL requirements: %s (%d)\n",
            vr_openxr_result_name(result),
            (int)result
        );
        vr_openxr_shutdown();
        return false;
    }

    printf(
        "[VR] Runtime OpenGL range: %u.%u through %u.%u\n",
        XR_VERSION_MAJOR(graphicsRequirements.minApiVersionSupported),
        XR_VERSION_MINOR(graphicsRequirements.minApiVersionSupported),
        XR_VERSION_MAJOR(graphicsRequirements.maxApiVersionSupported),
        XR_VERSION_MINOR(graphicsRequirements.maxApiVersionSupported)
    );

    printf("[VR] Persistent OpenXR context is ready.\n");
    return true;
}

bool vr_openxr_create_session(void) {
    if (sSession != XR_NULL_HANDLE) {
        return true;
    }

    if (sInstance == XR_NULL_HANDLE ||
        sSystemId == XR_NULL_SYSTEM_ID) {
        printf("[VR] Cannot create session: OpenXR is not initialized.\n");
        return false;
    }

    HDC hdc = wglGetCurrentDC();
    HGLRC hglrc = wglGetCurrentContext();

    if (hdc == NULL || hglrc == NULL) {
        printf("[VR] Could not find the current Windows OpenGL context.\n");
        return false;
    }

    printf("[VR] Current Windows OpenGL context found.\n");

    if (!vr_openxr_choose_blend_mode()) {
        return false;
    }

    XrGraphicsBindingOpenGLWin32KHR graphicsBinding = { 0 };
    graphicsBinding.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR;
    graphicsBinding.hDC = hdc;
    graphicsBinding.hGLRC = hglrc;

    XrSessionCreateInfo sessionInfo = { 0 };
    sessionInfo.type = XR_TYPE_SESSION_CREATE_INFO;
    sessionInfo.next = &graphicsBinding;
    sessionInfo.systemId = sSystemId;

    printf("[VR] Creating OpenXR OpenGL session...\n");

    XrResult result =
        sXr.xrCreateSession(sInstance, &sessionInfo, &sSession);

    if (XR_FAILED(result)) {
        printf(
            "[VR] xrCreateSession failed: %s (%d)\n",
            vr_openxr_result_name(result),
            (int)result
        );
        sSession = XR_NULL_HANDLE;
        return false;
    }

    sSessionState = XR_SESSION_STATE_IDLE;
    sSessionRunning = false;

    if (!vr_openxr_create_view_space()) {
        sXr.xrDestroySession(sSession);
        sSession = XR_NULL_HANDLE;
        return false;
    }

    printf("[VR] OpenXR OpenGL session created successfully.\n");
    printf("[VR] Waiting for OpenXR session state events.\n");

    return true;
}

static bool vr_openxr_handle_session_state(
    const XrEventDataSessionStateChanged* event
) {
    sSessionState = event->state;

    printf(
        "[VR] OpenXR session state: %s\n",
        vr_openxr_session_state_name(sSessionState)
    );

    switch (sSessionState) {
        case XR_SESSION_STATE_READY: {
            if (sSessionRunning) {
                break;
            }

            XrSessionBeginInfo beginInfo = { 0 };
            beginInfo.type = XR_TYPE_SESSION_BEGIN_INFO;
            beginInfo.primaryViewConfigurationType =
                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

            XrResult result =
                sXr.xrBeginSession(sSession, &beginInfo);

            if (XR_FAILED(result)) {
                printf(
                    "[VR] xrBeginSession failed: %s (%d)\n",
                    vr_openxr_result_name(result),
                    (int)result
                );
                return false;
            }

            sSessionRunning = true;
            printf("[VR] OpenXR session is now running.\n");
            break;
        }

        case XR_SESSION_STATE_STOPPING: {
            if (!sSessionRunning) {
                break;
            }

            XrResult result = sXr.xrEndSession(sSession);

            if (XR_FAILED(result)) {
                printf(
                    "[VR] xrEndSession failed: %s (%d)\n",
                    vr_openxr_result_name(result),
                    (int)result
                );
                return false;
            }

            sSessionRunning = false;
            printf("[VR] OpenXR session stopped.\n");
            break;
        }

        case XR_SESSION_STATE_EXITING:
            printf("[VR] OpenXR runtime requested exit.\n");
            return false;

        case XR_SESSION_STATE_LOSS_PENDING:
            printf("[VR] OpenXR session loss pending.\n");
            return false;

        default:
            break;
    }

    return true;
}

static bool vr_openxr_poll_events(void) {
    XrEventDataBuffer event = { 0 };
    event.type = XR_TYPE_EVENT_DATA_BUFFER;

    while (true) {
        XrResult result =
            sXr.xrPollEvent(sInstance, &event);

        if (result == XR_EVENT_UNAVAILABLE) {
            break;
        }

        if (XR_FAILED(result)) {
            printf(
                "[VR] xrPollEvent failed: %s (%d)\n",
                vr_openxr_result_name(result),
                (int)result
            );
            return false;
        }

        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const XrEventDataSessionStateChanged* stateEvent =
                (const XrEventDataSessionStateChanged*)&event;

            if (!vr_openxr_handle_session_state(stateEvent)) {
                return false;
            }
        }

        memset(&event, 0, sizeof(event));
        event.type = XR_TYPE_EVENT_DATA_BUFFER;
    }

    return true;
}

static void vr_openxr_log_views(void) {
    printf(
        "[VR] HMD eyes | "
        "L pos(%.3f, %.3f, %.3f) rot(%.3f, %.3f, %.3f, %.3f) | "
        "R pos(%.3f, %.3f, %.3f) rot(%.3f, %.3f, %.3f, %.3f)\n",
        sViews[0].pose.position.x,
        sViews[0].pose.position.y,
        sViews[0].pose.position.z,
        sViews[0].pose.orientation.x,
        sViews[0].pose.orientation.y,
        sViews[0].pose.orientation.z,
        sViews[0].pose.orientation.w,
        sViews[1].pose.position.x,
        sViews[1].pose.position.y,
        sViews[1].pose.position.z,
        sViews[1].pose.orientation.x,
        sViews[1].pose.orientation.y,
        sViews[1].pose.orientation.z,
        sViews[1].pose.orientation.w
    );
}

static bool vr_openxr_locate_views(XrTime displayTime) {
    XrViewLocateInfo locateInfo = { 0 };
    locateInfo.type = XR_TYPE_VIEW_LOCATE_INFO;
    locateInfo.viewConfigurationType =
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locateInfo.displayTime = displayTime;
    locateInfo.space = sViewSpace;

    XrViewState viewState = { 0 };
    viewState.type = XR_TYPE_VIEW_STATE;

    for (uint32_t i = 0; i < 2; i++) {
        sViews[i].type = XR_TYPE_VIEW;
        sViews[i].next = NULL;
    }

    uint32_t viewCount = 0;
    XrResult result =
        sXr.xrLocateViews(
            sSession,
            &locateInfo,
            &viewState,
            2,
            &viewCount,
            sViews
        );

    if (XR_FAILED(result)) {
        printf(
            "[VR] xrLocateViews failed: %s (%d)\n",
            vr_openxr_result_name(result),
            (int)result
        );
        sViewPoseValid = false;
        return false;
    }

    if (viewCount != 2) {
        printf(
            "[VR] xrLocateViews returned %u views; expected 2.\n",
            viewCount
        );
        sViewPoseValid = false;
        return false;
    }

    const XrViewStateFlags requiredFlags =
        XR_VIEW_STATE_POSITION_VALID_BIT |
        XR_VIEW_STATE_ORIENTATION_VALID_BIT;

    const bool poseValid =
        (viewState.viewStateFlags & requiredFlags) == requiredFlags;

    if (!poseValid) {
        if (sViewPoseValid ||
            sPoseLogFrame == 0 ||
            sPoseLogFrame >= 90) {
            printf("[VR] OpenXR view pose is temporarily invalid.\n");
            sPoseLogFrame = 1;
        } else {
            sPoseLogFrame++;
        }
        sViewPoseValid = false;
        return true;
    }

    if (!sViewPoseValid) {
        printf("[VR] Valid stereo 6DoF view poses acquired.\n");
        vr_openxr_log_views();
        sPoseLogFrame = 0;
    } else {
        sPoseLogFrame++;
        if (sPoseLogFrame >= 90) {
            vr_openxr_log_views();
            sPoseLogFrame = 0;
        }
    }

    sViewPoseValid = true;
    return true;
}

static bool vr_openxr_submit_empty_frame(void) {
    if (!sSessionRunning) {
        return true;
    }

    XrFrameWaitInfo waitInfo = { 0 };
    waitInfo.type = XR_TYPE_FRAME_WAIT_INFO;

    XrFrameState frameState = { 0 };
    frameState.type = XR_TYPE_FRAME_STATE;

    XrResult result =
        sXr.xrWaitFrame(sSession, &waitInfo, &frameState);

    if (XR_FAILED(result)) {
        printf(
            "[VR] xrWaitFrame failed: %s (%d)\n",
            vr_openxr_result_name(result),
            (int)result
        );
        return false;
    }

    bool viewsLocated =
        vr_openxr_locate_views(frameState.predictedDisplayTime);

    XrFrameBeginInfo beginInfo = { 0 };
    beginInfo.type = XR_TYPE_FRAME_BEGIN_INFO;

    result =
        sXr.xrBeginFrame(sSession, &beginInfo);

    if (XR_FAILED(result)) {
        printf(
            "[VR] xrBeginFrame failed: %s (%d)\n",
            vr_openxr_result_name(result),
            (int)result
        );
        return false;
    }

    XrFrameEndInfo endInfo = { 0 };
    endInfo.type = XR_TYPE_FRAME_END_INFO;
    endInfo.displayTime = frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = sEnvironmentBlendMode;
    endInfo.layerCount = 0;
    endInfo.layers = NULL;

    result =
        sXr.xrEndFrame(sSession, &endInfo);

    if (XR_FAILED(result)) {
        printf(
            "[VR] xrEndFrame failed: %s (%d)\n",
            vr_openxr_result_name(result),
            (int)result
        );
        return false;
    }

    return viewsLocated;
}

bool vr_openxr_update(void) {
    if (sSession == XR_NULL_HANDLE) {
        return true;
    }

    if (!vr_openxr_poll_events()) {
        return false;
    }

    if (!vr_openxr_submit_empty_frame()) {
        return false;
    }

    return true;
}

void vr_openxr_shutdown(void) {
    bool hadOpenXR =
        sViewSpace != XR_NULL_HANDLE ||
        sSession != XR_NULL_HANDLE ||
        sInstance != XR_NULL_HANDLE ||
        sLoader != NULL;

    if (sViewSpace != XR_NULL_HANDLE &&
        sXr.xrDestroySpace != NULL) {
        sXr.xrDestroySpace(sViewSpace);
    }

    sViewSpace = XR_NULL_HANDLE;
    memset(sViews, 0, sizeof(sViews));
    memset(
        sViewConfigurationViews,
        0,
        sizeof(sViewConfigurationViews)
    );
    sViewPoseValid = false;
    sPoseLogFrame = 0;

    if (sSession != XR_NULL_HANDLE &&
        sXr.xrDestroySession != NULL) {
        sXr.xrDestroySession(sSession);
    }

    sSession = XR_NULL_HANDLE;
    sSessionRunning = false;
    sSessionState = XR_SESSION_STATE_UNKNOWN;
    sEnvironmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

    if (sInstance != XR_NULL_HANDLE &&
        sXr.xrDestroyInstance != NULL) {
        sXr.xrDestroyInstance(sInstance);
    }

    sInstance = XR_NULL_HANDLE;
    sSystemId = XR_NULL_SYSTEM_ID;

    if (sLoader != NULL) {
        FreeLibrary(sLoader);
    }

    sLoader = NULL;
    memset(&sXr, 0, sizeof(sXr));

    if (hadOpenXR) {
        printf("[VR] OpenXR context shut down.\n");
    }
}

bool vr_openxr_is_initialized(void) {
    return sInstance != XR_NULL_HANDLE;
}

bool vr_openxr_has_session(void) {
    return sSession != XR_NULL_HANDLE;
}

bool vr_openxr_is_session_running(void) {
    return sSessionRunning;
}

#else

bool vr_openxr_startup(void) {
    printf(
        "[VR] OpenXR startup is not implemented "
        "on this platform yet.\n"
    );
    return false;
}

bool vr_openxr_create_session(void) {
    return false;
}

bool vr_openxr_update(void) {
    return false;
}

void vr_openxr_shutdown(void) {
}

bool vr_openxr_is_initialized(void) {
    return false;
}

bool vr_openxr_has_session(void) {
    return false;
}

bool vr_openxr_is_session_running(void) {
    return false;
}

#endif
