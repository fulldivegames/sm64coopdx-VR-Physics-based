#include "quest_text_input.h"

#include <jni.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "pc/djui/djui_inputbox.h"
#include "pc/djui/djui_interactable.h"

#define QUEST_TEXT_CAPACITY 4096

static JavaVM *sVm;
static jobject sActivity;
static jmethodID sShowKeyboard;
static jmethodID sHideKeyboard;
static jmethodID sGetClipboard;
static jmethodID sSetClipboard;
static pthread_mutex_t sTextMutex = PTHREAD_MUTEX_INITIALIZER;
static char sPendingText[QUEST_TEXT_CAPACITY];
static bool sTextPending;
static bool sDonePending;
static char sClipboard[QUEST_TEXT_CAPACITY];

static JNIEnv *quest_text_env(bool *attached) {
    *attached = false;
    if (sVm == NULL) return NULL;
    JNIEnv *env = NULL;
    const jint state = (*sVm)->GetEnv(sVm, (void **)&env, JNI_VERSION_1_6);
    if (state == JNI_OK) return env;
    if (state != JNI_EDETACHED ||
        (*sVm)->AttachCurrentThread(sVm, &env, NULL) != JNI_OK) return NULL;
    *attached = true;
    return env;
}

static void quest_text_detach(bool attached) {
    if (attached && sVm != NULL) (*sVm)->DetachCurrentThread(sVm);
}

void quest_text_input_initialize(ANativeActivity *activity) {
    if (activity == NULL || activity->vm == NULL || activity->clazz == NULL) return;
    bool attached = false;
    sVm = activity->vm;
    JNIEnv *env = quest_text_env(&attached);
    if (env == NULL) return;
    sActivity = (*env)->NewGlobalRef(env, activity->clazz);
    jclass cls = (*env)->GetObjectClass(env, activity->clazz);
    if (sActivity != NULL && cls != NULL) {
        sShowKeyboard = (*env)->GetMethodID(env, cls, "showQuestKeyboard", "(Ljava/lang/String;I)V");
        sHideKeyboard = (*env)->GetMethodID(env, cls, "hideQuestKeyboard", "()V");
        sGetClipboard = (*env)->GetMethodID(env, cls, "getQuestClipboardText", "()Ljava/lang/String;");
        sSetClipboard = (*env)->GetMethodID(env, cls, "setQuestClipboardText", "(Ljava/lang/String;)V");
        (*env)->DeleteLocalRef(env, cls);
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    quest_text_detach(attached);
}

void quest_text_input_shutdown(void) {
    quest_text_input_hide();
    bool attached = false;
    JNIEnv *env = quest_text_env(&attached);
    if (env != NULL && sActivity != NULL) {
        (*env)->DeleteGlobalRef(env, sActivity);
    }
    sActivity = NULL;
    sShowKeyboard = NULL;
    sHideKeyboard = NULL;
    sGetClipboard = NULL;
    sSetClipboard = NULL;
    quest_text_detach(attached);
    sVm = NULL;
}

void quest_text_input_show(const char *initial_text, int max_length) {
    if (sActivity == NULL || sShowKeyboard == NULL) return;
    bool attached = false;
    JNIEnv *env = quest_text_env(&attached);
    if (env == NULL) return;
    jstring text = (*env)->NewStringUTF(env, initial_text != NULL ? initial_text : "");
    if (text != NULL) {
        (*env)->CallVoidMethod(env, sActivity, sShowKeyboard, text,
                              (jint)(max_length > 0 ? max_length : 1));
        (*env)->DeleteLocalRef(env, text);
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    quest_text_detach(attached);
}

void quest_text_input_hide(void) {
    if (sActivity == NULL || sHideKeyboard == NULL) return;
    bool attached = false;
    JNIEnv *env = quest_text_env(&attached);
    if (env == NULL) return;
    (*env)->CallVoidMethod(env, sActivity, sHideKeyboard);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    quest_text_detach(attached);
}

void quest_text_input_poll(void) {
    char text[QUEST_TEXT_CAPACITY];
    bool text_pending;
    bool done_pending;
    pthread_mutex_lock(&sTextMutex);
    text_pending = sTextPending;
    done_pending = sDonePending;
    snprintf(text, sizeof(text), "%s", sPendingText);
    sTextPending = false;
    sDonePending = false;
    pthread_mutex_unlock(&sTextMutex);
    // The system keyboard is asynchronous. A panel can close while its final
    // TextWatcher/Done callbacks are still queued; never apply those callbacks
    // to a later control or a destroyed input box.
    if (!djui_inputbox_has_focused()) return;
    if (text_pending) djui_inputbox_replace_focused_text(text);
    if (done_pending) {
        djui_interactable_on_key_down(SCANCODE_ENTER);
    }
}

char *quest_text_input_get_clipboard(void) {
    sClipboard[0] = '\0';
    if (sActivity == NULL || sGetClipboard == NULL) return sClipboard;
    bool attached = false;
    JNIEnv *env = quest_text_env(&attached);
    if (env == NULL) return sClipboard;
    jstring value = (jstring)(*env)->CallObjectMethod(env, sActivity, sGetClipboard);
    if (!(*env)->ExceptionCheck(env) && value != NULL) {
        const char *utf = (*env)->GetStringUTFChars(env, value, NULL);
        if (utf != NULL) {
            snprintf(sClipboard, sizeof(sClipboard), "%s", utf);
            (*env)->ReleaseStringUTFChars(env, value, utf);
        }
        (*env)->DeleteLocalRef(env, value);
    } else if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }
    quest_text_detach(attached);
    return sClipboard;
}

void quest_text_input_set_clipboard(const char *text) {
    if (sActivity == NULL || sSetClipboard == NULL) return;
    bool attached = false;
    JNIEnv *env = quest_text_env(&attached);
    if (env == NULL) return;
    jstring value = (*env)->NewStringUTF(env, text != NULL ? text : "");
    if (value != NULL) {
        (*env)->CallVoidMethod(env, sActivity, sSetClipboard, value);
        (*env)->DeleteLocalRef(env, value);
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    quest_text_detach(attached);
}

JNIEXPORT void JNICALL
Java_com_fulldivegames_sm64coopdxvr_QuestNativeActivity_nativeOnQuestTextChanged(
        JNIEnv *env, jclass cls, jstring value) {
    (void)cls;
    const char *utf = value != NULL ? (*env)->GetStringUTFChars(env, value, NULL) : NULL;
    pthread_mutex_lock(&sTextMutex);
    snprintf(sPendingText, sizeof(sPendingText), "%s", utf != NULL ? utf : "");
    sTextPending = true;
    pthread_mutex_unlock(&sTextMutex);
    if (utf != NULL) (*env)->ReleaseStringUTFChars(env, value, utf);
}

JNIEXPORT void JNICALL
Java_com_fulldivegames_sm64coopdxvr_QuestNativeActivity_nativeOnQuestKeyboardDone(
        JNIEnv *env, jclass cls) {
    (void)env;
    (void)cls;
    pthread_mutex_lock(&sTextMutex);
    sDonePending = true;
    pthread_mutex_unlock(&sTextMutex);
}
