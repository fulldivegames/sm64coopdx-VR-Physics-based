#include <stdio.h>

#include "pc/vr/vr_openxr.h"

#ifdef _WIN32

#include <windows.h>
#include <openxr/openxr.h>

struct VrOpenXrFunctions {
    PFN_xrCreateInstance xrCreateInstance;
    PFN_xrDestroyInstance xrDestroyInstance;
    PFN_xrGetInstanceProperties xrGetInstanceProperties;
    PFN_xrGetSystem xrGetSystem;
    PFN_xrGetSystemProperties xrGetSystemProperties;
};

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

bool vr_openxr_probe(void) {
    HMODULE loader = vr_openxr_load_loader();

    if (loader == NULL) {
        printf("[VR] Could not load the OpenXR loader DLL.\n");
        return false;
    }

    printf("[VR] OpenXR loader DLL found.\n");

    struct VrOpenXrFunctions xr = { 0 };

    xr.xrCreateInstance =
        (PFN_xrCreateInstance)GetProcAddress(loader, "xrCreateInstance");

    xr.xrDestroyInstance =
        (PFN_xrDestroyInstance)GetProcAddress(loader, "xrDestroyInstance");

    xr.xrGetInstanceProperties =
        (PFN_xrGetInstanceProperties)GetProcAddress(loader, "xrGetInstanceProperties");

    xr.xrGetSystem =
        (PFN_xrGetSystem)GetProcAddress(loader, "xrGetSystem");

    xr.xrGetSystemProperties =
        (PFN_xrGetSystemProperties)GetProcAddress(loader, "xrGetSystemProperties");

    if (xr.xrCreateInstance == NULL ||
        xr.xrDestroyInstance == NULL ||
        xr.xrGetInstanceProperties == NULL ||
        xr.xrGetSystem == NULL ||
        xr.xrGetSystemProperties == NULL) {

        printf("[VR] OpenXR loader is missing required functions.\n");
        FreeLibrary(loader);
        return false;
    }

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

    /*
     * We only require OpenXR 1.0 features right now.
     * Using 1.0 here gives us wider runtime compatibility.
     */
    createInfo.applicationInfo.apiVersion =
        XR_MAKE_VERSION(1, 0, 0);

    XrInstance instance = XR_NULL_HANDLE;

    XrResult result =
        xr.xrCreateInstance(&createInfo, &instance);

    if (XR_FAILED(result)) {
        printf(
            "[VR] xrCreateInstance failed: %s (%d)\n",
            vr_openxr_result_name(result),
            (int)result
        );

        FreeLibrary(loader);
        return false;
    }

    printf("[VR] OpenXR instance created successfully.\n");

    XrInstanceProperties instanceProperties = { 0 };
    instanceProperties.type = XR_TYPE_INSTANCE_PROPERTIES;

    result =
        xr.xrGetInstanceProperties(
            instance,
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
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    XrSystemId systemId = XR_NULL_SYSTEM_ID;

    result =
        xr.xrGetSystem(
            instance,
            &systemInfo,
            &systemId
        );

    if (XR_FAILED(result)) {
        printf(
            "[VR] Could not find an HMD: %s (%d)\n",
            vr_openxr_result_name(result),
            (int)result
        );

        xr.xrDestroyInstance(instance);
        FreeLibrary(loader);

        return false;
    }

    XrSystemProperties systemProperties = { 0 };
    systemProperties.type = XR_TYPE_SYSTEM_PROPERTIES;

    result =
        xr.xrGetSystemProperties(
            instance,
            systemId,
            &systemProperties
        );

    if (XR_SUCCEEDED(result)) {
        printf(
            "[VR] OpenXR headset/system: %s\n",
            systemProperties.systemName
        );
    }

    xr.xrDestroyInstance(instance);
    FreeLibrary(loader);

    printf("[VR] OpenXR probe succeeded.\n");

    return true;
}

#else

bool vr_openxr_probe(void) {
    printf("[VR] OpenXR probe is not implemented on this platform yet.\n");
    return false;
}

#endif