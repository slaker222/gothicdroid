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
#include <cstdarg>
#include <ctime>
#include <sys/time.h>
#include <system/api/androidapi.h>
#include <Tempest/Log>

#define LOG_TAG "OpenGothic"
void androidAppendLog(const char* text);
// logcat + opengothic_log.txt ("[jni]")
static void jniLog(int prio, const char* fmt, ...) __attribute__((format(printf,2,3)));
static void jniLog(int prio, const char* fmt, ...) {
  char buf[600];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  __android_log_print(prio, LOG_TAG, "%s", buf);
  std::string line = std::string(prio>=ANDROID_LOG_ERROR ? "[jni] ERROR " : "[jni] ") + buf;
  androidAppendLog(line.c_str());
  }
#define LOGI(...) jniLog(ANDROID_LOG_INFO,  __VA_ARGS__)
#define LOGE(...) jniLog(ANDROID_LOG_ERROR, __VA_ARGS__)

extern int opengothic_main(int argc, const char** argv);

// settings of the web page, shared with the browser build (game/mainwindow.cpp)
extern "C" int og_setting_get(const char* name);
extern "C" int og_setting_set(const char* name, int val);
extern "C" int og_barrier_zone(int kind);
extern "C" int og_ui_state();
extern "C" int og_frames();
extern "C" const char* og_perf_info();
extern "C" const char* og_frame_times();
extern "C" void og_quick(int kind);
extern "C" int og_autosave();
extern "C" int og_save_busy();
extern "C" const char* og_quests();
extern "C" const char* og_video_take();
extern "C" void og_video_done();
extern "C" const char* og_quickslots();
extern "C" const char* og_potslots();
extern "C" void og_quickpanel(int open, int first);
extern "C" void og_pointer(int type, int x, int y);
extern "C" void og_input_look(int dx, int dy);
extern "C" void og_input_zoom(int steps);

static pthread_t         s_gameThread = 0;
static std::mutex        s_gameMutex;
static std::string       s_gameDir;
static bool              s_glRenderer = false; // the OpenGL ES renderer is chosen (-gl)
static int               s_loadSlot = -1;   // >=0: the game starts with -save <slot> (the autosave after the process was killed)
static std::string       s_skyArg;   // folder with the sky pack (g1/, g2/) for the sky setting (OG_SKY_PACK_DIR)
static int               s_gameVersion = 0;   // 1/2: -g1/-g2 from the launcher (it knows the game), 0 - the engine guesses by the files
static std::string       s_modArg;   // -game:<ini> of a mod (Archolos), set by the activity before the surface
static std::mutex        s_logMutex;
static std::string       s_recentLog;
static std::string       s_logFilePath = "/sdcard/Gothic2/opengothic_log.txt";
static std::atomic_bool  s_gameRunning{false};
static FILE*             s_logFile = nullptr; // opengothic_log.txt, open while the game runs
static bool              s_logPathSet = false; // the game folder is known: before that lines wait in s_earlyLog
static std::string       s_earlyLog;           // lines of the activity/page before the game thread opened the file

// "HH:MM:SS.mmm " wall clock
static void logStamp(char* out, size_t n) {
  timeval tv; gettimeofday(&tv, nullptr);
  tm t; time_t sec = tv.tv_sec; localtime_r(&sec, &t);
  snprintf(out, n, "%02d:%02d:%02d.%03d ", t.tm_hour, t.tm_min, t.tm_sec, int(tv.tv_usec/1000));
  }

void androidAppendLog(const char* text) {
  if(text == nullptr || text[0] == '\0')
    return;
  char stamp[24];
  logStamp(stamp, sizeof(stamp));
  std::lock_guard<std::mutex> lk(s_logMutex);
  if(s_recentLog.size() > 400000) {
    s_recentLog.erase(0, 100000);
  }
  s_recentLog += stamp;
  s_recentLog += text;
  s_recentLog += "\n";

  if(!s_logPathSet) {
    // the folder of the game is not known yet: the game thread writes this to the file when it opens it
    if(s_earlyLog.size() < 100000) {
      s_earlyLog += stamp;
      s_earlyLog += text;
      s_earlyLog += "\n";
    }
    return;
  }
  // the file stays open: an open and a close for every line went through the FUSE of the shared storage (a few lines every
  // ten frames, ~ms of the game thread); fflush hands every line to the kernel as fclose did
  if(s_logFile == nullptr)
    s_logFile = fopen(s_logFilePath.c_str(), "a");
  if(s_logFile != nullptr) {
    fputs(stamp, s_logFile);
    fputs(text, s_logFile);
    fputc('\n', s_logFile);
    fflush(s_logFile);
  }
}

#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>
#include <memory>

static void runGameThread(std::string gameDir) {
  s_gameRunning.store(true);
  opengothic_debug_set_game_dir(gameDir.c_str()); // debug.cfg and logs of this game, not of the first Gothic2 folder
  {
    std::lock_guard<std::mutex> lk(s_logMutex);
    if(s_logFile != nullptr) {
      fclose(s_logFile);
      s_logFile = nullptr;
    }
    s_logFilePath = gameDir + "/opengothic_log.txt";
    FILE* f = fopen(s_logFilePath.c_str(), "w");
    if(f) {
      if(!s_earlyLog.empty())
        fputs(s_earlyLog.c_str(), f); // what the activity logged before
      fclose(f);
    }
    s_earlyLog.clear();
    s_logPathSet = true;
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
  if(Tempest::Log::isVerbose())
    androidAppendLog("Full log: ON (side menu, Logs)");
  androidAppendLog(("Game directory: " + gameDir).c_str());
  androidAppendLog(("Saves directory: " + savesDir).c_str());

  Tempest::Log::setOutputCallback([](Tempest::Log::Mode mode, const char* text) {
    (void)mode;
    // the per-frame timing lines only in the full log (logcat keeps them anyway: Log prints there itself)
    if(!Tempest::Log::isVerbose() && text[0]=='[' && (strncmp(text,"[PERF",5)==0 || strncmp(text,"[SWAP_PERF",10)==0 ||
                                                       strncmp(text,"[DRAW_PERF",10)==0 || strncmp(text,"[GPU_PASS",9)==0))
      return;
    androidAppendLog(text);

  });

  if(!s_skyArg.empty())
    setenv("OG_SKY_PACK_DIR", s_skyArg.c_str(), 1);
  else
    unsetenv("OG_SKY_PACK_DIR");
  std::vector<std::string> args = {
    "Gothic2Notr",
    "-g",
    gameDir
  };
  if(s_gameVersion==1 || s_gameVersion==2) {
    // -g2 = Gothic 2 Night of the Raven, as the web page passes it
    args.push_back(s_gameVersion==1 ? "-g1" : "-g2");
    androidAppendLog(s_gameVersion==1 ? "Game: Gothic 1 (-g1)" : "Game: Gothic 2 (-g2)");
  }
  if(!s_modArg.empty()) {
    // the engine looks for the ini in <game>/system itself (CommandLine: nestedPath({"system", name})): only the file name goes there,
    // "system/x.ini" made <game>/system/system/x.ini, the ini was not found and the mod's archives were not filtered by its language
    std::string modIni = s_modArg;
    const size_t slash = modIni.find_last_of("/\\");
    if(slash != std::string::npos)
      modIni = modIni.substr(slash + 1);
    args.push_back("-game:" + modIni);
    androidAppendLog(("Mod: " + modIni).c_str());
  }

  if(s_glRenderer) {
    args.push_back("-gl");
    androidAppendLog("Renderer: OpenGL ES (-gl)");
  }
  if(s_loadSlot >= 0) {
    args.push_back("-save");
    args.push_back(std::to_string(s_loadSlot));
    androidAppendLog(("Load the autosave: slot " + std::to_string(s_loadSlot)).c_str());
  }

  // debugging: more command line arguments (-nomenu, ...) from <game dir>/.og_args, split by blanks
  if(FILE* f = fopen((gameDir + "/.og_args").c_str(), "r")) {
    char buf[512] = {};
    const size_t n = fread(buf, 1, sizeof(buf)-1, f);
    fclose(f);
    std::string cur;
    for(size_t i=0; i<=n; ++i) {
      const char c = i<n ? buf[i] : ' ';
      if(c==' ' || c=='\n' || c=='\r' || c=='\t') {
        if(!cur.empty()) {
          args.push_back(cur);
          androidAppendLog(("Arg: " + cur).c_str());
          cur.clear();
        }
      } else {
        cur += c;
      }
    }
  }

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
Java_com_opengothic_GameActivity_nativeSetDensity(JNIEnv*, jobject, jfloat density) {
  Tempest::AndroidApi::setDensity(density);
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

// The log holds text in the codepage of the game (cp1251 ...), and NewStringUTF needs valid modified UTF-8
// (CheckJNI aborts otherwise): keep valid 1-3 byte sequences, replace everything else with '?'.
static std::string sanitizeUtf8(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  const size_t n = in.size();
  size_t i = 0;
  while(i<n) {
    const unsigned char c = static_cast<unsigned char>(in[i]);
    size_t len = 0;
    if(c>=0x01 && c<0x80)      len = 1;
    else if(c>=0xC2 && c<0xE0) len = 2;
    else if(c>=0xE0 && c<0xF0) len = 3;
    bool ok = len>0 && i+len<=n;
    for(size_t k=1; ok && k<len; ++k)
      ok = (static_cast<unsigned char>(in[i+k])&0xC0)==0x80;
    if(ok && len==3) {
      const unsigned char c1 = static_cast<unsigned char>(in[i+1]);
      if((c==0xE0 && c1<0xA0) || (c==0xED && c1>=0xA0))
        ok = false; // overlong / surrogates
      }
    if(ok) {
      out.append(in, i, len);
      i += len;
      } else {
      out += '?';
      ++i;
      }
    }
  return out;
}

static jstring lastLogString(JNIEnv* env) {
  std::string copy;
  {
    std::lock_guard<std::mutex> lk(s_logMutex);
    copy = sanitizeUtf8(s_recentLog);
  }
  return env->NewStringUTF(copy.c_str());
}

JNIEXPORT jstring JNICALL
Java_com_opengothic_MainActivity_nativeGetLastLog(JNIEnv* env, jobject) {
  return lastLogString(env);
}

JNIEXPORT jstring JNICALL
Java_com_opengothic_GameActivity_nativeGetLastLog(JNIEnv* env, jobject) {
  return lastLogString(env);
}

JNIEXPORT jboolean JNICALL
Java_com_opengothic_MainActivity_nativeIsGameRunning(JNIEnv*, jobject) {
  return static_cast<jboolean>(s_gameRunning.load());
}

// the game asked to quit (menu "Exit"): the activity then goes back to the app's main menu
JNIEXPORT jboolean JNICALL
Java_com_opengothic_GameActivity_nativeExitRequested(JNIEnv*, jobject) {
  return static_cast<jboolean>(Tempest::AndroidApi::exitRequested());
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

JNIEXPORT void JNICALL
Java_com_opengothic_GameActivity_nativeSetMod(JNIEnv* env, jobject, jstring jMod) {
  const char* c = env->GetStringUTFChars(jMod, nullptr);
  s_modArg = c;
  env->ReleaseStringUTFChars(jMod, c);
}

// the side menu's "Logs" switch: Tempest::Log::v lines (files, saves, Vulkan, game resources) go to opengothic_log.txt too
JNIEXPORT void JNICALL
Java_com_opengothic_GameActivity_nativeSetVerboseLog(JNIEnv*, jobject, jboolean on) {
  Tempest::Log::setVerbose(on!=JNI_FALSE);
  if(s_gameRunning.load())
    androidAppendLog(on ? "=== full log: ON ===" : "=== full log: OFF ===");
}

// lines of the activity ("[app] ...") and of the page ("[ui] ...") into opengothic_log.txt
JNIEXPORT void JNICALL
Java_com_opengothic_GameActivity_nativeAppendLog(JNIEnv* env, jobject, jstring jText) {
  if(jText==nullptr) return;
  const char* c = env->GetStringUTFChars(jText, nullptr);
  if(c) { androidAppendLog(c); env->ReleaseStringUTFChars(jText, c); }
}

JNIEXPORT void JNICALL
Java_com_opengothic_GameActivity_nativeSetGameVersion(JNIEnv*, jobject, jint version) {
  s_gameVersion = version;
}

JNIEXPORT void JNICALL
Java_com_opengothic_GameActivity_nativeSetSkyDir(JNIEnv* env, jobject, jstring jDir) {
  const char* c = env->GetStringUTFChars(jDir, nullptr);
  s_skyArg = c;
  env->ReleaseStringUTFChars(jDir, c);
}

JNIEXPORT jint JNICALL
Java_com_opengothic_GameActivity_nativeFrames(JNIEnv*, jobject) { return og_frames(); }

JNIEXPORT jstring JNICALL
Java_com_opengothic_GameActivity_nativePerfInfo(JNIEnv* env, jobject) { return env->NewStringUTF(og_perf_info()); }

JNIEXPORT jstring JNICALL
Java_com_opengothic_GameActivity_nativeFrameTimes(JNIEnv* env, jobject) { return env->NewStringUTF(og_frame_times()); }

JNIEXPORT jint JNICALL
Java_com_opengothic_GameActivity_nativeUiState(JNIEnv*, jobject) { return og_ui_state(); }

JNIEXPORT void JNICALL
Java_com_opengothic_GameActivity_nativeSetLoadSlot(JNIEnv*, jobject, jint slot) { s_loadSlot = slot; }
JNIEXPORT void JNICALL
Java_com_opengothic_GameActivity_nativeSetRenderer(JNIEnv*, jobject, jboolean gl) { s_glRenderer = gl; }

JNIEXPORT jint JNICALL
Java_com_opengothic_GameActivity_nativeAutoSave(JNIEnv*, jobject) { return og_autosave(); }

JNIEXPORT jint JNICALL
Java_com_opengothic_GameActivity_nativeSaveBusy(JNIEnv*, jobject) { return og_save_busy(); }

JNIEXPORT void JNICALL
Java_com_opengothic_GameActivity_nativeQuick(JNIEnv*, jobject, jint kind) { og_quick(kind); }

JNIEXPORT jstring JNICALL
Java_com_opengothic_GameActivity_nativeQuests(JNIEnv* env, jobject) { return env->NewStringUTF(og_quests()); }

JNIEXPORT jstring JNICALL
Java_com_opengothic_GameActivity_nativeQuickSlots(JNIEnv* env, jobject) { return env->NewStringUTF(og_quickslots()); }

JNIEXPORT jstring JNICALL
Java_com_opengothic_GameActivity_nativePotSlots(JNIEnv* env, jobject) { return env->NewStringUTF(og_potslots()); }

JNIEXPORT void JNICALL
Java_com_opengothic_GameActivity_nativeQuickPanel(JNIEnv*, jobject, jint open, jint first) { og_quickpanel(open, first); }

JNIEXPORT void JNICALL
Java_com_opengothic_GameActivity_nativePointer(JNIEnv*, jobject, jint type, jint x, jint y) { og_pointer(type, x, y); }

JNIEXPORT void JNICALL
Java_com_opengothic_GameActivity_nativeLook(JNIEnv*, jobject, jint dx, jint dy) { og_input_look(dx, dy); }

JNIEXPORT void JNICALL
Java_com_opengothic_GameActivity_nativeZoom(JNIEnv*, jobject, jint steps) { og_input_zoom(steps); }

JNIEXPORT void JNICALL
Java_com_opengothic_GameActivity_nativeWebKey(JNIEnv* env, jobject, jint action, jstring jCode) {
  const char* c = env->GetStringUTFChars(jCode, nullptr);
  Tempest::AndroidApi::pushWebKey(action, c);
  env->ReleaseStringUTFChars(jCode, c);
}

JNIEXPORT jstring JNICALL
Java_com_opengothic_GameActivity_nativeVideoTake(JNIEnv* env, jobject) { return env->NewStringUTF(og_video_take()); }

JNIEXPORT void JNICALL
Java_com_opengothic_GameActivity_nativeVideoDone(JNIEnv*, jobject) { og_video_done(); }

} // extern "C"

#endif
