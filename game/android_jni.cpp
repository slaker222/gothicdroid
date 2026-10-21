#if defined(__ANDROID__)
#include <jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <string>
#include <thread>
#include <mutex>
#include <system/api/androidapi.h>

#define LOG_TAG "OpenGothic"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern int opengothic_main(int argc, const char** argv);

static std::thread* s_gameThread = nullptr;
static std::mutex   s_gameMutex;
static std::string  s_gameDir;

static void runGameThread(std::string gameDir) {
  LOGI("OpenGothic native game thread started with game dir: %s", gameDir.c_str());

  std::vector<std::string> args = {
    "Gothic2Notr",
    "-g",
    gameDir
  };

  std::vector<const char*> argv;
  for(const auto& a : args) {
    argv.push_back(a.c_str());
  }

  try {
    int ret = opengothic_main(static_cast<int>(argv.size()), argv.data());
    LOGI("opengothic_main finished with code: %d", ret);
  } catch(const std::exception& e) {
    LOGE("opengothic_main exception: %s", e.what());
  } catch(...) {
    LOGE("opengothic_main unhandled exception");
  }
}

extern "C" {

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
  (void)vm;
  (void)reserved;
  LOGI("libopengothic loaded");
  return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL
Java_com_opengothic_MainActivity_nativeSetSurface(JNIEnv* env, jobject /*thiz*/, jobject surface, jstring jGameDir) {
  ANativeWindow* window = nullptr;
  if(surface != nullptr) {
    window = ANativeWindow_fromSurface(env, surface);
    LOGI("nativeSetSurface: obtained ANativeWindow %p", window);
  } else {
    LOGI("nativeSetSurface: surface is null");
  }

  Tempest::AndroidApi::setNativeWindow(window);

  std::lock_guard<std::mutex> lk(s_gameMutex);
  if(s_gameThread == nullptr && window != nullptr) {
    if(jGameDir != nullptr) {
      const char* utf = env->GetStringUTFChars(jGameDir, nullptr);
      s_gameDir = utf;
      env->ReleaseStringUTFChars(jGameDir, utf);
    } else {
      s_gameDir = "/sdcard/Gothic2";
    }

    LOGI("Launching OpenGothic thread with dir: %s", s_gameDir.c_str());
    s_gameThread = new std::thread(runGameThread, s_gameDir);
  }
}

JNIEXPORT void JNICALL
Java_com_opengothic_MainActivity_nativeSurfaceChanged(JNIEnv* /*env*/, jobject /*thiz*/, jint width, jint height) {
  Tempest::AndroidApi::notifySurfaceChanged(width, height);
}

JNIEXPORT void JNICALL
Java_com_opengothic_MainActivity_nativeDestroySurface(JNIEnv* /*env*/, jobject /*thiz*/) {
  LOGI("nativeDestroySurface");
  Tempest::AndroidApi::setNativeWindow(nullptr);
}

JNIEXPORT void JNICALL
Java_com_opengothic_MainActivity_nativeOnTouch(JNIEnv* /*env*/, jobject /*thiz*/, jint action, jfloat x, jfloat y, jint id) {
  Tempest::AndroidApi::pushTouchEvent(action, x, y, id);
}

JNIEXPORT void JNICALL
Java_com_opengothic_MainActivity_nativeOnKey(JNIEnv* /*env*/, jobject /*thiz*/, jint action, jint keyCode) {
  Tempest::AndroidApi::pushKeyEvent(action, keyCode);
}

} // extern "C"

#endif
