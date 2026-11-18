#pragma once

#include <ostream>
#include <string_view>

class CrashLog final {
  public:
    static void setup();
    static void setGpu(std::string_view name);
    static void setGameDir(std::string_view dir);

    static void dumpStack(const char* sig, const char* extGpuLog);

  private:
    static void tracebackStd(std::ostream& out);
    static void tracebackLinux(std::ostream &out);
    static void tracebackAndroid(std::ostream &out);
    static void tracebackGpu(std::ostream &out, const char* extGpuLog);
    static void writeSysInfo(std::ostream& fout);
  };

