#include <android/native_activity.h>
#include <jni.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

ANativeActivity *quest_android_get_activity(void);

static pthread_mutex_t sSpeechMutex = PTHREAD_MUTEX_INITIALIZER;
static char sSpeechResult[512];
static bool sSpeechResultReady;
static bool sSpeechListening;

bool quest_speech_recognition_start(void) {
    ANativeActivity *activity = quest_android_get_activity();
    if (activity == NULL || activity->vm == NULL || activity->clazz == NULL) return false;
    JNIEnv *env = NULL;
    bool detach = false;
    jint status = (*activity->vm)->GetEnv(activity->vm, (void **)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*activity->vm)->AttachCurrentThread(activity->vm, &env, NULL) != JNI_OK) return false;
        detach = true;
    } else if (status != JNI_OK || env == NULL) {
        return false;
    }
    jclass clazz = (*env)->GetObjectClass(env, activity->clazz);
    jmethodID method = clazz == NULL ? NULL : (*env)->GetMethodID(
        env, clazz, "startSpeechRecognitionFromNative", "()V");
    bool started = method != NULL;
    if (started) {
        pthread_mutex_lock(&sSpeechMutex);
        sSpeechListening = true;
        pthread_mutex_unlock(&sSpeechMutex);
        (*env)->CallVoidMethod(env, activity->clazz, method);
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            pthread_mutex_lock(&sSpeechMutex);
            sSpeechListening = false;
            pthread_mutex_unlock(&sSpeechMutex);
            started = false;
        }
    }
    if (clazz != NULL) (*env)->DeleteLocalRef(env, clazz);
    if (detach) (*activity->vm)->DetachCurrentThread(activity->vm);
    return started;
}

bool quest_speech_recognition_poll(char *text, size_t textSize) {
    if (text == NULL || textSize == 0) return false;
    pthread_mutex_lock(&sSpeechMutex);
    bool ready = sSpeechResultReady;
    if (ready) {
        strncpy(text, sSpeechResult, textSize - 1);
        text[textSize - 1] = '\0';
        sSpeechResult[0] = '\0';
        sSpeechResultReady = false;
    }
    pthread_mutex_unlock(&sSpeechMutex);
    return ready;
}

bool quest_speech_recognition_is_listening(void) {
    pthread_mutex_lock(&sSpeechMutex);
    bool listening = sSpeechListening;
    pthread_mutex_unlock(&sSpeechMutex);
    return listening;
}

JNIEXPORT void JNICALL
Java_com_fulldivegames_sm64coopdxvr_QuestNativeActivity_nativeOnSpeechRecognitionResult(
        JNIEnv *env, jclass clazz, jstring result) {
    (void)clazz;
    if (result == NULL) return;
    const char *utf8 = (*env)->GetStringUTFChars(env, result, NULL);
    if (utf8 == NULL) return;
    pthread_mutex_lock(&sSpeechMutex);
    strncpy(sSpeechResult, utf8, sizeof(sSpeechResult) - 1);
    sSpeechResult[sizeof(sSpeechResult) - 1] = '\0';
    sSpeechResultReady = sSpeechResult[0] != '\0';
    pthread_mutex_unlock(&sSpeechMutex);
    (*env)->ReleaseStringUTFChars(env, result, utf8);
}

JNIEXPORT void JNICALL
Java_com_fulldivegames_sm64coopdxvr_QuestNativeActivity_nativeOnSpeechRecognitionState(
        JNIEnv *env, jclass clazz, jboolean listening) {
    (void)env; (void)clazz;
    pthread_mutex_lock(&sSpeechMutex);
    sSpeechListening = listening == JNI_TRUE;
    pthread_mutex_unlock(&sSpeechMutex);
}
