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
};


static HMODULE sLoader = NULL;

static XrInstance sInstance = XR_NULL_HANDLE;
static XrSystemId sSystemId = XR_NULL_SYSTEM_ID;

static struct VrOpenXrFunctions sXr = { 0 };

static PFN_xrGetOpenGLGraphicsRequirementsKHR
    sXrGetOpenGLGraphicsRequirementsKHR = NULL;


static const char* vr_openxr_result_name(XrResult result) {
    switch (result) {
        case XR_SUCCESS:
            return "XR_SUCCESS";

        case XR_ERROR_RUNTIME_UNAVAILABLE:
            return "XR_ERROR_RUNTIME_UNAVAILABLE";

        case XR_ERROR_FORM_FACTOR_UNAVAILABLE:
            return "XR_ERROR_FORM_FACTOR_UNAVAILABLE";

        case XR_ERROR_FORM_FACTOR_UNSUPPORTED:
            return "XR_ERROR_FORM_FACTOR_UNSUPPORTED";

        case XR_ERROR_API_VERSION_UNSUPPORTED:
            return "XR_ERROR_API_VERSION_UNSUPPORTED";

        case XR_ERROR_INITIALIZATION_FAILED:
            return "XR_ERROR_INITIALIZATION_FAILED";

        case XR_ERROR_RUNTIME_FAILURE:
            return "XR_ERROR_RUNTIME_FAILURE";

        case XR_ERROR_EXTENSION_NOT_PRESENT:
            return "XR_ERROR_EXTENSION_NOT_PRESENT";

        case XR_ERROR_FUNCTION_UNSUPPORTED:
            return "XR_ERROR_FUNCTION_UNSUPPORTED";

        default:
            return "UNKNOWN_OPENXR_ERROR";
    }
}


static HMODULE vr_openxr_load_loader(void) {
    HMODULE loader = LoadLibraryA("libopenxr_loader.dll");

    if (loader == NULL) {
        loader = LoadLibraryA("openxr_loader.dll");
    }

    return loader;
}


static bool vr_openxr_load_global_functions(void) {
    sXr.xrGetInstanceProcAddr =
        (PFN_xrGetInstanceProcAddr)
        GetProcAddress(sLoader, "xrGetInstanceProcAddr");

    sXr.xrEnumerateInstanceExtensionProperties =
        (PFN_xrEnumerateInstanceExtensionProperties)
        GetProcAddress(
            sLoader,
            "xrEnumerateInstanceExtensionProperties"
        );

    sXr.xrCreateInstance =
        (PFN_xrCreateInstance)
        GetProcAddress(sLoader, "xrCreateInstance");

    sXr.xrDestroyInstance =
        (PFN_xrDestroyInstance)
        GetProcAddress(sLoader, "xrDestroyInstance");

    sXr.xrGetInstanceProperties =
        (PFN_xrGetInstanceProperties)
        GetProcAddress(sLoader, "xrGetInstanceProperties");

    sXr.xrGetSystem =
        (PFN_xrGetSystem)
        GetProcAddress(sLoader, "xrGetSystem");

    sXr.xrGetSystemProperties =
        (PFN_xrGetSystemProperties)
        GetProcAddress(sLoader, "xrGetSystemProperties");


    if (sXr.xrGetInstanceProcAddr == NULL ||
        sXr.xrEnumerateInstanceExtensionProperties == NULL ||
        sXr.xrCreateInstance == NULL ||
        sXr.xrDestroyInstance == NULL ||
        sXr.xrGetInstanceProperties == NULL ||
        sXr.xrGetSystem == NULL ||
        sXr.xrGetSystemProperties == NULL) {

        printf("[VR] OpenXR loader is missing required functions.\n");

        return false;
    }

    return true;
}


static bool vr_openxr_has_opengl_extension(void) {
    uint32_t extensionCount = 0;

    XrResult result =
        sXr.xrEnumerateInstanceExtensionProperties(
            NULL,
            0,
            &extensionCount,
            NULL
        );

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


    if (!vr_openxr_load_global_functions()) {
        vr_openxr_shutdown();
        return false;
    }


    printf("[VR] Checking for OpenGL OpenXR support...\n");

    if (!vr_openxr_has_opengl_extension()) {
        printf(
            "[VR] OpenXR runtime does not provide "
            "XR_KHR_opengl_enable.\n"
        );

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

    createInfo.applicationInfo.apiVersion =
        XR_MAKE_VERSION(1, 0, 0);


    createInfo.enabledExtensionCount = 1;
    createInfo.enabledExtensionNames = enabledExtensions;


    XrResult result =
        sXr.xrCreateInstance(
            &createInfo,
            &sInstance
        );


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


    XrInstanceProperties instanceProperties = { 0 };
    instanceProperties.type = XR_TYPE_INSTANCE_PROPERTIES;

    result =
        sXr.xrGetInstanceProperties(
            sInstance,
            &instanceProperties
        );


    if (XR_SUCCEEDED(result)) {
        printf(
            "[VR] OpenXR runtime: %s\n",
            instanceProperties.runtimeName
        );
    }


    XrSystemGetInfo systemInfo = { 0 };
    systemInfo.type = XR_TYPE_SYSTEM_GET_INFO;
    systemInfo.formFactor =
        XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;


    result =
        sXr.xrGetSystem(
            sInstance,
            &systemInfo,
            &sSystemId
        );


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
        sXr.xrGetSystemProperties(
            sInstance,
            sSystemId,
            &systemProperties
        );


    if (XR_SUCCEEDED(result)) {
        printf(
            "[VR] OpenXR headset/system: %s\n",
            systemProperties.systemName
        );
    }


    PFN_xrVoidFunction function = NULL;

    result =
        sXr.xrGetInstanceProcAddr(
            sInstance,
            "xrGetOpenGLGraphicsRequirementsKHR",
            &function
        );


    if (XR_FAILED(result) || function == NULL) {
        printf(
            "[VR] Could not load "
            "xrGetOpenGLGraphicsRequirementsKHR: %s (%d)\n",
            vr_openxr_result_name(result),
            (int)result
        );

        vr_openxr_shutdown();

        return false;
    }


    sXrGetOpenGLGraphicsRequirementsKHR =
        (PFN_xrGetOpenGLGraphicsRequirementsKHR)function;


    XrGraphicsRequirementsOpenGLKHR graphicsRequirements = { 0 };

    graphicsRequirements.type =
        XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR;


    result =
        sXrGetOpenGLGraphicsRequirementsKHR(
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
        XR_VERSION_MAJOR(
            graphicsRequirements.minApiVersionSupported
        ),
        XR_VERSION_MINOR(
            graphicsRequirements.minApiVersionSupported
        ),
        XR_VERSION_MAJOR(
            graphicsRequirements.maxApiVersionSupported
        ),
        XR_VERSION_MINOR(
            graphicsRequirements.maxApiVersionSupported
        )
    );


    printf("[VR] Persistent OpenXR context is ready.\n");

    return true;
}


void vr_openxr_shutdown(void) {
    bool hadOpenXR =
        (sInstance != XR_NULL_HANDLE || sLoader != NULL);


    if (sInstance != XR_NULL_HANDLE &&
        sXr.xrDestroyInstance != NULL) {

        sXr.xrDestroyInstance(sInstance);
    }


    sInstance = XR_NULL_HANDLE;
    sSystemId = XR_NULL_SYSTEM_ID;

    sXrGetOpenGLGraphicsRequirementsKHR = NULL;


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


#else


bool vr_openxr_startup(void) {
    printf(
        "[VR] OpenXR startup is not implemented "
        "on this platform yet.\n"
    );

    return false;
}


void vr_openxr_shutdown(void) {
}


bool vr_openxr_is_initialized(void) {
    return false;
}


#endif