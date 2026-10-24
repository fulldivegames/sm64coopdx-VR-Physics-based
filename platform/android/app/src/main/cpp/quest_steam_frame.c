#include <android/native_activity.h>
#include <stdbool.h>
#include <jni.h>

extern ANativeActivity *quest_android_get_activity(void);

// Called on the native game thread. Android dialogs and permission requests
// themselves are dispatched by Java onto the UI thread.
static bool call_activity(const char *name, const char *message, bool boolean_result) {
    ANativeActivity *activity = quest_android_get_activity();
    if (activity == NULL || activity->vm == NULL || activity->clazz == NULL) return false;
    JNIEnv *env = NULL;
    bool detach = false;
    jint state = (*activity->vm)->GetEnv(activity->vm, (void **)&env, JNI_VERSION_1_6);
    if (state == JNI_EDETACHED) {
        if ((*activity->vm)->AttachCurrentThread(activity->vm, &env, NULL) != JNI_OK) return false;
        detach = true;
    } else if (state != JNI_OK || env == NULL) {
        return false;
    }
    jclass clazz = (*env)->GetObjectClass(env, activity->clazz);
    const char *signature = boolean_result ? "()Z" : "(Ljava/lang/String;)V";
    jmethodID method = clazz == NULL ? NULL : (*env)->GetMethodID(env, clazz, name, signature);
    bool result = false;
    if (method != NULL) {
        if (boolean_result) {
            result = (*env)->CallBooleanMethod(env, activity->clazz, method) == JNI_TRUE;
        } else {
            jstring text = (*env)->NewStringUTF(env, message != NULL ? message : "");
            if (text != NULL) {
                (*env)->CallVoidMethod(env, activity->clazz, method, text);
                (*env)->DeleteLocalRef(env, text);
                result = true;
            }
        }
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        result = false;
    }
    if (clazz != NULL) (*env)->DeleteLocalRef(env, clazz);
    if (detach) (*activity->vm)->DetachCurrentThread(activity->vm);
    return result;
}

void steam_frame_show_startup_error(const char *message) {
    call_activity("showSteamFrameStartupErrorFromNative", message, false);
}

void steam_frame_show_audio_error(void) {
    call_activity("showSteamFrameAudioErrorFromNative", "", false);
}

bool quest_android_require_microphone_permission(void) {
    return call_activity("requestSteamFrameMicrophonePermissionFromNative", NULL, true);
}

bool steam_frame_report_rom_result(int attempt, bool accepted, const char *message) {
    ANativeActivity *activity = quest_android_get_activity();
    if (activity == NULL || activity->vm == NULL || activity->clazz == NULL) return false;
    JNIEnv *env = NULL;
    bool detach = false;
    jint state = (*activity->vm)->GetEnv(activity->vm, (void **)&env, JNI_VERSION_1_6);
    if (state == JNI_EDETACHED) {
        if ((*activity->vm)->AttachCurrentThread(activity->vm, &env, NULL) != JNI_OK) return false;
        detach = true;
    } else if (state != JNI_OK || env == NULL) {
        return false;
    }
    jclass clazz = (*env)->GetObjectClass(env, activity->clazz);
    jmethodID method = clazz == NULL ? NULL : (*env)->GetMethodID(env, clazz,
        "onSteamFrameRomValidated", "(IZLjava/lang/String;)V");
    jstring text = method == NULL ? NULL : (*env)->NewStringUTF(env, message ? message : "");
    bool result = text != NULL;
    if (result) (*env)->CallVoidMethod(env, activity->clazz, method, (jint)attempt,
                                     accepted ? JNI_TRUE : JNI_FALSE, text);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        result = false;
    }
    if (text != NULL) (*env)->DeleteLocalRef(env, text);
    if (clazz != NULL) (*env)->DeleteLocalRef(env, clazz);
    if (detach) (*activity->vm)->DetachCurrentThread(activity->vm);
    return result;
}
