#if defined(__ANDROID__)
#include <jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <cstdio>
#include <system/api/androidapi.h>
#include <Tempest/Log>

#define LOG_TAG "OpenGothic"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern int opengothic_main(int argc, const char** argv);

static std::thread*      s_gameThread = nullptr;
static std::mutex        s_gameMutex;
static std::string       s_gameDir;
static std::mutex        s_logMutex;
static std::string       s_recentLog;
static std::string       s_logFilePath = "/sdcard/Gothic2/opengothic_log.txt";
static std::atomic_bool  s_gameRunning{false};

void androidAppendLog(const char* text) {
  if(text == nullptr || text[0] == '\0')
    return;
  std::lock_guard<std::mutex> lk(s_logMutex);
  if(s_recentLog.size() > 400000) {
    s_recentLog.erase(0, 100000);
  }
  s_recentLog += text;
  s_recentLog += "\n";

  FILE* f = fopen(s_logFilePath.c_str(), "a");
  if(f != nullptr) {
    fputs(text, f);
    fputc('\n', f);
    fclose(f);
  }
}

#include <unistd.h>
#include <sys/stat.h>

static void runGameThread(std::string gameDir) {
  s_gameRunning.store(true);
  s_logFilePath = gameDir + "/opengothic_log.txt";
  {
    FILE* f = fopen(s_logFilePath.c_str(), "w");
    if(f) fclose(f);
  }

  // Switch process working directory to game directory so relative paths (save_slot_*.sav, Gothic.ini)
  // are resolved directly in /sdcard/Gothic2.
  if(chdir(gameDir.c_str()) == 0) {
    LOGI("Changed working directory to: %s", gameDir.c_str());
  } else {
    LOGE("Failed to chdir to: %s", gameDir.c_str());
  }

  // Ensure saves directory exists
  std::string savesDir = gameDir + "/saves";
  mkdir(savesDir.c_str(), 0777);

  LOGI("OpenGothic native game thread started with game dir: %s", gameDir.c_str());
  androidAppendLog("=== OpenGothic Android Session Start ===");
  androidAppendLog(("Game directory: " + gameDir).c_str());
  androidAppendLog(("Saves directory: " + savesDir).c_str());

  Tempest::Log::setOutputCallback([](Tempest::Log::Mode mode, const char* text) {
    (void)mode;
    androidAppendLog(text);

  });

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
    androidAppendLog(("opengothic_main exited with code: " + std::to_string(ret)).c_str());
  } catch(const std::exception& e) {
    LOGE("opengothic_main exception: %s", e.what());
    androidAppendLog(("FATAL EXCEPTION: " + std::string(e.what())).c_str());
  } catch(...) {
    LOGE("opengothic_main unhandled exception");
    androidAppendLog("FATAL UNKNOWN EXCEPTION (check crash.log)");
  }

  s_gameRunning.store(false);
  std::lock_guard<std::mutex> lk(s_gameMutex);
  s_gameThread = nullptr;
}

extern "C" {

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
  (void)vm;
  (void)reserved;
  LOGI("libopengothic loaded");
  return JNI_VERSION_1_6;
}

static void implSetSurface(JNIEnv* env, jobject surface, jstring jGameDir) {
  ANativeWindow* window = nullptr;
  if(surface != nullptr) {
    window = ANativeWindow_fromSurface(env, surface);
    LOGI("implSetSurface: obtained ANativeWindow %p", window);
  } else {
    LOGI("implSetSurface: surface is null");
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
Java_com_opengothic_MainActivity_nativeSetSurface(JNIEnv* env, jobject, jobject surface, jstring jGameDir) {
  implSetSurface(env, surface, jGameDir);
}

JNIEXPORT void JNICALL
Java_com_opengothic_GameActivity_nativeSetSurface(JNIEnv* env, jobject, jobject surface, jstring jGameDir) {
  implSetSurface(env, surface, jGameDir);
}

JNIEXPORT void JNICALL
Java_com_opengothic_MainActivity_nativeSurfaceChanged(JNIEnv*, jobject, jint width, jint height) {
  Tempest::AndroidApi::notifySurfaceChanged(width, height);
}

JNIEXPORT void JNICALL
Java_com_opengothic_GameActivity_nativeSurfaceChanged(JNIEnv*, jobject, jint width, jint height) {
  Tempest::AndroidApi::notifySurfaceChanged(width, height);
}

JNIEXPORT void JNICALL
Java_com_opengothic_MainActivity_nativeDestroySurface(JNIEnv*, jobject) {
  LOGI("nativeDestroySurface");
  Tempest::AndroidApi::setNativeWindow(nullptr);
}

JNIEXPORT void JNICALL
Java_com_opengothic_GameActivity_nativeDestroySurface(JNIEnv*, jobject) {
  LOGI("nativeDestroySurface");
  Tempest::AndroidApi::setNativeWindow(nullptr);
}

JNIEXPORT void JNICALL
Java_com_opengothic_MainActivity_nativeOnTouch(JNIEnv*, jobject, jint action, jfloat x, jfloat y, jint id) {
  Tempest::AndroidApi::pushTouchEvent(action, x, y, id);
}

JNIEXPORT void JNICALL
Java_com_opengothic_GameActivity_nativeOnTouch(JNIEnv*, jobject, jint action, jfloat x, jfloat y, jint id) {
  Tempest::AndroidApi::pushTouchEvent(action, x, y, id);
}

JNIEXPORT void JNICALL
Java_com_opengothic_MainActivity_nativeOnKey(JNIEnv*, jobject, jint action, jint keyCode) {
  Tempest::AndroidApi::pushKeyEvent(action, keyCode);
}

JNIEXPORT void JNICALL
Java_com_opengothic_GameActivity_nativeOnKey(JNIEnv*, jobject, jint action, jint keyCode) {
  Tempest::AndroidApi::pushKeyEvent(action, keyCode);
}

JNIEXPORT jstring JNICALL
Java_com_opengothic_MainActivity_nativeGetLastLog(JNIEnv* env, jobject) {
  std::lock_guard<std::mutex> lk(s_logMutex);
  return env->NewStringUTF(s_recentLog.c_str());
}

JNIEXPORT jstring JNICALL
Java_com_opengothic_GameActivity_nativeGetLastLog(JNIEnv* env, jobject) {
  std::lock_guard<std::mutex> lk(s_logMutex);
  return env->NewStringUTF(s_recentLog.c_str());
}

JNIEXPORT jboolean JNICALL
Java_com_opengothic_MainActivity_nativeIsGameRunning(JNIEnv*, jobject) {
  return static_cast<jboolean>(s_gameRunning.load());
}

JNIEXPORT jboolean JNICALL
Java_com_opengothic_GameActivity_nativeIsGameRunning(JNIEnv*, jobject) {
  return static_cast<jboolean>(s_gameRunning.load());
}

} // extern "C"

#endif
