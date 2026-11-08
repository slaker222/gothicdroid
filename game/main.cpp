#include <Tempest/Window>
#include <Tempest/Application>
#include <Tempest/Log>

#include <iostream>
#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include <zenkit/Logger.hh>

#if defined(TEMPEST_BUILD_VULKAN)
#include <Tempest/VulkanApi>
#endif

#if defined(_MSC_VER) && defined(TEMPEST_BUILD_DIRECTX12)
#include <Tempest/DirectX12Api>
#endif

#if defined(__APPLE__) && defined(TEMPEST_BUILD_METAL)
#include <Tempest/MetalApi>
#endif

#if defined(__EMSCRIPTEN__)
#include <Tempest/WebGapi>
#endif
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
#include <Tempest/WebGLApi>
#endif

#if defined(__IOS__)
#include "utils/installdetect.h"
#endif

#include "utils/crashlog.h"
#include "mainwindow.h"
#include "gothic.h"
#include "build.h"
#include "commandline.h"
#include "og_hb.h"

#include <dmusic.h>

std::string_view selectDevice(const Tempest::AbstractGraphicsApi& api) {
  auto d = api.devices();

  static Tempest::Device::Props p;
  for(auto& i:d)
    // if(i.type==Tempest::DeviceType::Integrated) {
    if(i.type==Tempest::DeviceType::Discrete) {
      p = i;
      return p.name;
      }
  if(d.size()>0) {
    p = d[0];
    return p.name;
    }
  return "";
  }

std::unique_ptr<Tempest::AbstractGraphicsApi> mkApi(const CommandLine& g) {
  Tempest::ApiFlags flg = g.isValidationMode() ? Tempest::ApiFlags::Validation : Tempest::ApiFlags::NoFlags;
  switch(g.graphicsApi()) {
    case CommandLine::DirectX12:
#if defined(_MSC_VER) && defined(TEMPEST_BUILD_DIRECTX12)
      return std::make_unique<Tempest::DirectX12Api>(flg);
#else
      break;
#endif
    case CommandLine::WebGL2:
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
      return std::make_unique<Tempest::WebGLApi>(flg);
#else
      break;
#endif
    case CommandLine::Vulkan:
#if defined(TEMPEST_BUILD_VULKAN)
      return std::make_unique<Tempest::VulkanApi>(flg);
#else
      break;
#endif
    }

#if defined(__APPLE__) && defined(TEMPEST_BUILD_METAL)
  return std::make_unique<Tempest::MetalApi>(flg);
#elif defined(TEMPEST_BUILD_VULKAN)
  return std::make_unique<Tempest::VulkanApi>(flg);
#elif defined(__EMSCRIPTEN__)
  return std::make_unique<Tempest::WebGapi>(flg);
#else
  return nullptr;
#endif
  }

#if defined(__ANDROID__)
extern void androidAppendLog(const char* text);

class AndroidLogBuf : public std::streambuf {
  public:
    AndroidLogBuf(android_LogPriority prio, const char* tag) : prio(prio), tag(tag) {}
  protected:
    int_type overflow(int_type c) override {
      if(c != EOF) {
        if(c == '\n') {
          __android_log_print(prio, tag, "%s", buf.c_str());
          androidAppendLog(buf.c_str());
          buf.clear();
          } else {
          buf += static_cast<char>(c);
          }
        }
      return c;
      }
    int sync() override {
      if(!buf.empty()) {
        __android_log_print(prio, tag, "%s", buf.c_str());
        androidAppendLog(buf.c_str());
        buf.clear();
        }
      return 0;
      }
  private:
    android_LogPriority prio;
    const char*         tag;
    std::string         buf;
  };

int opengothic_main(int argc, const char** argv);
int opengothic_main(int argc, const char** argv) {
  static AndroidLogBuf coutBuf(ANDROID_LOG_INFO,  "OpenGothic");
  static AndroidLogBuf cerrBuf(ANDROID_LOG_ERROR, "OpenGothic");
  std::cout.rdbuf(&coutBuf);
  std::cerr.rdbuf(&cerrBuf);
#else
int main(int argc, const char** argv) {
#endif
#if defined(__IOS__)
  {
    auto appdir = InstallDetect::applicationSupportDirectory();
    std::filesystem::current_path(appdir);
  }
#endif

#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
  try {
    static Tempest::WFile logFile("log.txt");
    Tempest::Log::setOutputCallback([](Tempest::Log::Mode mode, const char* text) {
      logFile.write(text,std::strlen(text));
      logFile.write("\n",1);
      if(mode==Tempest::Log::Error)
        logFile.flush();
      });
    }
  catch(...) {
    Tempest::Log::e("unable to setup logfile - fallback to console log");
    }
#endif
  CrashLog::setup();

  zenkit::Logger::set(zenkit::LogLevel::INFO, [] (zenkit::LogLevel lvl, const char* cat, const char* message) {
    (void)cat;
    switch (lvl) {
      case zenkit::LogLevel::ERROR:
        Tempest::Log::e("[zenkit] ", message);
        break;
      case zenkit::LogLevel::WARNING:
        Tempest::Log::e("[zenkit] ", message);
        break;
      case zenkit::LogLevel::INFO:
        Tempest::Log::i("[zenkit] ", message);
        break;
      case zenkit::LogLevel::DEBUG:
      case zenkit::LogLevel::TRACE:
        Tempest::Log::d("[zenkit] ", message); // unused
        break;
      }
    });
  Dm_setLogger(DmLogLevel_INFO, [](void* ctx, DmLogLevel lvl, char const* msg) {
    switch (lvl) {
      case DmLogLevel_FATAL:
      case DmLogLevel_ERROR:
      case DmLogLevel_WARN:
        Tempest::Log::e("[dmusic] ", msg);
        break;
      case DmLogLevel_INFO:
        Tempest::Log::i("[dmusic] ", msg);
        break;
      case DmLogLevel_DEBUG:
      case DmLogLevel_TRACE:
        Tempest::Log::d("[dmusic] ", msg);
        break;
      }
    }, nullptr);

  Tempest::Log::i(appBuild);
  Workers::setThreadName("Main thread");
  OG_HB('main-enter');

  try {
#if defined(__EMSCRIPTEN__)
    // The browser main loop keeps running after main() returns (the runtime is
    // never exited), so the engine state must not live on main()'s stack.
    auto* cmd    = new CommandLine{argc,argv};
    auto* api    = mkApi(*cmd).release();
    const auto gpuName = selectDevice(*api);
    CrashLog::setGpu(gpuName);
    auto* device = new Tempest::Device{*api,gpuName};
    CrashLog::setGpu(device->properties().name);
    new Resources{*device};
    new Gothic();
    new GameMusic();
    Gothic::inst().setupGlobalScripts();
    new MainWindow(*device);
    OG_HB('main-ctor-done');
    auto* app = new Tempest::Application();
    OG_HB('main-exec-enter');
    return app->exec();
#else
    CommandLine          cmd{argc,argv};
#if defined(__ANDROID__)
    // Vulkan, or OpenGL ES (the WebGL 2 renderer of the web build) when asked for (-gl) or when Vulkan can not be
    // used on this phone (no driver, no device, the device is not made)
    std::unique_ptr<Tempest::AbstractGraphicsApi> api;
    std::unique_ptr<Tempest::Device>              devicePtr;
    if(cmd.graphicsApi()!=CommandLine::WebGL2) {
      try {
        api = mkApi(cmd);
        const auto gpuName = selectDevice(*api);
        if(gpuName.empty())
          throw std::runtime_error("no vulkan device");
        CrashLog::setGpu(gpuName);
        devicePtr.reset(new Tempest::Device{*api,gpuName});
        }
      catch(const std::exception& e) {
        Tempest::Log::e("Vulkan is not usable (", e.what(), "): OpenGL ES instead");
        devicePtr.reset();
        api.reset();
        }
      }
    if(devicePtr==nullptr) {
      api = std::make_unique<Tempest::WebGLApi>();
      const auto gpuName = selectDevice(*api);
      CrashLog::setGpu(gpuName);
      devicePtr.reset(new Tempest::Device{*api,gpuName});
      }
    Tempest::Device&     device = *devicePtr;
    CrashLog::setGpu(device.properties().name);
#else
    auto                 api     = mkApi(cmd);
    const auto           gpuName = selectDevice(*api);
    CrashLog::setGpu(gpuName);

    Tempest::Device      device{*api,gpuName};
    CrashLog::setGpu(device.properties().name);
#endif

    Resources            resources{device};
    Gothic               gothic;
    GameMusic            music;
    gothic.setupGlobalScripts();

    MainWindow           wx(device);
    OG_HB('main-ctor-done');
    Tempest::Application app;
    OG_HB('main-exec-enter');
    int rc = app.exec();
    OG_HB('main-exec-done');
    return rc;
#endif
    }
  catch(const GothicNotFoundException&) {
    Tempest::Log::e("Gothic installation not found! Please specify game directory with -g <path>.");
    return 1;
    }
  catch(const std::exception& e) {
    Tempest::Log::e("Fatal exception: ", e.what());
    return 1;
    }
  catch(...) {
    OG_HB('main-unknown-throw');
    Tempest::Log::e("Fatal exception: unknown type");
    return 1;
    }
  }

