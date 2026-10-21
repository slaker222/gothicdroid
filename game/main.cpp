#include <Tempest/Window>
#include <Tempest/Application>
#include <Tempest/Log>

#include <iostream>
#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include <zenkit/Logger.hh>

#include <Tempest/VulkanApi>

#if defined(_MSC_VER)
#include <Tempest/DirectX12Api>
#endif

#if defined(__APPLE__)
#include <Tempest/MetalApi>
#endif

#if defined(__IOS__)
#include "utils/installdetect.h"
#endif

#include "utils/crashlog.h"
#include "mainwindow.h"
#include "gothic.h"
#include "build.h"
#include "commandline.h"

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
#if defined(_MSC_VER)
      return std::make_unique<Tempest::DirectX12Api>(flg);
#else
      break;
#endif
    case CommandLine::Vulkan:
#if !defined(__APPLE__)
      return std::make_unique<Tempest::VulkanApi>(flg);
#else
      break;
#endif
    }

#if defined(__APPLE__)
  return std::make_unique<Tempest::MetalApi>(flg);
#else
  return std::make_unique<Tempest::VulkanApi>(flg);
#endif
  }

#if defined(__ANDROID__)
class AndroidLogBuf : public std::streambuf {
  public:
    AndroidLogBuf(android_LogPriority prio, const char* tag) : prio(prio), tag(tag) {}
  protected:
    int_type overflow(int_type c) override {
      if(c != EOF) {
        if(c == '\n') {
          __android_log_print(prio, tag, "%s", buf.c_str());
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

#if !defined(__ANDROID__)
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

  CommandLine          cmd{argc,argv};
  auto                 api     = mkApi(cmd);
  const auto           gpuName = selectDevice(*api);
  CrashLog::setGpu(gpuName);

  Tempest::Device      device{*api,gpuName};
  CrashLog::setGpu(device.properties().name);

  Resources            resources{device};
  Gothic               gothic;
  GameMusic            music;
  gothic.setupGlobalScripts();

  MainWindow           wx(device);
  Tempest::Application app;
  return app.exec();
  }
