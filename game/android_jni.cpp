#if defined(__ANDROID__)
#include "gapi/vulkan/vpipelinelogger.h"
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

// settings of the web page, shared with the browser build (game/mainwindow.cpp)
extern "C" int og_setting_get(const char* name);
extern "C" int og_setting_set(const char* name, int val);
extern "C" int og_barrier_zone(int kind);
extern "C" int og_ui_state();
extern "C" int og_frames();
extern "C" void og_quick(int kind);
extern "C" const char* og_quests();
extern "C" const char* og_quickslots();
extern "C" void og_quickpanel(int open, int first);
extern "C" void og_pointer(int type, int x, int y);
extern "C" void og_input_look(int dx, int dy);

static pthread_t         s_gameThread = 0;
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
#include <pthread.h>
#include <memory>

static void runGameThread(std::string gameDir) {
  s_gameRunning.store(true);
  opengothic_debug_set_game_dir(gameDir.c_str()); // debug.cfg and logs of this game, not of the first Gothic2 folder
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
  s_gameThread = 0;
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
  if(s_gameThread == 0 && window != nullptr) {
    if(jGameDir != nullptr) {
      const char* utf = env->GetStringUTFChars(jGameDir, nullptr);
      s_gameDir = utf;
      env->ReleaseStringUTFChars(jGameDir, utf);
    } else {
      s_gameDir = "/sdcard/Gothic2";
    }

    LOGI("Launching OpenGothic thread with dir: %s", s_gameDir.c_str());
    // explicit large stack: opengothic_main early call-chain (VulkanApi init + deviceProps)
    // overflows the Android default thread stack
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 32*1024*1024);
    pthread_t t = 0;
    if(pthread_create(&t, &attr, [](void* arg) -> void* {
         std::unique_ptr<std::string> dir(static_cast<std::string*>(arg));
         runGameThread(*dir);
         return nullptr;
       }, new std::string(s_gameDir)) == 0) {
      s_gameThread = t;
      pthread_detach(t);
      } else {
      LOGE("pthread_create failed");
      }
    pthread_attr_destroy(&attr);
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

JNIEXPORT jint JNICALL
Java_com_opengothic_GameActivity_nativeSettingGet(JNIEnv* env, jobject, jstring jName) {
  const char* n = env->GetStringUTFChars(jName, nullptr);
  const int   v = og_setting_get(n);
  env->ReleaseStringUTFChars(jName, n);
  return v;
}

JNIEXPORT jint JNICALL
Java_com_opengothic_GameActivity_nativeSettingSet(JNIEnv* env, jobject, jstring jName, jint val) {
  const char* n = env->GetStringUTFChars(jName, nullptr);
  const int   v = og_setting_set(n, val);
  env->ReleaseStringUTFChars(jName, n);
  return v;
}

JNIEXPORT jint JNICALL
Java_com_opengothic_GameActivity_nativeBarrierZone(JNIEnv*, jobject, jint kind) {
  return og_barrier_zone(kind);
}

JNIEXPORT jint JNICALL
Java_com_opengothic_GameActivity_nativeFrames(JNIEnv*, jobject) { return og_frames(); }

JNIEXPORT jint JNICALL
Java_com_opengothic_GameActivity_nativeUiState(JNIEnv*, jobject) { return og_ui_state(); }

JNIEXPORT void JNICALL
Java_com_opengothic_GameActivity_nativeQuick(JNIEnv*, jobject, jint kind) { og_quick(kind); }

JNIEXPORT jstring JNICALL
Java_com_opengothic_GameActivity_nativeQuests(JNIEnv* env, jobject) { return env->NewStringUTF(og_quests()); }

JNIEXPORT jstring JNICALL
Java_com_opengothic_GameActivity_nativeQuickSlots(JNIEnv* env, jobject) { return env->NewStringUTF(og_quickslots()); }

JNIEXPORT void JNICALL
Java_com_opengothic_GameActivity_nativeQuickPanel(JNIEnv*, jobject, jint open, jint first) { og_quickpanel(open, first); }

JNIEXPORT void JNICALL
Java_com_opengothic_GameActivity_nativePointer(JNIEnv*, jobject, jint type, jint x, jint y) { og_pointer(type, x, y); }

JNIEXPORT void JNICALL
Java_com_opengothic_GameActivity_nativeLook(JNIEnv*, jobject, jint dx, jint dy) { og_input_look(dx, dy); }

JNIEXPORT void JNICALL
Java_com_opengothic_GameActivity_nativeWebKey(JNIEnv* env, jobject, jint action, jstring jCode) {
  const char* c = env->GetStringUTFChars(jCode, nullptr);
  Tempest::AndroidApi::pushWebKey(action, c);
  env->ReleaseStringUTFChars(jCode, c);
}

} // extern "C"

#endif
