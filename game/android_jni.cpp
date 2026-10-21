#if defined(__ANDROID__)
#include <jni.h>
#include <android/log.h>

#define LOG_TAG "OpenGothic"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    (void)vm;
    (void)reserved;
    LOGI("libopengothic loaded");
    return JNI_VERSION_1_6;
}

#endif
