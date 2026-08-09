#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pc/configfile.h"
#include "pc/vr/vr.h"
#include "pc/vr/vr_openxr.h"

#ifdef _WIN32

#include <windows.h>

#ifndef GLEW_STATIC
#define GLEW_STATIC
#endif
#include <GL/glew.h>

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#define VR_GL_RGBA8 0x8058
#define VR_GL_SRGB8_ALPHA8 0x8C43

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
    PFN_xrCreateActionSpace xrCreateActionSpace;
    PFN_xrDestroySpace xrDestroySpace;
    PFN_xrLocateSpace xrLocateSpace;
    PFN_xrLocateViews xrLocateViews;
    PFN_xrStringToPath xrStringToPath;
    PFN_xrCreateActionSet xrCreateActionSet;
    PFN_xrDestroyActionSet xrDestroyActionSet;
    PFN_xrCreateAction xrCreateAction;
    PFN_xrSuggestInteractionProfileBindings xrSuggestInteractionProfileBindings;
    PFN_xrAttachSessionActionSets xrAttachSessionActionSets;
    PFN_xrGetActionStateBoolean xrGetActionStateBoolean;
    PFN_xrGetActionStateFloat xrGetActionStateFloat;
    PFN_xrGetActionStateVector2f xrGetActionStateVector2f;
    PFN_xrGetActionStatePose xrGetActionStatePose;
    PFN_xrSyncActions xrSyncActions;
    PFN_xrApplyHapticFeedback xrApplyHapticFeedback;
    PFN_xrStopHapticFeedback xrStopHapticFeedback;
    PFN_xrEnumerateViewConfigurationViews xrEnumerateViewConfigurationViews;
    PFN_xrEnumerateSwapchainFormats xrEnumerateSwapchainFormats;
    PFN_xrCreateSwapchain xrCreateSwapchain;
    PFN_xrDestroySwapchain xrDestroySwapchain;
    PFN_xrEnumerateSwapchainImages xrEnumerateSwapchainImages;
    PFN_xrAcquireSwapchainImage xrAcquireSwapchainImage;
    PFN_xrWaitSwapchainImage xrWaitSwapchainImage;
    PFN_xrReleaseSwapchainImage xrReleaseSwapchainImage;
};

struct VrOpenXrSwapchain {
    XrSwapchain handle;
    XrSwapchainImageOpenGLKHR* images;
    bool* framebufferValidated;
    uint32_t imageCount;
    uint32_t width;
    uint32_t height;
};

static HMODULE sLoader = NULL;
static XrInstance sInstance = XR_NULL_HANDLE;
static XrSystemId sSystemId = XR_NULL_SYSTEM_ID;
static XrSession sSession = XR_NULL_HANDLE;
static XrSpace sViewSpace = XR_NULL_HANDLE;
static XrActionSet sControllerActionSet = XR_NULL_HANDLE;
static XrAction sGripPoseAction = XR_NULL_HANDLE;
static XrAction sAimPoseAction = XR_NULL_HANDLE;
static XrAction sTriggerAction = XR_NULL_HANDLE;
static XrAction sSqueezeAction = XR_NULL_HANDLE;
static XrAction sThumbstickAction = XR_NULL_HANDLE;
static XrAction sPrimaryButtonAction = XR_NULL_HANDLE;
static XrAction sSecondaryButtonAction = XR_NULL_HANDLE;
static XrAction sMenuButtonAction = XR_NULL_HANDLE;
static XrAction sThumbstickButtonAction = XR_NULL_HANDLE;
static XrAction sHapticAction = XR_NULL_HANDLE;
static XrPath sControllerHandPaths[VR_CONTROLLER_COUNT] = {
    XR_NULL_PATH,
    XR_NULL_PATH
};
static XrSpace sControllerGripSpaces[VR_CONTROLLER_COUNT] = {
    XR_NULL_HANDLE,
    XR_NULL_HANDLE
};
static XrSpace sControllerAimSpaces[VR_CONTROLLER_COUNT] = {
    XR_NULL_HANDLE,
    XR_NULL_HANDLE
};
static struct VrControllerState
    sControllerStates[VR_CONTROLLER_COUNT] = { 0 };
static bool sControllerPreviousGripValid[VR_CONTROLLER_COUNT] = {
    false,
    false
};
static float
    sControllerPreviousGripPosition[VR_CONTROLLER_COUNT][3] = { 0 };
static XrTime sControllerPreviousGripTime[VR_CONTROLLER_COUNT] = {
    0,
    0
};
static bool sControllerVelocityFallbackLogged = false;
static bool sControllerActionsReady = false;
static bool sControllerActionsAttached = false;
static bool sControllerSyncErrorLogged = false;
static XrSessionState sSessionState = XR_SESSION_STATE_UNKNOWN;
static bool sSessionRunning = false;
static XrEnvironmentBlendMode sEnvironmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
static XrView sViews[2] = { 0 };
static XrViewConfigurationView sViewConfigurationViews[2] = { 0 };
static struct VrOpenXrSwapchain sColorSwapchains[2] = { 0 };
static int64_t sColorSwapchainFormat = 0;
static GLuint sEyeFramebuffer = 0;
static GLuint sEyeDepthRenderbuffers[2] = { 0 };
static GLuint sScaledEyeFramebuffer = 0;
static GLuint sScaledEyeColorRenderbuffers[2] = { 0 };
static GLuint sScaledEyeDepthRenderbuffers[2] = { 0 };
static uint32_t sScaledEyeWidths[2] = { 0 };
static uint32_t sScaledEyeHeights[2] = { 0 };
static bool sScaledEyeFramebufferValidated[2] = { false };
static bool sActiveEyeUsesScaledTarget = false;
static bool sEyeDirectRendered[2] = { false };
static bool sEyeImageAcquired[2] = { false };
static uint32_t sEyeDirectImageIndices[2] = { 0 };
static int32_t sActiveRenderEye = -1;
static GLint sPreviousDrawFramebuffer = 0;
static GLint sPreviousReadFramebuffer = 0;
static GLint sPreviousDrawBuffer = GL_BACK;
static GLint sPreviousReadBuffer = GL_BACK;
static bool sPreviousFramebufferStateValid = false;
static bool sDirectRenderingLogged = false;
static bool sDesktopMirrorLogged = false;
static bool sStereoEyeOffsetsLogged = false;
static bool sAsymmetricProjectionLogged = false;

static uint32_t vr_openxr_scaled_dimension(uint32_t dimension) {
    uint32_t scale = configVrRenderScale;

    if (scale < VR_RENDER_SCALE_MIN) {
        scale = VR_RENDER_SCALE_MIN;
    } else if (scale > VR_RENDER_SCALE_MAX) {
        scale = VR_RENDER_SCALE_MAX;
    }

    return (dimension * scale + 50) / 100;
}

static bool sViewPoseValid = false;
static bool sViewPoseLogged = false;
static bool sInvalidViewPoseLogged = false;
static bool sSwapchainCycleLogged = false;
static XrTime sFrameDisplayTime = 0;
static bool sFrameBegun = false;
static bool sFrameShouldRender = false;
static bool sFrameViewsLocated = false;
static bool sFrameTimingLogged = false;
static XrQuaternionf sHeadOrientationReference = { 0 };
static XrVector3f sHeadPositionReference = { 0 };
static bool sHeadOrientationReferenceValid = false;
static bool sVrEnterRecenterPending = false;
static bool sTrackingOriginRecenterPending = false;
static XrTime sTrackingOriginRecenterTime = 0;
static uint32_t sTrackingOriginGeneration = 0;
static bool sCachedTrackingValid = false;
static float sCachedEyeOffsets[2][3] = { 0 };
static float sCachedHeadRotation[4] = { 0 };
static float sCachedHeadTranslation[3] = { 0 };
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
        case XR_ERROR_SIZE_INSUFFICIENT: return "XR_ERROR_SIZE_INSUFFICIENT";
        case XR_ERROR_LIMIT_REACHED: return "XR_ERROR_LIMIT_REACHED";
        case XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED: return "XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED";
        case XR_TIMEOUT_EXPIRED: return "XR_TIMEOUT_EXPIRED";
        case XR_SESSION_LOSS_PENDING: return "XR_SESSION_LOSS_PENDING";
        case XR_SESSION_NOT_FOCUSED: return "XR_SESSION_NOT_FOCUSED";
        case XR_ERROR_PATH_UNSUPPORTED: return "XR_ERROR_PATH_UNSUPPORTED";
        case XR_ERROR_ACTION_TYPE_MISMATCH: return "XR_ERROR_ACTION_TYPE_MISMATCH";
        case XR_ERROR_NAME_DUPLICATED: return "XR_ERROR_NAME_DUPLICATED";
        case XR_ERROR_NAME_INVALID: return "XR_ERROR_NAME_INVALID";
        case XR_ERROR_ACTIONSET_NOT_ATTACHED: return "XR_ERROR_ACTIONSET_NOT_ATTACHED";
        case XR_ERROR_ACTIONSETS_ALREADY_ATTACHED: return "XR_ERROR_ACTIONSETS_ALREADY_ATTACHED";
        case XR_ERROR_LOCALIZED_NAME_DUPLICATED: return "XR_ERROR_LOCALIZED_NAME_DUPLICATED";
        case XR_ERROR_LOCALIZED_NAME_INVALID: return "XR_ERROR_LOCALIZED_NAME_INVALID";
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
    LOAD_XR_FUNCTION(xrCreateActionSpace, "xrCreateActionSpace", PFN_xrCreateActionSpace);
    LOAD_XR_FUNCTION(xrDestroySpace, "xrDestroySpace", PFN_xrDestroySpace);
    LOAD_XR_FUNCTION(xrLocateSpace, "xrLocateSpace", PFN_xrLocateSpace);
    LOAD_XR_FUNCTION(xrLocateViews, "xrLocateViews", PFN_xrLocateViews);
    LOAD_XR_FUNCTION(xrStringToPath, "xrStringToPath", PFN_xrStringToPath);
    LOAD_XR_FUNCTION(xrCreateActionSet, "xrCreateActionSet", PFN_xrCreateActionSet);
    LOAD_XR_FUNCTION(xrDestroyActionSet, "xrDestroyActionSet", PFN_xrDestroyActionSet);
    LOAD_XR_FUNCTION(xrCreateAction, "xrCreateAction", PFN_xrCreateAction);
    LOAD_XR_FUNCTION(xrSuggestInteractionProfileBindings, "xrSuggestInteractionProfileBindings", PFN_xrSuggestInteractionProfileBindings);
    LOAD_XR_FUNCTION(xrAttachSessionActionSets, "xrAttachSessionActionSets", PFN_xrAttachSessionActionSets);
    LOAD_XR_FUNCTION(xrGetActionStateBoolean, "xrGetActionStateBoolean", PFN_xrGetActionStateBoolean);
    LOAD_XR_FUNCTION(xrGetActionStateFloat, "xrGetActionStateFloat", PFN_xrGetActionStateFloat);
    LOAD_XR_FUNCTION(xrGetActionStateVector2f, "xrGetActionStateVector2f", PFN_xrGetActionStateVector2f);
    LOAD_XR_FUNCTION(xrGetActionStatePose, "xrGetActionStatePose", PFN_xrGetActionStatePose);
    LOAD_XR_FUNCTION(xrSyncActions, "xrSyncActions", PFN_xrSyncActions);
    LOAD_XR_FUNCTION(xrApplyHapticFeedback, "xrApplyHapticFeedback", PFN_xrApplyHapticFeedback);
    LOAD_XR_FUNCTION(xrStopHapticFeedback, "xrStopHapticFeedback", PFN_xrStopHapticFeedback);
    LOAD_XR_FUNCTION(xrEnumerateViewConfigurationViews, "xrEnumerateViewConfigurationViews", PFN_xrEnumerateViewConfigurationViews);
    LOAD_XR_FUNCTION(xrEnumerateSwapchainFormats, "xrEnumerateSwapchainFormats", PFN_xrEnumerateSwapchainFormats);
    LOAD_XR_FUNCTION(xrCreateSwapchain, "xrCreateSwapchain", PFN_xrCreateSwapchain);
    LOAD_XR_FUNCTION(xrDestroySwapchain, "xrDestroySwapchain", PFN_xrDestroySwapchain);
    LOAD_XR_FUNCTION(xrEnumerateSwapchainImages, "xrEnumerateSwapchainImages", PFN_xrEnumerateSwapchainImages);
    LOAD_XR_FUNCTION(xrAcquireSwapchainImage, "xrAcquireSwapchainImage", PFN_xrAcquireSwapchainImage);
    LOAD_XR_FUNCTION(xrWaitSwapchainImage, "xrWaitSwapchainImage", PFN_xrWaitSwapchainImage);
    LOAD_XR_FUNCTION(xrReleaseSwapchainImage, "xrReleaseSwapchainImage", PFN_xrReleaseSwapchainImage);

#undef LOAD_XR_FUNCTION

    return true;
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

struct VrOpenXrBindingSpec {
    XrAction action;
    const char* path;
};

static bool vr_openxr_string_to_path(
    const char* pathString,
    XrPath* path
) {
    XrResult result =
        sXr.xrStringToPath(sInstance, pathString, path);

    if (XR_FAILED(result)) {
        printf(
            "[VR] Could not create OpenXR path %s: %s (%d)\n",
            pathString,
            vr_openxr_result_name(result),
            (int)result
        );
        *path = XR_NULL_PATH;
        return false;
    }

    return true;
}

static bool vr_openxr_create_controller_action(
    XrAction* action,
    XrActionType type,
    const char* name,
    const char* localizedName
) {
    XrActionCreateInfo createInfo = { 0 };
    createInfo.type = XR_TYPE_ACTION_CREATE_INFO;
    createInfo.actionType = type;
    createInfo.countSubactionPaths = VR_CONTROLLER_COUNT;
    createInfo.subactionPaths = sControllerHandPaths;
    snprintf(
        createInfo.actionName,
        XR_MAX_ACTION_NAME_SIZE,
        "%s",
        name
    );
    snprintf(
        createInfo.localizedActionName,
        XR_MAX_LOCALIZED_ACTION_NAME_SIZE,
        "%s",
        localizedName
    );

    XrResult result = sXr.xrCreateAction(
        sControllerActionSet,
        &createInfo,
        action
    );

    if (XR_FAILED(result)) {
        printf(
            "[VR] Could not create controller action %s: %s (%d)\n",
            name,
            vr_openxr_result_name(result),
            (int)result
        );
        *action = XR_NULL_HANDLE;
        return false;
    }

    return true;
}

static void vr_openxr_suggest_controller_bindings(
    const char* profilePathString,
    const struct VrOpenXrBindingSpec* specs,
    uint32_t specCount
) {
    enum { VR_OPENXR_BINDING_COUNT_MAX = 32 };
    XrActionSuggestedBinding bindings[
        VR_OPENXR_BINDING_COUNT_MAX
    ] = { 0 };
    XrPath profilePath = XR_NULL_PATH;

    if (specCount > VR_OPENXR_BINDING_COUNT_MAX ||
        !vr_openxr_string_to_path(
            profilePathString,
            &profilePath
        )) {
        return;
    }

    uint32_t bindingCount = 0;
    for (uint32_t i = 0; i < specCount; i++) {
        XrPath bindingPath = XR_NULL_PATH;
        if (!vr_openxr_string_to_path(
                specs[i].path,
                &bindingPath
            )) {
            continue;
        }

        bindings[bindingCount].action = specs[i].action;
        bindings[bindingCount].binding = bindingPath;
        bindingCount++;
    }

    if (bindingCount == 0) {
        return;
    }

    XrInteractionProfileSuggestedBinding suggested = { 0 };
    suggested.type =
        XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
    suggested.interactionProfile = profilePath;
    suggested.countSuggestedBindings = bindingCount;
    suggested.suggestedBindings = bindings;

    XrResult result =
        sXr.xrSuggestInteractionProfileBindings(
            sInstance,
            &suggested
        );

    if (XR_FAILED(result)) {
        printf(
            "[VR] Controller profile %s was not accepted: %s (%d)\n",
            profilePathString,
            vr_openxr_result_name(result),
            (int)result
        );
    }
}

static void vr_openxr_destroy_controller_action_spaces(void) {
    if (sXr.xrDestroySpace != NULL) {
        for (uint32_t hand = 0;
             hand < VR_CONTROLLER_COUNT;
             hand++) {
            if (sControllerGripSpaces[hand] != XR_NULL_HANDLE) {
                sXr.xrDestroySpace(sControllerGripSpaces[hand]);
            }
            if (sControllerAimSpaces[hand] != XR_NULL_HANDLE) {
                sXr.xrDestroySpace(sControllerAimSpaces[hand]);
            }
        }
    }

    memset(
        sControllerGripSpaces,
        0,
        sizeof(sControllerGripSpaces)
    );
    memset(
        sControllerAimSpaces,
        0,
        sizeof(sControllerAimSpaces)
    );
    sControllerActionsAttached = false;
}

static void vr_openxr_destroy_controller_actions(void) {
    vr_openxr_destroy_controller_action_spaces();

    if (sControllerActionSet != XR_NULL_HANDLE &&
        sXr.xrDestroyActionSet != NULL) {
        sXr.xrDestroyActionSet(sControllerActionSet);
    }

    sControllerActionSet = XR_NULL_HANDLE;
    sGripPoseAction = XR_NULL_HANDLE;
    sAimPoseAction = XR_NULL_HANDLE;
    sTriggerAction = XR_NULL_HANDLE;
    sSqueezeAction = XR_NULL_HANDLE;
    sThumbstickAction = XR_NULL_HANDLE;
    sPrimaryButtonAction = XR_NULL_HANDLE;
    sSecondaryButtonAction = XR_NULL_HANDLE;
    sMenuButtonAction = XR_NULL_HANDLE;
    sThumbstickButtonAction = XR_NULL_HANDLE;
    sHapticAction = XR_NULL_HANDLE;
    memset(
        sControllerHandPaths,
        0,
        sizeof(sControllerHandPaths)
    );
    memset(sControllerStates, 0, sizeof(sControllerStates));
    memset(
        sControllerPreviousGripValid,
        0,
        sizeof(sControllerPreviousGripValid)
    );
    memset(
        sControllerPreviousGripPosition,
        0,
        sizeof(sControllerPreviousGripPosition)
    );
    memset(
        sControllerPreviousGripTime,
        0,
        sizeof(sControllerPreviousGripTime)
    );
    sControllerVelocityFallbackLogged = false;
    sControllerActionsReady = false;
    sControllerSyncErrorLogged = false;
}

static bool vr_openxr_create_controller_actions(void) {
    if (sControllerActionsReady) {
        return true;
    }

    XrActionSetCreateInfo actionSetInfo = { 0 };
    actionSetInfo.type = XR_TYPE_ACTION_SET_CREATE_INFO;
    actionSetInfo.priority = 0;
    snprintf(
        actionSetInfo.actionSetName,
        XR_MAX_ACTION_SET_NAME_SIZE,
        "%s",
        "sm64_vr_controls"
    );
    snprintf(
        actionSetInfo.localizedActionSetName,
        XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE,
        "%s",
        "SM64 VR Controls"
    );

    XrResult result = sXr.xrCreateActionSet(
        sInstance,
        &actionSetInfo,
        &sControllerActionSet
    );

    if (XR_FAILED(result)) {
        printf(
            "[VR] Could not create controller action set: %s (%d)\n",
            vr_openxr_result_name(result),
            (int)result
        );
        sControllerActionSet = XR_NULL_HANDLE;
        return false;
    }

    if (!vr_openxr_string_to_path(
            "/user/hand/left",
            &sControllerHandPaths[VR_CONTROLLER_LEFT]
        ) ||
        !vr_openxr_string_to_path(
            "/user/hand/right",
            &sControllerHandPaths[VR_CONTROLLER_RIGHT]
        ) ||
        !vr_openxr_create_controller_action(
            &sGripPoseAction,
            XR_ACTION_TYPE_POSE_INPUT,
            "grip_pose",
            "Grip Pose"
        ) ||
        !vr_openxr_create_controller_action(
            &sAimPoseAction,
            XR_ACTION_TYPE_POSE_INPUT,
            "aim_pose",
            "Aim Pose"
        ) ||
        !vr_openxr_create_controller_action(
            &sTriggerAction,
            XR_ACTION_TYPE_FLOAT_INPUT,
            "trigger",
            "Trigger"
        ) ||
        !vr_openxr_create_controller_action(
            &sSqueezeAction,
            XR_ACTION_TYPE_FLOAT_INPUT,
            "squeeze",
            "Squeeze"
        ) ||
        !vr_openxr_create_controller_action(
            &sThumbstickAction,
            XR_ACTION_TYPE_VECTOR2F_INPUT,
            "thumbstick",
            "Thumbstick"
        ) ||
        !vr_openxr_create_controller_action(
            &sPrimaryButtonAction,
            XR_ACTION_TYPE_BOOLEAN_INPUT,
            "primary_button",
            "Primary Button"
        ) ||
        !vr_openxr_create_controller_action(
            &sSecondaryButtonAction,
            XR_ACTION_TYPE_BOOLEAN_INPUT,
            "secondary_button",
            "Secondary Button"
        ) ||
        !vr_openxr_create_controller_action(
            &sMenuButtonAction,
            XR_ACTION_TYPE_BOOLEAN_INPUT,
            "menu_button",
            "Menu Button"
        ) ||
        !vr_openxr_create_controller_action(
            &sThumbstickButtonAction,
            XR_ACTION_TYPE_BOOLEAN_INPUT,
            "thumbstick_button",
            "Thumbstick Button"
        ) ||
        !vr_openxr_create_controller_action(
            &sHapticAction,
            XR_ACTION_TYPE_VIBRATION_OUTPUT,
            "haptic",
            "Haptic Feedback"
        )) {
        vr_openxr_destroy_controller_actions();
        return false;
    }

    const struct VrOpenXrBindingSpec touchBindings[] = {
        { sGripPoseAction, "/user/hand/left/input/grip/pose" },
        { sGripPoseAction, "/user/hand/right/input/grip/pose" },
        { sAimPoseAction, "/user/hand/left/input/aim/pose" },
        { sAimPoseAction, "/user/hand/right/input/aim/pose" },
        { sTriggerAction, "/user/hand/left/input/trigger/value" },
        { sTriggerAction, "/user/hand/right/input/trigger/value" },
        { sSqueezeAction, "/user/hand/left/input/squeeze/value" },
        { sSqueezeAction, "/user/hand/right/input/squeeze/value" },
        { sThumbstickAction, "/user/hand/left/input/thumbstick" },
        { sThumbstickAction, "/user/hand/right/input/thumbstick" },
        { sPrimaryButtonAction, "/user/hand/left/input/x/click" },
        { sPrimaryButtonAction, "/user/hand/right/input/a/click" },
        { sSecondaryButtonAction, "/user/hand/left/input/y/click" },
        { sSecondaryButtonAction, "/user/hand/right/input/b/click" },
        { sMenuButtonAction, "/user/hand/left/input/menu/click" },
        { sThumbstickButtonAction, "/user/hand/left/input/thumbstick/click" },
        { sThumbstickButtonAction, "/user/hand/right/input/thumbstick/click" },
        { sHapticAction, "/user/hand/left/output/haptic" },
        { sHapticAction, "/user/hand/right/output/haptic" }
    };
    vr_openxr_suggest_controller_bindings(
        "/interaction_profiles/oculus/touch_controller",
        touchBindings,
        sizeof(touchBindings) / sizeof(touchBindings[0])
    );

    const struct VrOpenXrBindingSpec indexBindings[] = {
        { sGripPoseAction, "/user/hand/left/input/grip/pose" },
        { sGripPoseAction, "/user/hand/right/input/grip/pose" },
        { sAimPoseAction, "/user/hand/left/input/aim/pose" },
        { sAimPoseAction, "/user/hand/right/input/aim/pose" },
        { sTriggerAction, "/user/hand/left/input/trigger/value" },
        { sTriggerAction, "/user/hand/right/input/trigger/value" },
        { sSqueezeAction, "/user/hand/left/input/squeeze/value" },
        { sSqueezeAction, "/user/hand/right/input/squeeze/value" },
        { sThumbstickAction, "/user/hand/left/input/thumbstick" },
        { sThumbstickAction, "/user/hand/right/input/thumbstick" },
        { sPrimaryButtonAction, "/user/hand/left/input/a/click" },
        { sPrimaryButtonAction, "/user/hand/right/input/a/click" },
        { sSecondaryButtonAction, "/user/hand/left/input/b/click" },
        { sSecondaryButtonAction, "/user/hand/right/input/b/click" },
        { sMenuButtonAction, "/user/hand/left/input/b/click" },
        { sMenuButtonAction, "/user/hand/right/input/b/click" },
        { sThumbstickButtonAction, "/user/hand/left/input/thumbstick/click" },
        { sThumbstickButtonAction, "/user/hand/right/input/thumbstick/click" },
        { sHapticAction, "/user/hand/left/output/haptic" },
        { sHapticAction, "/user/hand/right/output/haptic" }
    };
    vr_openxr_suggest_controller_bindings(
        "/interaction_profiles/valve/index_controller",
        indexBindings,
        sizeof(indexBindings) / sizeof(indexBindings[0])
    );

    const struct VrOpenXrBindingSpec viveBindings[] = {
        { sGripPoseAction, "/user/hand/left/input/grip/pose" },
        { sGripPoseAction, "/user/hand/right/input/grip/pose" },
        { sAimPoseAction, "/user/hand/left/input/aim/pose" },
        { sAimPoseAction, "/user/hand/right/input/aim/pose" },
        { sTriggerAction, "/user/hand/left/input/trigger/value" },
        { sTriggerAction, "/user/hand/right/input/trigger/value" },
        { sSqueezeAction, "/user/hand/left/input/squeeze/click" },
        { sSqueezeAction, "/user/hand/right/input/squeeze/click" },
        { sThumbstickAction, "/user/hand/left/input/trackpad" },
        { sThumbstickAction, "/user/hand/right/input/trackpad" },
        { sPrimaryButtonAction, "/user/hand/left/input/trackpad/click" },
        { sPrimaryButtonAction, "/user/hand/right/input/trackpad/click" },
        { sSecondaryButtonAction, "/user/hand/left/input/menu/click" },
        { sSecondaryButtonAction, "/user/hand/right/input/menu/click" },
        { sMenuButtonAction, "/user/hand/left/input/menu/click" },
        { sMenuButtonAction, "/user/hand/right/input/menu/click" },
        { sThumbstickButtonAction, "/user/hand/left/input/trackpad/click" },
        { sThumbstickButtonAction, "/user/hand/right/input/trackpad/click" },
        { sHapticAction, "/user/hand/left/output/haptic" },
        { sHapticAction, "/user/hand/right/output/haptic" }
    };
    vr_openxr_suggest_controller_bindings(
        "/interaction_profiles/htc/vive_controller",
        viveBindings,
        sizeof(viveBindings) / sizeof(viveBindings[0])
    );

    const struct VrOpenXrBindingSpec mixedRealityBindings[] = {
        { sGripPoseAction, "/user/hand/left/input/grip/pose" },
        { sGripPoseAction, "/user/hand/right/input/grip/pose" },
        { sAimPoseAction, "/user/hand/left/input/aim/pose" },
        { sAimPoseAction, "/user/hand/right/input/aim/pose" },
        { sTriggerAction, "/user/hand/left/input/trigger/value" },
        { sTriggerAction, "/user/hand/right/input/trigger/value" },
        { sSqueezeAction, "/user/hand/left/input/squeeze/click" },
        { sSqueezeAction, "/user/hand/right/input/squeeze/click" },
        { sThumbstickAction, "/user/hand/left/input/thumbstick" },
        { sThumbstickAction, "/user/hand/right/input/thumbstick" },
        { sPrimaryButtonAction, "/user/hand/left/input/trackpad/click" },
        { sPrimaryButtonAction, "/user/hand/right/input/trackpad/click" },
        { sSecondaryButtonAction, "/user/hand/left/input/squeeze/click" },
        { sSecondaryButtonAction, "/user/hand/right/input/squeeze/click" },
        { sMenuButtonAction, "/user/hand/left/input/menu/click" },
        { sMenuButtonAction, "/user/hand/right/input/menu/click" },
        { sThumbstickButtonAction, "/user/hand/left/input/thumbstick/click" },
        { sThumbstickButtonAction, "/user/hand/right/input/thumbstick/click" },
        { sHapticAction, "/user/hand/left/output/haptic" },
        { sHapticAction, "/user/hand/right/output/haptic" }
    };
    vr_openxr_suggest_controller_bindings(
        "/interaction_profiles/microsoft/motion_controller",
        mixedRealityBindings,
        sizeof(mixedRealityBindings) /
            sizeof(mixedRealityBindings[0])
    );
    vr_openxr_suggest_controller_bindings(
        "/interaction_profiles/samsung/odyssey_controller",
        mixedRealityBindings,
        sizeof(mixedRealityBindings) /
            sizeof(mixedRealityBindings[0])
    );

    const struct VrOpenXrBindingSpec simpleBindings[] = {
        { sGripPoseAction, "/user/hand/left/input/grip/pose" },
        { sGripPoseAction, "/user/hand/right/input/grip/pose" },
        { sAimPoseAction, "/user/hand/left/input/aim/pose" },
        { sAimPoseAction, "/user/hand/right/input/aim/pose" },
        { sPrimaryButtonAction, "/user/hand/left/input/select/click" },
        { sPrimaryButtonAction, "/user/hand/right/input/select/click" },
        { sMenuButtonAction, "/user/hand/left/input/menu/click" },
        { sMenuButtonAction, "/user/hand/right/input/menu/click" },
        { sHapticAction, "/user/hand/left/output/haptic" },
        { sHapticAction, "/user/hand/right/output/haptic" }
    };
    vr_openxr_suggest_controller_bindings(
        "/interaction_profiles/khr/simple_controller",
        simpleBindings,
        sizeof(simpleBindings) / sizeof(simpleBindings[0])
    );

    sControllerActionsReady = true;
    printf("[VR] OpenXR motion-controller action set is ready.\n");
    return true;
}

static bool vr_openxr_attach_controller_actions(void) {
    if (!sControllerActionsReady) {
        return false;
    }

    XrSessionActionSetsAttachInfo attachInfo = { 0 };
    attachInfo.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
    attachInfo.countActionSets = 1;
    attachInfo.actionSets = &sControllerActionSet;

    XrResult result = sXr.xrAttachSessionActionSets(
        sSession,
        &attachInfo
    );

    if (XR_FAILED(result)) {
        printf(
            "[VR] Could not attach controller actions: %s (%d)\n",
            vr_openxr_result_name(result),
            (int)result
        );
        return false;
    }

    for (uint32_t hand = 0;
         hand < VR_CONTROLLER_COUNT;
         hand++) {
        XrActionSpaceCreateInfo spaceInfo = { 0 };
        spaceInfo.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
        spaceInfo.subactionPath = sControllerHandPaths[hand];
        spaceInfo.poseInActionSpace.orientation.w = 1.0f;

        spaceInfo.action = sGripPoseAction;
        result = sXr.xrCreateActionSpace(
            sSession,
            &spaceInfo,
            &sControllerGripSpaces[hand]
        );
        if (XR_FAILED(result)) {
            printf(
                "[VR] Could not create %s grip action space: %s (%d)\n",
                hand == VR_CONTROLLER_LEFT ? "left" : "right",
                vr_openxr_result_name(result),
                (int)result
            );
            vr_openxr_destroy_controller_action_spaces();
            return false;
        }

        spaceInfo.action = sAimPoseAction;
        result = sXr.xrCreateActionSpace(
            sSession,
            &spaceInfo,
            &sControllerAimSpaces[hand]
        );
        if (XR_FAILED(result)) {
            printf(
                "[VR] Could not create %s aim action space: %s (%d)\n",
                hand == VR_CONTROLLER_LEFT ? "left" : "right",
                vr_openxr_result_name(result),
                (int)result
            );
            vr_openxr_destroy_controller_action_spaces();
            return false;
        }
    }

    sControllerActionsAttached = true;
    printf("[VR] OpenXR motion-controller actions attached.\n");
    return true;
}

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

static const char* vr_openxr_color_format_name(int64_t format) {
    if (format == VR_GL_SRGB8_ALPHA8) {
        return "GL_SRGB8_ALPHA8";
    }

    if (format == VR_GL_RGBA8) {
        return "GL_RGBA8";
    }

    return "UNKNOWN_OPENGL_FORMAT";
}

static void vr_openxr_destroy_eye_framebuffer(void) {
    bool hadFramebuffer = sEyeFramebuffer != 0;

    if (sEyeFramebuffer != 0) {
        glDeleteFramebuffers(1, &sEyeFramebuffer);
        sEyeFramebuffer = 0;
    }

    if (sEyeDepthRenderbuffers[0] != 0 ||
        sEyeDepthRenderbuffers[1] != 0) {
        glDeleteRenderbuffers(2, sEyeDepthRenderbuffers);
    }
    memset(
        sEyeDepthRenderbuffers,
        0,
        sizeof(sEyeDepthRenderbuffers)
    );

    if (sScaledEyeFramebuffer != 0) {
        glDeleteFramebuffers(1, &sScaledEyeFramebuffer);
        sScaledEyeFramebuffer = 0;
    }

    if (sScaledEyeColorRenderbuffers[0] != 0 ||
        sScaledEyeColorRenderbuffers[1] != 0) {
        glDeleteRenderbuffers(2, sScaledEyeColorRenderbuffers);
    }
    if (sScaledEyeDepthRenderbuffers[0] != 0 ||
        sScaledEyeDepthRenderbuffers[1] != 0) {
        glDeleteRenderbuffers(2, sScaledEyeDepthRenderbuffers);
    }
    memset(
        sScaledEyeColorRenderbuffers,
        0,
        sizeof(sScaledEyeColorRenderbuffers)
    );
    memset(
        sScaledEyeDepthRenderbuffers,
        0,
        sizeof(sScaledEyeDepthRenderbuffers)
    );
    memset(sScaledEyeWidths, 0, sizeof(sScaledEyeWidths));
    memset(sScaledEyeHeights, 0, sizeof(sScaledEyeHeights));
    memset(
        sScaledEyeFramebufferValidated,
        0,
        sizeof(sScaledEyeFramebufferValidated)
    );
    sActiveEyeUsesScaledTarget = false;

    if (hadFramebuffer) {
        printf("[VR] OpenXR eye framebuffer destroyed.\n");
    }
}

static void vr_openxr_destroy_color_swapchains(void) {
    bool hadSwapchain = false;

    for (uint32_t i = 0; i < 2; i++) {
        struct VrOpenXrSwapchain* swapchain =
            &sColorSwapchains[i];

        if (swapchain->handle != XR_NULL_HANDLE) {
            hadSwapchain = true;

            if (sXr.xrDestroySwapchain != NULL) {
                sXr.xrDestroySwapchain(swapchain->handle);
            }
        }

        free(swapchain->images);
        free(swapchain->framebufferValidated);
        memset(swapchain, 0, sizeof(*swapchain));
    }

    sColorSwapchainFormat = 0;

    if (hadSwapchain) {
        printf("[VR] OpenXR color swapchains destroyed.\n");
    }
}

static bool vr_openxr_choose_color_swapchain_format(void) {
    uint32_t formatCount = 0;

    XrResult result =
        sXr.xrEnumerateSwapchainFormats(
            sSession,
            0,
            &formatCount,
            NULL
        );

    if (XR_FAILED(result) || formatCount == 0) {
        printf(
            "[VR] Could not enumerate swapchain formats: %s (%d)\n",
            vr_openxr_result_name(result),
            (int)result
        );
        return false;
    }

    int64_t* formats = calloc(formatCount, sizeof(int64_t));

    if (formats == NULL) {
        printf("[VR] Could not allocate swapchain format list.\n");
        return false;
    }

    result =
        sXr.xrEnumerateSwapchainFormats(
            sSession,
            formatCount,
            &formatCount,
            formats
        );

    if (XR_FAILED(result)) {
        printf(
            "[VR] Could not read swapchain formats: %s (%d)\n",
            vr_openxr_result_name(result),
            (int)result
        );
        free(formats);
        return false;
    }

    const int64_t preferredFormats[] = {
        VR_GL_SRGB8_ALPHA8,
        VR_GL_RGBA8
    };

    bool foundFormat = false;

    for (uint32_t preferred = 0;
         preferred < sizeof(preferredFormats) / sizeof(preferredFormats[0]);
         preferred++) {
        for (uint32_t available = 0;
             available < formatCount;
             available++) {
            if (preferredFormats[preferred] == formats[available]) {
                sColorSwapchainFormat = preferredFormats[preferred];
                foundFormat = true;
                break;
            }
        }

        if (foundFormat) {
            break;
        }
    }

    free(formats);

    if (!foundFormat) {
        printf(
            "[VR] Runtime supports neither GL_SRGB8_ALPHA8 "
            "nor GL_RGBA8 swapchains.\n"
        );
        return false;
    }

    printf(
        "[VR] OpenXR color format selected: %s (0x%llX).\n",
        vr_openxr_color_format_name(sColorSwapchainFormat),
        (unsigned long long)sColorSwapchainFormat
    );

    return true;
}

static bool vr_openxr_create_eye_color_swapchain(uint32_t eyeIndex) {
    struct VrOpenXrSwapchain* swapchain =
        &sColorSwapchains[eyeIndex];
    const XrViewConfigurationView* view =
        &sViewConfigurationViews[eyeIndex];
    const char* eyeName = eyeIndex == 0 ? "left" : "right";

    if (view->recommendedImageRectWidth == 0 ||
        view->recommendedImageRectHeight == 0 ||
        view->recommendedSwapchainSampleCount == 0) {
        printf("[VR] %s eye reported an invalid target size.\n", eyeName);
        return false;
    }

    XrSwapchainCreateInfo createInfo = { 0 };
    createInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
    createInfo.createFlags = 0;
    createInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.format = sColorSwapchainFormat;
    createInfo.sampleCount =
        view->recommendedSwapchainSampleCount;
    createInfo.width = view->recommendedImageRectWidth;
    createInfo.height = view->recommendedImageRectHeight;
    createInfo.faceCount = 1;
    createInfo.arraySize = 1;
    createInfo.mipCount = 1;

    XrResult result =
        sXr.xrCreateSwapchain(
            sSession,
            &createInfo,
            &swapchain->handle
        );

    if (XR_FAILED(result)) {
        printf(
            "[VR] Could not create %s eye color swapchain: %s (%d)\n",
            eyeName,
            vr_openxr_result_name(result),
            (int)result
        );
        swapchain->handle = XR_NULL_HANDLE;
        return false;
    }

    swapchain->width = createInfo.width;
    swapchain->height = createInfo.height;

    uint32_t imageCount = 0;
    result =
        sXr.xrEnumerateSwapchainImages(
            swapchain->handle,
            0,
            &imageCount,
            NULL
        );

    if (XR_FAILED(result) || imageCount == 0) {
        printf(
            "[VR] Could not enumerate %s eye swapchain images: "
            "%s (%d)\n",
            eyeName,
            vr_openxr_result_name(result),
            (int)result
        );
        return false;
    }

    swapchain->images =
        calloc(imageCount, sizeof(XrSwapchainImageOpenGLKHR));
    swapchain->framebufferValidated =
        calloc(imageCount, sizeof(bool));

    if (swapchain->images == NULL ||
        swapchain->framebufferValidated == NULL) {
        printf(
            "[VR] Could not allocate %s eye swapchain image list.\n",
            eyeName
        );
        free(swapchain->images);
        free(swapchain->framebufferValidated);
        swapchain->images = NULL;
        swapchain->framebufferValidated = NULL;
        return false;
    }

    for (uint32_t i = 0; i < imageCount; i++) {
        swapchain->images[i].type =
            XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
        swapchain->images[i].next = NULL;
    }

    result =
        sXr.xrEnumerateSwapchainImages(
            swapchain->handle,
            imageCount,
            &swapchain->imageCount,
            (XrSwapchainImageBaseHeader*)swapchain->images
        );

    if (XR_FAILED(result)) {
        printf(
            "[VR] Could not read %s eye swapchain images: %s (%d)\n",
            eyeName,
            vr_openxr_result_name(result),
            (int)result
        );
        return false;
    }

    if (swapchain->imageCount == 0) {
        printf(
            "[VR] %s eye swapchain returned no OpenGL images.\n",
            eyeName
        );
        return false;
    }

    printf(
        "[VR] %s eye color swapchain: %ux%u at %u sample(s), "
        "%u OpenGL image(s).\n",
        eyeName,
        swapchain->width,
        swapchain->height,
        view->recommendedSwapchainSampleCount,
        swapchain->imageCount
    );

    return true;
}

static bool vr_openxr_create_color_swapchains(void) {
    if (!vr_openxr_choose_color_swapchain_format()) {
        return false;
    }

    for (uint32_t eye = 0; eye < 2; eye++) {
        if (!vr_openxr_create_eye_color_swapchain(eye)) {
            vr_openxr_destroy_color_swapchains();
            return false;
        }
    }

    printf("[VR] OpenXR stereo color swapchains are ready.\n");
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

    if (!vr_openxr_create_controller_actions()) {
        printf(
            "[VR] Motion-controller actions are unavailable; "
            "gamepad VR will remain active.\n"
        );
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

    if (!vr_openxr_create_color_swapchains()) {
        sXr.xrDestroySpace(sViewSpace);
        sViewSpace = XR_NULL_HANDLE;
        sXr.xrDestroySession(sSession);
        sSession = XR_NULL_HANDLE;
        return false;
    }

    if (sControllerActionsReady &&
        !vr_openxr_attach_controller_actions()) {
        printf(
            "[VR] Motion-controller tracking is unavailable; "
            "gamepad VR will remain active.\n"
        );
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
        } else if (event.type ==
                   XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
            const XrEventDataReferenceSpaceChangePending* spaceEvent =
                (const XrEventDataReferenceSpaceChangePending*)&event;
            if (spaceEvent->session == sSession &&
                spaceEvent->referenceSpaceType ==
                    XR_REFERENCE_SPACE_TYPE_LOCAL) {
                sTrackingOriginRecenterPending = true;
                sTrackingOriginRecenterTime = spaceEvent->changeTime;
                printf(
                    "[VR] OpenXR tracking-origin recenter pending.\n"
                );
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
        if (sViewPoseValid || !sInvalidViewPoseLogged) {
            printf("[VR] OpenXR view pose is temporarily invalid.\n");
            sInvalidViewPoseLogged = true;
        }
        sViewPoseValid = false;
        return true;
    }

    if (!sViewPoseValid) {
        printf("[VR] Valid stereo 6DoF view poses acquired.\n");
        if (!sViewPoseLogged) {
            vr_openxr_log_views();
            sViewPoseLogged = true;
        }
    }

    sInvalidViewPoseLogged = false;
    sViewPoseValid = true;
    return true;
}

static void vr_openxr_rotate_vector(
    const XrQuaternionf* rotation,
    const XrVector3f* vector,
    float result[3]
) {
    const float twiceCrossX = 2.0f * (
        rotation->y * vector->z -
        rotation->z * vector->y
    );
    const float twiceCrossY = 2.0f * (
        rotation->z * vector->x -
        rotation->x * vector->z
    );
    const float twiceCrossZ = 2.0f * (
        rotation->x * vector->y -
        rotation->y * vector->x
    );

    result[0] = vector->x +
        rotation->w * twiceCrossX +
        rotation->y * twiceCrossZ -
        rotation->z * twiceCrossY;
    result[1] = vector->y +
        rotation->w * twiceCrossY +
        rotation->z * twiceCrossX -
        rotation->x * twiceCrossZ;
    result[2] = vector->z +
        rotation->w * twiceCrossZ +
        rotation->x * twiceCrossY -
        rotation->y * twiceCrossX;
}

static XrQuaternionf vr_openxr_yaw_only_orientation(
    const XrQuaternionf* orientation
) {
    // A tracking-origin recenter establishes horizontal forward only. Using
    // the full headset quaternion here made a tilted head redefine world-up,
    // leaving the entire view permanently rolled or pitched after recenter.
    const float yaw = atan2f(
        2.0f * (
            orientation->w * orientation->y +
            orientation->x * orientation->z
        ),
        1.0f - 2.0f * (
            orientation->x * orientation->x +
            orientation->y * orientation->y
        )
    );
    const float halfYaw = yaw * 0.5f;
    const XrQuaternionf reference = {
        0.0f,
        sinf(halfYaw),
        0.0f,
        cosf(halfYaw)
    };
    return reference;
}

static void vr_openxr_update_cached_tracking(void) {
    sCachedTrackingValid = false;

    if (!sFrameViewsLocated ||
        !sViewPoseValid ||
        !sHeadOrientationReferenceValid) {
        return;
    }

    const XrVector3f center = {
        (sViews[0].pose.position.x +
         sViews[1].pose.position.x) * 0.5f,
        (sViews[0].pose.position.y +
         sViews[1].pose.position.y) * 0.5f,
        (sViews[0].pose.position.z +
         sViews[1].pose.position.z) * 0.5f
    };
    const XrQuaternionf viewOrientationInverse = {
        -sViews[0].pose.orientation.x,
        -sViews[0].pose.orientation.y,
        -sViews[0].pose.orientation.z,
        sViews[0].pose.orientation.w
    };

    for (uint32_t eye = 0; eye < 2; eye++) {
        const XrVector3f eyeDelta = {
            sViews[eye].pose.position.x - center.x,
            sViews[eye].pose.position.y - center.y,
            sViews[eye].pose.position.z - center.z
        };
        vr_openxr_rotate_vector(
            &viewOrientationInverse,
            &eyeDelta,
            sCachedEyeOffsets[eye]
        );
    }

    const XrQuaternionf referenceInverse = {
        -sHeadOrientationReference.x,
        -sHeadOrientationReference.y,
        -sHeadOrientationReference.z,
        sHeadOrientationReference.w
    };
    const XrQuaternionf current = sViews[0].pose.orientation;
    const XrQuaternionf relative = {
        referenceInverse.w * current.x +
            referenceInverse.x * current.w +
            referenceInverse.y * current.z -
            referenceInverse.z * current.y,
        referenceInverse.w * current.y -
            referenceInverse.x * current.z +
            referenceInverse.y * current.w +
            referenceInverse.z * current.x,
        referenceInverse.w * current.z +
            referenceInverse.x * current.y -
            referenceInverse.y * current.x +
            referenceInverse.z * current.w,
        referenceInverse.w * current.w -
            referenceInverse.x * current.x -
            referenceInverse.y * current.y -
            referenceInverse.z * current.z
    };
    const float rotationLength = sqrtf(
        relative.x * relative.x +
        relative.y * relative.y +
        relative.z * relative.z +
        relative.w * relative.w
    );

    if (rotationLength <= 0.000001f) {
        return;
    }

    sCachedHeadRotation[0] = relative.x / rotationLength;
    sCachedHeadRotation[1] = relative.y / rotationLength;
    sCachedHeadRotation[2] = relative.z / rotationLength;
    sCachedHeadRotation[3] = relative.w / rotationLength;

    const XrVector3f headDelta = {
        center.x - sHeadPositionReference.x,
        center.y - sHeadPositionReference.y,
        center.z - sHeadPositionReference.z
    };
    vr_openxr_rotate_vector(
        &referenceInverse,
        &headDelta,
        sCachedHeadTranslation
    );
    sCachedTrackingValid = true;
}

static bool vr_openxr_controller_pose_is_active(
    XrAction action,
    XrPath handPath
) {
    XrActionStateGetInfo getInfo = { 0 };
    getInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
    getInfo.action = action;
    getInfo.subactionPath = handPath;

    XrActionStatePose state = { 0 };
    state.type = XR_TYPE_ACTION_STATE_POSE;

    XrResult result = sXr.xrGetActionStatePose(
        sSession,
        &getInfo,
        &state
    );
    return XR_SUCCEEDED(result) && state.isActive;
}

static bool vr_openxr_locate_controller_pose(
    XrSpace space,
    XrTime displayTime,
    float position[3],
    float rotation[4],
    bool* linearVelocityValid,
    float linearVelocity[3],
    bool* angularVelocityValid,
    float angularVelocity[3]
) {
    if (linearVelocityValid != NULL) {
        *linearVelocityValid = false;
    }
    if (angularVelocityValid != NULL) {
        *angularVelocityValid = false;
    }
    if (space == XR_NULL_HANDLE ||
        !sHeadOrientationReferenceValid) {
        return false;
    }

    XrSpaceVelocity velocity = { 0 };
    velocity.type = XR_TYPE_SPACE_VELOCITY;

    XrSpaceLocation location = { 0 };
    location.type = XR_TYPE_SPACE_LOCATION;
    location.next = &velocity;

    XrResult result = sXr.xrLocateSpace(
        space,
        sViewSpace,
        displayTime,
        &location
    );
    const XrSpaceLocationFlags requiredFlags =
        XR_SPACE_LOCATION_POSITION_VALID_BIT |
        XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;

    if (XR_FAILED(result) ||
        (location.locationFlags & requiredFlags) != requiredFlags) {
        return false;
    }

    const XrQuaternionf referenceInverse = {
        -sHeadOrientationReference.x,
        -sHeadOrientationReference.y,
        -sHeadOrientationReference.z,
        sHeadOrientationReference.w
    };
    const XrQuaternionf current = location.pose.orientation;
    const XrQuaternionf relative = {
        referenceInverse.w * current.x +
            referenceInverse.x * current.w +
            referenceInverse.y * current.z -
            referenceInverse.z * current.y,
        referenceInverse.w * current.y -
            referenceInverse.x * current.z +
            referenceInverse.y * current.w +
            referenceInverse.z * current.x,
        referenceInverse.w * current.z +
            referenceInverse.x * current.y -
            referenceInverse.y * current.x +
            referenceInverse.z * current.w,
        referenceInverse.w * current.w -
            referenceInverse.x * current.x -
            referenceInverse.y * current.y -
            referenceInverse.z * current.z
    };
    const float rotationLength = sqrtf(
        relative.x * relative.x +
        relative.y * relative.y +
        relative.z * relative.z +
        relative.w * relative.w
    );

    if (rotationLength <= 0.000001f) {
        return false;
    }

    rotation[0] = relative.x / rotationLength;
    rotation[1] = relative.y / rotationLength;
    rotation[2] = relative.z / rotationLength;
    rotation[3] = relative.w / rotationLength;

    const XrVector3f positionDelta = {
        location.pose.position.x - sHeadPositionReference.x,
        location.pose.position.y - sHeadPositionReference.y,
        location.pose.position.z - sHeadPositionReference.z
    };
    vr_openxr_rotate_vector(
        &referenceInverse,
        &positionDelta,
        position
    );

    if (linearVelocityValid != NULL &&
        linearVelocity != NULL &&
        (velocity.velocityFlags &
         XR_SPACE_VELOCITY_LINEAR_VALID_BIT) != 0) {
        vr_openxr_rotate_vector(
            &referenceInverse,
            &velocity.linearVelocity,
            linearVelocity
        );
        *linearVelocityValid = true;
    }
    if (angularVelocityValid != NULL &&
        angularVelocity != NULL &&
        (velocity.velocityFlags &
         XR_SPACE_VELOCITY_ANGULAR_VALID_BIT) != 0) {
        vr_openxr_rotate_vector(
            &referenceInverse,
            &velocity.angularVelocity,
            angularVelocity
        );
        *angularVelocityValid = true;
    }
    return true;
}

static float vr_openxr_get_float_action(
    XrAction action,
    XrPath handPath
) {
    XrActionStateGetInfo getInfo = { 0 };
    getInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
    getInfo.action = action;
    getInfo.subactionPath = handPath;

    XrActionStateFloat state = { 0 };
    state.type = XR_TYPE_ACTION_STATE_FLOAT;
    XrResult result = sXr.xrGetActionStateFloat(
        sSession,
        &getInfo,
        &state
    );

    return XR_SUCCEEDED(result) && state.isActive
        ? state.currentState
        : 0.0f;
}

static bool vr_openxr_get_boolean_action(
    XrAction action,
    XrPath handPath
) {
    XrActionStateGetInfo getInfo = { 0 };
    getInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
    getInfo.action = action;
    getInfo.subactionPath = handPath;

    XrActionStateBoolean state = { 0 };
    state.type = XR_TYPE_ACTION_STATE_BOOLEAN;
    XrResult result = sXr.xrGetActionStateBoolean(
        sSession,
        &getInfo,
        &state
    );

    return XR_SUCCEEDED(result) &&
        state.isActive &&
        state.currentState;
}

static void vr_openxr_get_vector_action(
    XrAction action,
    XrPath handPath,
    float value[2]
) {
    XrActionStateGetInfo getInfo = { 0 };
    getInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
    getInfo.action = action;
    getInfo.subactionPath = handPath;

    XrActionStateVector2f state = { 0 };
    state.type = XR_TYPE_ACTION_STATE_VECTOR2F;
    XrResult result = sXr.xrGetActionStateVector2f(
        sSession,
        &getInfo,
        &state
    );

    if (XR_SUCCEEDED(result) && state.isActive) {
        value[0] = state.currentState.x;
        value[1] = state.currentState.y;
    } else {
        value[0] = 0.0f;
        value[1] = 0.0f;
    }
}

static void vr_openxr_apply_controller_velocity_fallback(
    uint32_t hand,
    XrTime displayTime
) {
    if (hand >= VR_CONTROLLER_COUNT) {
        return;
    }

    struct VrControllerState* state = &sControllerStates[hand];
    if (!state->gripPoseValid) {
        sControllerPreviousGripValid[hand] = false;
        sControllerPreviousGripTime[hand] = 0;
        return;
    }

    if (!state->gripLinearVelocityValid &&
        sControllerPreviousGripValid[hand] &&
        displayTime > sControllerPreviousGripTime[hand]) {
        const double deltaSeconds =
            (double)(displayTime -
                     sControllerPreviousGripTime[hand]) /
            1000000000.0;

        // Reject stale samples after a pause or tracking loss. Normal OpenXR
        // frame intervals are comfortably inside this range.
        if (deltaSeconds >= 0.001 && deltaSeconds <= 0.100) {
            for (uint32_t axis = 0; axis < 3; axis++) {
                state->gripLinearVelocity[axis] =
                    (state->gripPosition[axis] -
                     sControllerPreviousGripPosition[hand][axis]) /
                    (float)deltaSeconds;
            }
            state->gripLinearVelocityValid = true;

            if (!sControllerVelocityFallbackLogged) {
                printf(
                    "[VR] Runtime controller velocity unavailable; "
                    "using tracked-pose fallback.\n"
                );
                sControllerVelocityFallbackLogged = true;
            }
        }
    }

    memcpy(
        sControllerPreviousGripPosition[hand],
        state->gripPosition,
        sizeof(sControllerPreviousGripPosition[hand])
    );
    sControllerPreviousGripTime[hand] = displayTime;
    sControllerPreviousGripValid[hand] = true;
}

static void vr_openxr_update_controller_states(
    XrTime displayTime
) {
    if (!sControllerActionsAttached ||
        !sSessionRunning ||
        displayTime == 0) {
        return;
    }

    XrActiveActionSet activeActionSet = {
        sControllerActionSet,
        XR_NULL_PATH
    };
    XrActionsSyncInfo syncInfo = { 0 };
    syncInfo.type = XR_TYPE_ACTIONS_SYNC_INFO;
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &activeActionSet;

    XrResult result = sXr.xrSyncActions(sSession, &syncInfo);
    if (result == XR_SESSION_NOT_FOCUSED) {
        memset(sControllerStates, 0, sizeof(sControllerStates));
        memset(
            sControllerPreviousGripValid,
            0,
            sizeof(sControllerPreviousGripValid)
        );
        return;
    }
    if (XR_FAILED(result)) {
        if (!sControllerSyncErrorLogged) {
            printf(
                "[VR] Could not synchronize controller actions: %s (%d)\n",
                vr_openxr_result_name(result),
                (int)result
            );
            sControllerSyncErrorLogged = true;
        }
        memset(sControllerStates, 0, sizeof(sControllerStates));
        memset(
            sControllerPreviousGripValid,
            0,
            sizeof(sControllerPreviousGripValid)
        );
        return;
    }

    sControllerSyncErrorLogged = false;
    for (uint32_t hand = 0;
         hand < VR_CONTROLLER_COUNT;
         hand++) {
        const bool wasTracked =
            sControllerStates[hand].gripPoseValid ||
            sControllerStates[hand].aimPoseValid;
        memset(
            &sControllerStates[hand],
            0,
            sizeof(sControllerStates[hand])
        );

        if (vr_openxr_controller_pose_is_active(
                sGripPoseAction,
                sControllerHandPaths[hand]
            )) {
            sControllerStates[hand].gripPoseValid =
                vr_openxr_locate_controller_pose(
                    sControllerGripSpaces[hand],
                    displayTime,
                    sControllerStates[hand].gripPosition,
                    sControllerStates[hand].gripRotation,
                    &sControllerStates[hand].gripLinearVelocityValid,
                    sControllerStates[hand].gripLinearVelocity,
                    &sControllerStates[hand].gripAngularVelocityValid,
                    sControllerStates[hand].gripAngularVelocity
                );
        }

        vr_openxr_apply_controller_velocity_fallback(
            hand,
            displayTime
        );

        if (vr_openxr_controller_pose_is_active(
                sAimPoseAction,
                sControllerHandPaths[hand]
            )) {
            sControllerStates[hand].aimPoseValid =
                vr_openxr_locate_controller_pose(
                    sControllerAimSpaces[hand],
                    displayTime,
                    sControllerStates[hand].aimPosition,
                    sControllerStates[hand].aimRotation,
                    NULL,
                    NULL,
                    NULL,
                    NULL
                );
        }

        sControllerStates[hand].trigger =
            vr_openxr_get_float_action(
                sTriggerAction,
                sControllerHandPaths[hand]
            );
        sControllerStates[hand].squeeze =
            vr_openxr_get_float_action(
                sSqueezeAction,
                sControllerHandPaths[hand]
            );
        vr_openxr_get_vector_action(
            sThumbstickAction,
            sControllerHandPaths[hand],
            sControllerStates[hand].thumbstick
        );
        sControllerStates[hand].primaryButton =
            vr_openxr_get_boolean_action(
                sPrimaryButtonAction,
                sControllerHandPaths[hand]
            );
        sControllerStates[hand].secondaryButton =
            vr_openxr_get_boolean_action(
                sSecondaryButtonAction,
                sControllerHandPaths[hand]
            );
        sControllerStates[hand].menuButton =
            vr_openxr_get_boolean_action(
                sMenuButtonAction,
                sControllerHandPaths[hand]
            );
        sControllerStates[hand].thumbstickButton =
            vr_openxr_get_boolean_action(
                sThumbstickButtonAction,
                sControllerHandPaths[hand]
            );

        const bool isTracked =
            sControllerStates[hand].gripPoseValid ||
            sControllerStates[hand].aimPoseValid;
        if (isTracked != wasTracked) {
            printf(
                "[VR] %s motion controller tracking %s.\n",
                hand == VR_CONTROLLER_LEFT ? "Left" : "Right",
                isTracked ? "acquired" : "lost"
            );
        }
    }
}

static void vr_openxr_blit_game_frame(
    const struct VrOpenXrSwapchain* swapchain,
    const GLint sourceViewport[4]
) {
    const int sourceWidth = sourceViewport[2];
    const int sourceHeight = sourceViewport[3];
    int sourceX = sourceViewport[0];
    int sourceY = sourceViewport[1];
    int croppedSourceWidth = sourceWidth;
    int croppedSourceHeight = sourceHeight;
    const float sourceAspect =
        (float)sourceWidth / (float)sourceHeight;
    const float destinationAspect =
        (float)swapchain->width / (float)swapchain->height;

    if (sourceAspect > destinationAspect) {
        croppedSourceWidth =
            (int)((float)sourceHeight * destinationAspect);
        sourceX += (sourceWidth - croppedSourceWidth) / 2;
    } else {
        croppedSourceHeight =
            (int)((float)sourceWidth / destinationAspect);
        sourceY += (sourceHeight - croppedSourceHeight) / 2;
    }

    glViewport(
        0,
        0,
        (GLsizei)swapchain->width,
        (GLsizei)swapchain->height
    );
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBlitFramebuffer(
        sourceX,
        sourceY,
        sourceX + croppedSourceWidth,
        sourceY + croppedSourceHeight,
        0,
        0,
        (GLint)swapchain->width,
        (GLint)swapchain->height,
        GL_COLOR_BUFFER_BIT,
        GL_LINEAR
    );
    glFlush();
}

static bool vr_openxr_copy_game_frame_to_eye(
    uint32_t eyeIndex,
    uint32_t imageIndex
) {
    const char* eyeName = eyeIndex == 0 ? "left" : "right";
    const struct VrOpenXrSwapchain* swapchain =
        &sColorSwapchains[eyeIndex];
    const XrViewConfigurationView* view =
        &sViewConfigurationViews[eyeIndex];

    if (sEyeFramebuffer == 0) {
        glGenFramebuffers(1, &sEyeFramebuffer);

        if (sEyeFramebuffer == 0) {
            printf("[VR] Could not create OpenXR eye framebuffer.\n");
            return false;
        }

        printf("[VR] OpenXR eye framebuffer created.\n");
    }

    GLint previousDrawFramebuffer = 0;
    GLint previousReadFramebuffer = 0;
    GLint previousReadBuffer = 0;
    GLint previousViewport[4] = { 0 };
    GLint previousScissorBox[4] = { 0 };
    GLfloat previousClearColor[4] = { 0 };
    GLboolean previousColorMask[4] = { 0 };
    GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);

    glGetIntegerv(
        GL_DRAW_FRAMEBUFFER_BINDING,
        &previousDrawFramebuffer
    );
    glGetIntegerv(
        GL_READ_FRAMEBUFFER_BINDING,
        &previousReadFramebuffer
    );
    glGetIntegerv(GL_READ_BUFFER, &previousReadBuffer);
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    glGetIntegerv(GL_SCISSOR_BOX, previousScissorBox);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);
    glGetBooleanv(GL_COLOR_WRITEMASK, previousColorMask);

    if (previousViewport[2] <= 0 || previousViewport[3] <= 0) {
        printf(
            "[VR] Cannot copy the game frame: invalid OpenGL viewport.\n"
        );
        return false;
    }

    const GLenum textureTarget =
        view->recommendedSwapchainSampleCount > 1
            ? GL_TEXTURE_2D_MULTISAMPLE
            : GL_TEXTURE_2D;
    const GLuint texture = swapchain->images[imageIndex].image;

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sEyeFramebuffer);
    glFramebufferTexture2D(
        GL_DRAW_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        textureTarget,
        texture,
        0
    );
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    GLenum framebufferStatus =
        glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
    bool rendered = framebufferStatus == GL_FRAMEBUFFER_COMPLETE;

    if (rendered) {
        if (previousReadFramebuffer == 0) {
            glReadBuffer(GL_BACK);
        }

        vr_openxr_blit_game_frame(swapchain, previousViewport);
    } else {
        printf(
            "[VR] %s eye framebuffer is incomplete: 0x%X.\n",
            eyeName,
            (unsigned int)framebufferStatus
        );
    }

    glFramebufferTexture2D(
        GL_DRAW_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        textureTarget,
        0,
        0
    );

    glBindFramebuffer(
        GL_DRAW_FRAMEBUFFER,
        (GLuint)previousDrawFramebuffer
    );
    glBindFramebuffer(
        GL_READ_FRAMEBUFFER,
        (GLuint)previousReadFramebuffer
    );
    glReadBuffer((GLenum)previousReadBuffer);
    glViewport(
        previousViewport[0],
        previousViewport[1],
        previousViewport[2],
        previousViewport[3]
    );
    glScissor(
        previousScissorBox[0],
        previousScissorBox[1],
        previousScissorBox[2],
        previousScissorBox[3]
    );

    if (scissorWasEnabled) {
        glEnable(GL_SCISSOR_TEST);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }

    glColorMask(
        previousColorMask[0],
        previousColorMask[1],
        previousColorMask[2],
        previousColorMask[3]
    );
    glClearColor(
        previousClearColor[0],
        previousClearColor[1],
        previousClearColor[2],
        previousClearColor[3]
    );

    return rendered;
}
static bool vr_openxr_acquire_eye_image(
    uint32_t eyeIndex,
    uint32_t* imageIndex
) {
    const char* eyeName = eyeIndex == 0 ? "left" : "right";
    struct VrOpenXrSwapchain* swapchain =
        &sColorSwapchains[eyeIndex];

    XrSwapchainImageAcquireInfo acquireInfo = { 0 };
    acquireInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;

    XrResult result =
        sXr.xrAcquireSwapchainImage(
            swapchain->handle,
            &acquireInfo,
            imageIndex
        );

    if (result != XR_SUCCESS) {
        printf(
            "[VR] Could not acquire %s eye swapchain image: "
            "%s (%d)\n",
            eyeName,
            vr_openxr_result_name(result),
            (int)result
        );
        return false;
    }

    sEyeImageAcquired[eyeIndex] = true;

    if (*imageIndex >= swapchain->imageCount) {
        printf(
            "[VR] %s eye acquired image %u, but only %u exist.\n",
            eyeName,
            *imageIndex,
            swapchain->imageCount
        );
        return false;
    }

    XrSwapchainImageWaitInfo waitInfo = { 0 };
    waitInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
    waitInfo.timeout = XR_INFINITE_DURATION;

    result =
        sXr.xrWaitSwapchainImage(
            swapchain->handle,
            &waitInfo
        );

    if (result != XR_SUCCESS) {
        printf(
            "[VR] Could not wait for %s eye swapchain image: "
            "%s (%d)\n",
            eyeName,
            vr_openxr_result_name(result),
            (int)result
        );
        return false;
    }

    return true;
}

static bool vr_openxr_release_eye_image(uint32_t eyeIndex) {
    if (!sEyeImageAcquired[eyeIndex]) {
        return true;
    }

    const char* eyeName = eyeIndex == 0 ? "left" : "right";
    XrSwapchainImageReleaseInfo releaseInfo = { 0 };
    releaseInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;

    XrResult result =
        sXr.xrReleaseSwapchainImage(
            sColorSwapchains[eyeIndex].handle,
            &releaseInfo
        );

    sEyeImageAcquired[eyeIndex] = false;

    if (result != XR_SUCCESS) {
        printf(
            "[VR] Could not release %s eye swapchain image: "
            "%s (%d)\n",
            eyeName,
            vr_openxr_result_name(result),
            (int)result
        );
        return false;
    }

    return true;
}

static bool vr_openxr_ensure_direct_eye_target(
    uint32_t eyeIndex,
    bool requireDepthBuffer
) {
    const struct VrOpenXrSwapchain* swapchain =
        &sColorSwapchains[eyeIndex];
    const XrViewConfigurationView* view =
        &sViewConfigurationViews[eyeIndex];

    if (sEyeFramebuffer == 0) {
        glGenFramebuffers(1, &sEyeFramebuffer);

        if (sEyeFramebuffer == 0) {
            printf(
                "[VR] Could not create OpenXR eye framebuffer.\n"
            );
            return false;
        }

        printf("[VR] OpenXR eye framebuffer created.\n");
    }

    if (!requireDepthBuffer ||
        sEyeDepthRenderbuffers[eyeIndex] != 0) {
        return true;
    }

    GLint previousRenderbuffer = 0;
    glGetIntegerv(
        GL_RENDERBUFFER_BINDING,
        &previousRenderbuffer
    );
    glGenRenderbuffers(
        1,
        &sEyeDepthRenderbuffers[eyeIndex]
    );

    if (sEyeDepthRenderbuffers[eyeIndex] == 0) {
        printf(
            "[VR] Could not create %s eye depth buffer.\n",
            eyeIndex == 0 ? "left" : "right"
        );
        return false;
    }

    glBindRenderbuffer(
        GL_RENDERBUFFER,
        sEyeDepthRenderbuffers[eyeIndex]
    );

    if (view->recommendedSwapchainSampleCount > 1) {
        glRenderbufferStorageMultisample(
            GL_RENDERBUFFER,
            (GLsizei)view->recommendedSwapchainSampleCount,
            GL_DEPTH_COMPONENT24,
            (GLsizei)swapchain->width,
            (GLsizei)swapchain->height
        );
    } else {
        glRenderbufferStorage(
            GL_RENDERBUFFER,
            GL_DEPTH_COMPONENT24,
            (GLsizei)swapchain->width,
            (GLsizei)swapchain->height
        );
    }

    glBindRenderbuffer(
        GL_RENDERBUFFER,
        (GLuint)previousRenderbuffer
    );
    return true;
}

static bool vr_openxr_bind_scaled_eye_target(
    uint32_t eyeIndex,
    uint32_t width,
    uint32_t height
) {
    const XrViewConfigurationView* view =
        &sViewConfigurationViews[eyeIndex];
    GLint previousRenderbuffer = 0;
    const bool recreateTargets =
        sScaledEyeWidths[eyeIndex] != width ||
        sScaledEyeHeights[eyeIndex] != height;
    const bool allocateColor =
        recreateTargets ||
        sScaledEyeColorRenderbuffers[eyeIndex] == 0;
    const bool allocateDepth =
        recreateTargets ||
        sScaledEyeDepthRenderbuffers[eyeIndex] == 0;

    if (sScaledEyeFramebuffer == 0) {
        glGenFramebuffers(1, &sScaledEyeFramebuffer);
    }

    if (sScaledEyeFramebuffer == 0) {
        printf("[VR] Could not create scaled eye framebuffer.\n");
        return false;
    }

    if (recreateTargets) {
        glDeleteRenderbuffers(
            1,
            &sScaledEyeColorRenderbuffers[eyeIndex]
        );
        glDeleteRenderbuffers(
            1,
            &sScaledEyeDepthRenderbuffers[eyeIndex]
        );
        sScaledEyeColorRenderbuffers[eyeIndex] = 0;
        sScaledEyeDepthRenderbuffers[eyeIndex] = 0;
        sScaledEyeFramebufferValidated[eyeIndex] = false;
    }

    if (allocateColor || allocateDepth) {
        glGetIntegerv(
            GL_RENDERBUFFER_BINDING,
            &previousRenderbuffer
        );
    }

    if (sScaledEyeColorRenderbuffers[eyeIndex] == 0) {
        glGenRenderbuffers(
            1,
            &sScaledEyeColorRenderbuffers[eyeIndex]
        );
        glBindRenderbuffer(
            GL_RENDERBUFFER,
            sScaledEyeColorRenderbuffers[eyeIndex]
        );

        if (view->recommendedSwapchainSampleCount > 1) {
            glRenderbufferStorageMultisample(
                GL_RENDERBUFFER,
                (GLsizei)view->recommendedSwapchainSampleCount,
                (GLenum)sColorSwapchainFormat,
                (GLsizei)width,
                (GLsizei)height
            );
        } else {
            glRenderbufferStorage(
                GL_RENDERBUFFER,
                (GLenum)sColorSwapchainFormat,
                (GLsizei)width,
                (GLsizei)height
            );
        }
    }

    if (sScaledEyeDepthRenderbuffers[eyeIndex] == 0) {
        glGenRenderbuffers(
            1,
            &sScaledEyeDepthRenderbuffers[eyeIndex]
        );
        glBindRenderbuffer(
            GL_RENDERBUFFER,
            sScaledEyeDepthRenderbuffers[eyeIndex]
        );

        if (view->recommendedSwapchainSampleCount > 1) {
            glRenderbufferStorageMultisample(
                GL_RENDERBUFFER,
                (GLsizei)view->recommendedSwapchainSampleCount,
                GL_DEPTH_COMPONENT24,
                (GLsizei)width,
                (GLsizei)height
            );
        } else {
            glRenderbufferStorage(
                GL_RENDERBUFFER,
                GL_DEPTH_COMPONENT24,
                (GLsizei)width,
                (GLsizei)height
            );
        }
    }

    sScaledEyeWidths[eyeIndex] = width;
    sScaledEyeHeights[eyeIndex] = height;

    glBindFramebuffer(GL_FRAMEBUFFER, sScaledEyeFramebuffer);
    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_RENDERBUFFER,
        sScaledEyeColorRenderbuffers[eyeIndex]
    );
    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER,
        GL_DEPTH_ATTACHMENT,
        GL_RENDERBUFFER,
        sScaledEyeDepthRenderbuffers[eyeIndex]
    );
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    if (allocateColor || allocateDepth) {
        glBindRenderbuffer(
            GL_RENDERBUFFER,
            (GLuint)previousRenderbuffer
        );
    }

    if (!sScaledEyeFramebufferValidated[eyeIndex]) {
        const GLenum status =
            glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            printf(
                "[VR] Scaled %s eye framebuffer is incomplete: 0x%X.\n",
                eyeIndex == 0 ? "left" : "right",
                (unsigned int)status
            );
            return false;
        }
        sScaledEyeFramebufferValidated[eyeIndex] = true;
    }

    return true;
}

bool vr_openxr_begin_eye(
    uint32_t eyeIndex,
    uint32_t* width,
    uint32_t* height
) {
    if (eyeIndex >= 2 ||
        width == NULL ||
        height == NULL ||
        !sFrameBegun ||
        !sFrameShouldRender ||
        !sFrameViewsLocated ||
        !sViewPoseValid ||
        sActiveRenderEye >= 0) {
        return false;
    }

    uint32_t imageIndex = 0;

    if (!vr_openxr_acquire_eye_image(
            eyeIndex,
            &imageIndex
        )) {
        vr_openxr_release_eye_image(eyeIndex);
        return false;
    }

    const struct VrOpenXrSwapchain* swapchain =
        &sColorSwapchains[eyeIndex];
    const XrViewConfigurationView* view =
        &sViewConfigurationViews[eyeIndex];
    const GLenum textureTarget =
        view->recommendedSwapchainSampleCount > 1
            ? GL_TEXTURE_2D_MULTISAMPLE
            : GL_TEXTURE_2D;
    const GLuint texture =
        swapchain->images[imageIndex].image;

    uint32_t renderWidth =
        vr_openxr_scaled_dimension(swapchain->width);
    uint32_t renderHeight =
        vr_openxr_scaled_dimension(swapchain->height);

    // OpenGL cannot scale while resolving a multisampled renderbuffer.
    // Keep the runtime's full size for the uncommon MSAA swapchain case.
    if (view->recommendedSwapchainSampleCount > 1) {
        renderWidth = swapchain->width;
        renderHeight = swapchain->height;
    }
    sActiveEyeUsesScaledTarget =
        renderWidth != swapchain->width ||
        renderHeight != swapchain->height;

    if (!vr_openxr_ensure_direct_eye_target(
            eyeIndex,
            !sActiveEyeUsesScaledTarget
        )) {
        sActiveEyeUsesScaledTarget = false;
        vr_openxr_release_eye_image(eyeIndex);
        return false;
    }

    if (!sPreviousFramebufferStateValid) {
        glGetIntegerv(
            GL_DRAW_FRAMEBUFFER_BINDING,
            &sPreviousDrawFramebuffer
        );
        glGetIntegerv(
            GL_READ_FRAMEBUFFER_BINDING,
            &sPreviousReadFramebuffer
        );
        glGetIntegerv(GL_DRAW_BUFFER, &sPreviousDrawBuffer);
        glGetIntegerv(GL_READ_BUFFER, &sPreviousReadBuffer);
        sPreviousFramebufferStateValid = true;
    }

    glBindFramebuffer(
        GL_FRAMEBUFFER,
        sEyeFramebuffer
    );
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        textureTarget,
        texture,
        0
    );
    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER,
        GL_DEPTH_ATTACHMENT,
        GL_RENDERBUFFER,
        sActiveEyeUsesScaledTarget
            ? 0
            : sEyeDepthRenderbuffers[eyeIndex]
    );
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    GLenum framebufferStatus = GL_FRAMEBUFFER_COMPLETE;
    if (!swapchain->framebufferValidated[imageIndex]) {
        framebufferStatus =
            glCheckFramebufferStatus(GL_FRAMEBUFFER);
    }

    if (framebufferStatus != GL_FRAMEBUFFER_COMPLETE) {
        printf(
            "[VR] %s direct eye framebuffer is "
            "incomplete: 0x%X.\n",
            eyeIndex == 0 ? "left" : "right",
            (unsigned int)framebufferStatus
        );
        glFramebufferTexture2D(
            GL_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0,
            textureTarget,
            0,
            0
        );
        glFramebufferRenderbuffer(
            GL_FRAMEBUFFER,
            GL_DEPTH_ATTACHMENT,
            GL_RENDERBUFFER,
            0
        );
        glBindFramebuffer(
            GL_DRAW_FRAMEBUFFER,
            (GLuint)sPreviousDrawFramebuffer
        );
        glBindFramebuffer(
            GL_READ_FRAMEBUFFER,
            (GLuint)sPreviousReadFramebuffer
        );
        vr_openxr_release_eye_image(eyeIndex);
        return false;
    }

    if (sActiveEyeUsesScaledTarget &&
        !vr_openxr_bind_scaled_eye_target(
            eyeIndex,
            renderWidth,
            renderHeight
        )) {
        sActiveEyeUsesScaledTarget = false;
        glBindFramebuffer(GL_FRAMEBUFFER, sEyeFramebuffer);
        glFramebufferTexture2D(
            GL_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0,
            textureTarget,
            0,
            0
        );
        glFramebufferRenderbuffer(
            GL_FRAMEBUFFER,
            GL_DEPTH_ATTACHMENT,
            GL_RENDERBUFFER,
            0
        );
        glBindFramebuffer(
            GL_DRAW_FRAMEBUFFER,
            (GLuint)sPreviousDrawFramebuffer
        );
        glBindFramebuffer(
            GL_READ_FRAMEBUFFER,
            (GLuint)sPreviousReadFramebuffer
        );
        vr_openxr_release_eye_image(eyeIndex);
        return false;
    }
    swapchain->framebufferValidated[imageIndex] = true;

    sEyeDirectImageIndices[eyeIndex] = imageIndex;
    sActiveRenderEye = (int32_t)eyeIndex;
    *width = renderWidth;
    *height = renderHeight;
    return true;
}

bool vr_openxr_end_eye(uint32_t eyeIndex) {
    if (eyeIndex >= 2 ||
        sActiveRenderEye != (int32_t)eyeIndex) {
        return false;
    }

    const XrViewConfigurationView* view =
        &sViewConfigurationViews[eyeIndex];
    const GLenum textureTarget =
        view->recommendedSwapchainSampleCount > 1
            ? GL_TEXTURE_2D_MULTISAMPLE
            : GL_TEXTURE_2D;

    if (sActiveEyeUsesScaledTarget) {
        const struct VrOpenXrSwapchain* swapchain =
            &sColorSwapchains[eyeIndex];

        // The OpenGL renderer enables scissoring at the start of every scene
        // render and never disables it while processing the display list.
        // Restore that known state directly instead of synchronously querying
        // the driver for every scaled eye.
        glDisable(GL_SCISSOR_TEST);

        glBindFramebuffer(
            GL_READ_FRAMEBUFFER,
            sScaledEyeFramebuffer
        );
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glBindFramebuffer(
            GL_DRAW_FRAMEBUFFER,
            sEyeFramebuffer
        );
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glBlitFramebuffer(
            0,
            0,
            (GLint)sScaledEyeWidths[eyeIndex],
            (GLint)sScaledEyeHeights[eyeIndex],
            0,
            0,
            (GLint)swapchain->width,
            (GLint)swapchain->height,
            GL_COLOR_BUFFER_BIT,
            GL_LINEAR
        );

        glEnable(GL_SCISSOR_TEST);
    }

    glFlush();
    glBindFramebuffer(
        GL_DRAW_FRAMEBUFFER,
        sEyeFramebuffer
    );
    glFramebufferTexture2D(
        GL_DRAW_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        textureTarget,
        0,
        0
    );
    glFramebufferRenderbuffer(
        GL_DRAW_FRAMEBUFFER,
        GL_DEPTH_ATTACHMENT,
        GL_RENDERBUFFER,
        0
    );
    glBindFramebuffer(
        GL_DRAW_FRAMEBUFFER,
        (GLuint)sPreviousDrawFramebuffer
    );
    glDrawBuffer((GLenum)sPreviousDrawBuffer);
    glBindFramebuffer(
        GL_READ_FRAMEBUFFER,
        (GLuint)sPreviousReadFramebuffer
    );
    glReadBuffer((GLenum)sPreviousReadBuffer);

    sActiveRenderEye = -1;
    sActiveEyeUsesScaledTarget = false;

    if (!vr_openxr_release_eye_image(eyeIndex)) {
        return false;
    }

    sEyeDirectRendered[eyeIndex] = true;

    if (!sDirectRenderingLogged &&
        sEyeDirectRendered[0] &&
        sEyeDirectRendered[1]) {
        printf(
            "[VR] Game rendered directly into both "
            "OpenXR eye targets.\n"
        );
        sDirectRenderingLogged = true;
    }

    return true;
}

bool vr_openxr_mirror_eye(
    uint32_t eyeIndex,
    uint32_t width,
    uint32_t height
) {
    if (eyeIndex >= 2 ||
        sActiveRenderEye != (int32_t)eyeIndex ||
        width == 0 ||
        height == 0) {
        return false;
    }

    const struct VrOpenXrSwapchain* swapchain =
        &sColorSwapchains[eyeIndex];
    if (swapchain->width == 0 || swapchain->height == 0) {
        return false;
    }

    GLint sourceX = 0;
    GLint sourceY = 0;
    GLint sourceWidth = (GLint)
        vr_openxr_scaled_dimension(swapchain->width);
    GLint sourceHeight = (GLint)
        vr_openxr_scaled_dimension(swapchain->height);
    const float sourceAspect =
        (float)sourceWidth / (float)sourceHeight;
    const float destinationAspect =
        (float)width / (float)height;

    // Fill the desktop window without stretching the headset image. A
    // single eye is taller than a typical capture window, so this normally
    // center-crops the top and bottom while preserving the correct aspect.
    if (sourceAspect > destinationAspect) {
        const GLint croppedWidth =
            (GLint)((float)sourceHeight * destinationAspect);
        sourceX = (sourceWidth - croppedWidth) / 2;
        sourceWidth = croppedWidth;
    } else {
        const GLint croppedHeight =
            (GLint)((float)sourceWidth / destinationAspect);
        sourceY = (sourceHeight - croppedHeight) / 2;
        sourceHeight = croppedHeight;
    }

    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(
        GL_READ_FRAMEBUFFER,
        sActiveEyeUsesScaledTarget
            ? sScaledEyeFramebuffer
            : sEyeFramebuffer
    );
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(
        GL_DRAW_FRAMEBUFFER,
        (GLuint)sPreviousDrawFramebuffer
    );
    glDrawBuffer((GLenum)sPreviousDrawBuffer);
    glBlitFramebuffer(
        sourceX,
        sourceY,
        sourceX + sourceWidth,
        sourceY + sourceHeight,
        0,
        0,
        (GLint)width,
        (GLint)height,
        GL_COLOR_BUFFER_BIT,
        GL_LINEAR
    );

    const GLuint activeEyeFramebuffer =
        sActiveEyeUsesScaledTarget
            ? sScaledEyeFramebuffer
            : sEyeFramebuffer;
    glBindFramebuffer(
        GL_DRAW_FRAMEBUFFER,
        activeEyeFramebuffer
    );
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(
        GL_READ_FRAMEBUFFER,
        activeEyeFramebuffer
    );
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    // gfx_opengl_start_frame() establishes this state before every eye.
    glEnable(GL_SCISSOR_TEST);

    if (!sDesktopMirrorLogged) {
        printf(
            "[VR] Desktop mirror is displaying the "
            "left OpenXR eye.\n"
        );
        sDesktopMirrorLogged = true;
    }

    return true;
}

static bool vr_openxr_cycle_swapchain_image(
    uint32_t eyeIndex,
    uint32_t* imageIndex
) {
    if (!vr_openxr_acquire_eye_image(
            eyeIndex,
            imageIndex
        )) {
        vr_openxr_release_eye_image(eyeIndex);
        return false;
    }

    const bool imageRendered =
        vr_openxr_copy_game_frame_to_eye(
            eyeIndex,
            *imageIndex
        );
    const bool imageReleased =
        vr_openxr_release_eye_image(eyeIndex);

    return imageRendered && imageReleased;
}

static void vr_openxr_log_swapchain_cycle(
    const uint32_t imageIndices[2]
) {
    if (sSwapchainCycleLogged) {
        return;
    }

    printf(
        "[VR] Live game stereo frame: left image %u, "
        "right image %u.\n",
        imageIndices[0],
        imageIndices[1]
    );
    sSwapchainCycleLogged = true;
}

bool vr_openxr_begin_frame(void) {
    if (sSession == XR_NULL_HANDLE) {
        return true;
    }

    if (sFrameBegun) {
        printf("[VR] Tried to begin a new OpenXR frame before ending the previous frame.\n");
        return false;
    }

    if (!vr_openxr_poll_events()) {
        return false;
    }

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

    XrFrameBeginInfo beginInfo = { 0 };
    beginInfo.type = XR_TYPE_FRAME_BEGIN_INFO;

    result = sXr.xrBeginFrame(sSession, &beginInfo);

    if (XR_FAILED(result)) {
        printf(
            "[VR] xrBeginFrame failed: %s (%d)\n",
            vr_openxr_result_name(result),
            (int)result
        );
        return false;
    }

    sFrameBegun = true;
    sPreviousFramebufferStateValid = false;
    memset(
        sEyeDirectRendered,
        0,
        sizeof(sEyeDirectRendered)
    );
    memset(
        sEyeImageAcquired,
        0,
        sizeof(sEyeImageAcquired)
    );
    sActiveRenderEye = -1;
    sFrameDisplayTime = frameState.predictedDisplayTime;
    sFrameShouldRender = frameState.shouldRender;
    sFrameViewsLocated =
        vr_openxr_locate_views(sFrameDisplayTime);

    if (sVrEnterRecenterPending &&
        sFrameViewsLocated &&
        sViewPoseValid &&
        sSessionState == XR_SESSION_STATE_FOCUSED) {
        // Do not accept the early valid poses reported while the session is
        // merely visible. The first focused pose represents the headset's
        // settled play position and becomes the neutral height/yaw for this
        // VR activation.
        sHeadOrientationReferenceValid = false;
        sCachedTrackingValid = false;
        sVrEnterRecenterPending = false;
        sTrackingOriginGeneration++;
        printf(
            "[VR] Headset automatically recalibrated "
            "on VR entry.\n"
        );
    }

    if (sFrameViewsLocated &&
        sViewPoseValid &&
        sTrackingOriginRecenterPending &&
        sFrameDisplayTime >= sTrackingOriginRecenterTime) {
        // The LOCAL-space change is now active. Capture the first pose in the
        // new space as the application's neutral origin so headset, hands,
        // body yaw, and room-scale translation all rebase together.
        sHeadOrientationReferenceValid = false;
        sCachedTrackingValid = false;
        sTrackingOriginRecenterPending = false;
        sTrackingOriginRecenterTime = 0;
        sTrackingOriginGeneration++;
    }

    if (sFrameViewsLocated &&
        sViewPoseValid &&
        !sVrEnterRecenterPending &&
        !sHeadOrientationReferenceValid) {
        sHeadOrientationReference =
            vr_openxr_yaw_only_orientation(
                &sViews[0].pose.orientation
            );
        sHeadPositionReference.x =
            (sViews[0].pose.position.x +
             sViews[1].pose.position.x) * 0.5f;
        sHeadPositionReference.y =
            (sViews[0].pose.position.y +
             sViews[1].pose.position.y) * 0.5f;
        sHeadPositionReference.z =
            (sViews[0].pose.position.z +
             sViews[1].pose.position.z) * 0.5f;
        sHeadOrientationReferenceValid = true;

        printf(
            "[VR] Headset origin centered for camera "
            "rotation and translation.\n"
        );
    }

    vr_openxr_update_cached_tracking();
    vr_openxr_update_controller_states(sFrameDisplayTime);

    if (!sFrameTimingLogged &&
        sFrameViewsLocated &&
        sViewPoseValid) {
        printf(
            "[VR] Predicted headset pose synchronized "
            "before game rendering.\n"
        );
        sFrameTimingLogged = true;
    }

    return true;
}

bool vr_openxr_end_frame(void) {
    if (!sFrameBegun) {
        return true;
    }

    const bool viewsLocated = sFrameViewsLocated;
    const bool canSubmitProjection =
        sFrameShouldRender &&
        viewsLocated &&
        sViewPoseValid;
    bool imagesRendered = true;
    uint32_t imageIndices[2] = { 0 };

    if (canSubmitProjection) {
        for (uint32_t eye = 0; eye < 2; eye++) {
            if (sEyeDirectRendered[eye]) {
                imageIndices[eye] =
                    sEyeDirectImageIndices[eye];
                continue;
            }

            if (!vr_openxr_cycle_swapchain_image(
                    eye,
                    &imageIndices[eye]
                )) {
                imagesRendered = false;
                break;
            }
        }
    }

    XrCompositionLayerProjectionView projectionViews[2] = { 0 };
    XrCompositionLayerProjection projectionLayer = { 0 };
    const XrCompositionLayerBaseHeader* layers[1] = { NULL };
    uint32_t layerCount = 0;

    if (canSubmitProjection && imagesRendered) {
        for (uint32_t eye = 0; eye < 2; eye++) {
            projectionViews[eye].type =
                XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            projectionViews[eye].pose = sViews[eye].pose;
            projectionViews[eye].fov = sViews[eye].fov;
            projectionViews[eye].subImage.swapchain =
                sColorSwapchains[eye].handle;
            projectionViews[eye].subImage.imageRect.offset.x = 0;
            projectionViews[eye].subImage.imageRect.offset.y = 0;
            projectionViews[eye].subImage.imageRect.extent.width =
                (int32_t)sColorSwapchains[eye].width;
            projectionViews[eye].subImage.imageRect.extent.height =
                (int32_t)sColorSwapchains[eye].height;
            projectionViews[eye].subImage.imageArrayIndex = 0;
        }

        projectionLayer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
        projectionLayer.layerFlags = 0;
        projectionLayer.space = sViewSpace;
        projectionLayer.viewCount = 2;
        projectionLayer.views = projectionViews;

        layers[0] =
            (const XrCompositionLayerBaseHeader*)&projectionLayer;
        layerCount = 1;

        vr_openxr_log_swapchain_cycle(imageIndices);
    }

    XrFrameEndInfo endInfo = { 0 };
    endInfo.type = XR_TYPE_FRAME_END_INFO;
    endInfo.displayTime = sFrameDisplayTime;
    endInfo.environmentBlendMode = sEnvironmentBlendMode;
    endInfo.layerCount = layerCount;
    endInfo.layers = layerCount > 0 ? layers : NULL;

    XrResult result = sXr.xrEndFrame(sSession, &endInfo);

    sFrameBegun = false;
    sFrameDisplayTime = 0;
    sFrameShouldRender = false;
    sFrameViewsLocated = false;

    if (XR_FAILED(result)) {
        printf(
            "[VR] xrEndFrame failed: %s (%d)\n",
            vr_openxr_result_name(result),
            (int)result
        );
        return false;
    }

    return viewsLocated && imagesRendered;
}

bool vr_openxr_get_eye_offset(
    uint32_t eyeIndex,
    float offset[3]
) {
    if (eyeIndex >= 2 ||
        offset == NULL ||
        !sFrameViewsLocated ||
        !sViewPoseValid ||
        !sCachedTrackingValid) {
        return false;
    }

    offset[0] = sCachedEyeOffsets[eyeIndex][0];
    offset[1] = sCachedEyeOffsets[eyeIndex][1];
    offset[2] = sCachedEyeOffsets[eyeIndex][2];

    if (eyeIndex == 1 && !sStereoEyeOffsetsLogged) {
        const float eyeDistanceX =
            sViews[1].pose.position.x -
            sViews[0].pose.position.x;
        const float eyeDistanceY =
            sViews[1].pose.position.y -
            sViews[0].pose.position.y;
        const float eyeDistanceZ =
            sViews[1].pose.position.z -
            sViews[0].pose.position.z;
        const float ipdMillimeters = 1000.0f * sqrtf(
            eyeDistanceX * eyeDistanceX +
            eyeDistanceY * eyeDistanceY +
            eyeDistanceZ * eyeDistanceZ
        );

        printf(
            "[VR] Runtime stereo eye separation active "
            "(IPD %.1f mm).\n",
            ipdMillimeters
        );
        sStereoEyeOffsetsLogged = true;
    }

    return true;
}

bool vr_openxr_get_eye_fov(
    uint32_t eyeIndex,
    float fov[4]
) {
    if (eyeIndex >= 2 ||
        fov == NULL ||
        !sViewPoseValid) {
        return false;
    }

    fov[0] = sViews[eyeIndex].fov.angleLeft;
    fov[1] = sViews[eyeIndex].fov.angleRight;
    fov[2] = sViews[eyeIndex].fov.angleDown;
    fov[3] = sViews[eyeIndex].fov.angleUp;

    if (eyeIndex == 1 && !sAsymmetricProjectionLogged) {
        printf(
            "[VR] Runtime asymmetric eye projections active.\n"
        );
        sAsymmetricProjectionLogged = true;
    }

    return true;
}
bool vr_openxr_get_head_rotation(float rotation[4]) {
    if (rotation == NULL ||
        !sHeadOrientationReferenceValid ||
        !sViewPoseValid ||
        !sCachedTrackingValid) {
        return false;
    }

    rotation[0] = sCachedHeadRotation[0];
    rotation[1] = sCachedHeadRotation[1];
    rotation[2] = sCachedHeadRotation[2];
    rotation[3] = sCachedHeadRotation[3];
    return true;
}

bool vr_openxr_get_head_translation(float translation[3]) {
    if (translation == NULL ||
        !sHeadOrientationReferenceValid ||
        !sViewPoseValid ||
        !sCachedTrackingValid) {
        return false;
    }

    translation[0] = sCachedHeadTranslation[0];
    translation[1] = sCachedHeadTranslation[1];
    translation[2] = sCachedHeadTranslation[2];
    return true;
}

bool vr_openxr_get_calibrated_head_height(float* height) {
    if (height == NULL ||
        !sHeadOrientationReferenceValid ||
        !sViewPoseValid) {
        return false;
    }

    // Most runtimes expose the LOCAL origin at floor level after their room
    // calibration. A few use an eye-level LOCAL origin; retain a normal adult
    // standing-height fallback so the same one-third descent threshold remains
    // usable on those runtimes.
    *height = fabsf(sHeadPositionReference.y);
    if (*height < 0.75f || *height > 2.50f) {
        *height = 1.65f;
    }
    return true;
}

uint32_t vr_openxr_get_tracking_origin_generation(void) {
    return sTrackingOriginGeneration;
}

void vr_openxr_request_recenter(void) {
    // Keep this pending across instance startup/session creation. It is
    // consumed only after OpenXR supplies a valid pose in the FOCUSED state.
    sVrEnterRecenterPending = true;
    sHeadOrientationReferenceValid = false;
    sCachedTrackingValid = false;
}

bool vr_openxr_get_controller_state(
    uint32_t handIndex,
    struct VrControllerState* state
) {
    if (handIndex >= VR_CONTROLLER_COUNT ||
        state == NULL ||
        !sControllerActionsAttached) {
        return false;
    }

    *state = sControllerStates[handIndex];
    return true;
}

bool vr_openxr_apply_haptic(
    uint32_t handIndex,
    float amplitude,
    float durationSeconds,
    float frequency
) {
    if (handIndex >= VR_CONTROLLER_COUNT ||
        !sControllerActionsAttached ||
        !sSessionRunning) {
        return false;
    }

    XrHapticActionInfo actionInfo = { 0 };
    actionInfo.type = XR_TYPE_HAPTIC_ACTION_INFO;
    actionInfo.action = sHapticAction;
    actionInfo.subactionPath = sControllerHandPaths[handIndex];

    if (amplitude <= 0.0f) {
        return XR_SUCCEEDED(
            sXr.xrStopHapticFeedback(sSession, &actionInfo)
        );
    }

    if (amplitude > 1.0f) {
        amplitude = 1.0f;
    }
    if (frequency < 0.0f) {
        frequency = XR_FREQUENCY_UNSPECIFIED;
    }

    XrHapticVibration vibration = { 0 };
    vibration.type = XR_TYPE_HAPTIC_VIBRATION;
    vibration.amplitude = amplitude;
    vibration.frequency = frequency;
    vibration.duration = durationSeconds > 0.0f
        ? (XrDuration)(durationSeconds * 1000000000.0f)
        : XR_MIN_HAPTIC_DURATION;

    return XR_SUCCEEDED(
        sXr.xrApplyHapticFeedback(
            sSession,
            &actionInfo,
            (const XrHapticBaseHeader*)&vibration
        )
    );
}

void vr_openxr_shutdown(void) {
    bool hadOpenXR =
        sColorSwapchains[0].handle != XR_NULL_HANDLE ||
        sColorSwapchains[1].handle != XR_NULL_HANDLE ||
        sViewSpace != XR_NULL_HANDLE ||
        sSession != XR_NULL_HANDLE ||
        sInstance != XR_NULL_HANDLE ||
        sLoader != NULL;

    vr_openxr_destroy_eye_framebuffer();
    vr_openxr_destroy_color_swapchains();
    vr_openxr_destroy_controller_action_spaces();

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
    sViewPoseLogged = false;
    sInvalidViewPoseLogged = false;
    sSwapchainCycleLogged = false;
    sFrameDisplayTime = 0;
    sFrameBegun = false;
    sFrameShouldRender = false;
    sFrameViewsLocated = false;
    sFrameTimingLogged = false;
    memset(
        sEyeDirectRendered,
        0,
        sizeof(sEyeDirectRendered)
    );
    memset(
        sEyeImageAcquired,
        0,
        sizeof(sEyeImageAcquired)
    );
    memset(
        sEyeDirectImageIndices,
        0,
        sizeof(sEyeDirectImageIndices)
    );
    sActiveRenderEye = -1;
    sPreviousDrawFramebuffer = 0;
    sPreviousReadFramebuffer = 0;
    sPreviousDrawBuffer = GL_BACK;
    sPreviousReadBuffer = GL_BACK;
    sDirectRenderingLogged = false;
    sDesktopMirrorLogged = false;
    sStereoEyeOffsetsLogged = false;
    sAsymmetricProjectionLogged = false;
    memset(
        &sHeadOrientationReference,
        0,
        sizeof(sHeadOrientationReference)
    );
    memset(
        &sHeadPositionReference,
        0,
        sizeof(sHeadPositionReference)
    );
    sHeadOrientationReferenceValid = false;
    sVrEnterRecenterPending = false;
    sTrackingOriginRecenterPending = false;
    sTrackingOriginRecenterTime = 0;
    // Keep the generation monotonic across VR disable/re-enable cycles. The
    // renderer retains its last observed value while VR is off, so resetting
    // this counter here could make the next activation look unchanged and
    // interpolate from stale camera/body samples.
    sCachedTrackingValid = false;
    memset(sCachedEyeOffsets, 0, sizeof(sCachedEyeOffsets));
    memset(sCachedHeadRotation, 0, sizeof(sCachedHeadRotation));
    memset(sCachedHeadTranslation, 0, sizeof(sCachedHeadTranslation));

    if (sSession != XR_NULL_HANDLE &&
        sXr.xrDestroySession != NULL) {
        sXr.xrDestroySession(sSession);
    }

    sSession = XR_NULL_HANDLE;
    sSessionRunning = false;
    sSessionState = XR_SESSION_STATE_UNKNOWN;
    sEnvironmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

    vr_openxr_destroy_controller_actions();

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

bool vr_openxr_begin_frame(void) {
    return false;
}

bool vr_openxr_end_frame(void) {
    return false;
}

bool vr_openxr_begin_eye(
    uint32_t eyeIndex,
    uint32_t* width,
    uint32_t* height
) {
    (void)eyeIndex;
    (void)width;
    (void)height;
    return false;
}

bool vr_openxr_end_eye(uint32_t eyeIndex) {
    (void)eyeIndex;
    return false;
}

bool vr_openxr_mirror_eye(
    uint32_t eyeIndex,
    uint32_t width,
    uint32_t height
) {
    (void)eyeIndex;
    (void)width;
    (void)height;
    return false;
}

bool vr_openxr_get_eye_offset(
    uint32_t eyeIndex,
    float offset[3]
) {
    (void)eyeIndex;
    (void)offset;
    return false;
}

bool vr_openxr_get_eye_fov(
    uint32_t eyeIndex,
    float fov[4]
) {
    (void)eyeIndex;
    (void)fov;
    return false;
}
bool vr_openxr_get_head_rotation(float rotation[4]) {
    (void)rotation;
    return false;
}

bool vr_openxr_get_head_translation(float translation[3]) {
    (void)translation;
    return false;
}

bool vr_openxr_get_calibrated_head_height(float* height) {
    (void)height;
    return false;
}

uint32_t vr_openxr_get_tracking_origin_generation(void) {
    return 0;
}

void vr_openxr_request_recenter(void) {
}

bool vr_openxr_get_controller_state(
    uint32_t handIndex,
    struct VrControllerState* state
) {
    (void)handIndex;
    (void)state;
    return false;
}

bool vr_openxr_apply_haptic(
    uint32_t handIndex,
    float amplitude,
    float durationSeconds,
    float frequency
) {
    (void)handIndex;
    (void)amplitude;
    (void)durationSeconds;
    (void)frequency;
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
