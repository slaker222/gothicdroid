#include "crashlog.h"
#include "commandline.h"

#include <Tempest/Except>
#include <Tempest/Platform>

#include <iostream>
#include <cstring>
#include <csignal>
#include <fstream>
#include <cstring>

#if defined(__cpp_lib_stacktrace)
#include <stacktrace>
#endif

#if defined(__LINUX__) || defined(__APPLE__)
#include <execinfo.h> // backtrace
#include <dlfcn.h>    // dladdr
#include <cxxabi.h>   // __cxa_demangle
#endif

#if defined(__ANDROID__)
#include <unwind.h>
#include <dlfcn.h>
#include <cxxabi.h>
#include <unistd.h>
#endif

#if defined(__WINDOWS__)
#include <dbg/frames.hpp>
#include <dbg/symbols.hpp>
#endif

static char gpuName[64]="?";

#ifdef __WINDOWS__
#include <windows.h>

static LONG WINAPI exceptionHandler(PEXCEPTION_POINTERS) {
  SetUnhandledExceptionFilter(nullptr);
  CrashLog::dumpStack("ExceptionFilter", nullptr);
  return EXCEPTION_EXECUTE_HANDLER;
  }
#endif

static void signalHandler(int sig) {
  std::signal(sig, SIG_DFL);
  const char* sname = nullptr;
  char buf[16]={};
  if(sig==SIGSEGV)
    sname = "SIGSEGV";
  else if(sig==SIGABRT)
    sname = "SIGABRT";
  else if(sig==SIGFPE)
    sname = "SIGFPE";
  else if(sig==SIGBUS)
    sname = "SIGBUS";
  else if(sig==SIGILL)
    sname = "SIGILL";
  else {
    std::snprintf(buf,16,"%d",sig);
    sname = buf;
    }

  CrashLog::dumpStack(sname, nullptr);
  std::raise(sig);
  }

[[noreturn]]
static void terminateHandler() {
  char msg[128] = "std::terminate";
  std::exception_ptr p = std::current_exception();
  std::string extGpuLog;
  if(p) {
    try {
      std::rethrow_exception(p);
      }
    catch (GothicNotFoundException& ) {
      std::signal(SIGABRT, SIG_DFL); // avoid recursion
      std::abort();
      }
    catch (Tempest::DeviceLostException& e) {
      std::snprintf(msg,sizeof(msg),"DeviceLostException(%s)",e.what());
      extGpuLog = e.log();
      }
    catch (Tempest::BadTextureCastException& e) {
      std::snprintf(msg,sizeof(msg),"BadTextureCastException(%s)",e.what());
      }
    catch (std::system_error& e) {
      std::snprintf(msg,sizeof(msg),"std::system_error(%s)",e.what());
      }
    catch (std::runtime_error& e) {
      std::snprintf(msg,sizeof(msg),"std::runtime_error(%s)",e.what());
      }
    catch (std::logic_error& e) {
      std::snprintf(msg,sizeof(msg),"std::logic_error(%s)",e.what());
      }
    catch (std::bad_alloc& e) {
      std::snprintf(msg,sizeof(msg),"std::bad_alloc(%s)",e.what());
      }
    catch (std::exception& e) {
      std::snprintf(msg,sizeof(msg),"std::exception(%s)",e.what());
      }
    catch (...) {
      }
    }
  CrashLog::dumpStack(msg, extGpuLog.c_str());
  std::signal(SIGABRT, SIG_DFL); // avoid recursion
  std::abort();
  }

void CrashLog::setup() {
#ifdef __WINDOWS__
  SetUnhandledExceptionFilter(exceptionHandler);
#endif

  std::signal(SIGSEGV, &signalHandler);
  std::signal(SIGABRT, &signalHandler);
  std::signal(SIGFPE,  &signalHandler);
  std::signal(SIGBUS,  &signalHandler);
  std::signal(SIGILL,  &signalHandler);
  std::set_terminate(terminateHandler);
  }

static char s_gameDir[512] = "";

void CrashLog::setGpu(std::string_view name) {
  if(name.empty())
    return;
  std::strncpy(gpuName,name.data(),sizeof(gpuName)-1);
  }

void CrashLog::setGameDir(std::string_view dir) {
  if(!dir.empty()) {
    std::strncpy(s_gameDir, dir.data(), sizeof(s_gameDir)-1);
    s_gameDir[sizeof(s_gameDir)-1] = '\0';
    }
  }

void CrashLog::dumpStack(const char *sig, const char *extGpuLog) {
#if !defined(__cpp_lib_stacktrace) && defined(__WINDOWS__)
  dbg::symdb          db;
  dbg::call_stack<64> traceback;
#endif
  if(sig==nullptr)
    sig = "SIGSEGV";

  std::cout.setf(std::ios::unitbuf);
  std::cout << std::endl << "---crashlog(" <<  sig   << ")---" << std::endl;
  writeSysInfo(std::cout);
  tracebackGpu(std::cout, extGpuLog);
#if defined(__cpp_lib_stacktrace)
  tracebackStd(std::cout);
#elif defined(__WINDOWS__)
  traceback.collect(0);
  traceback.log(db, std::cout);
#elif defined(__ANDROID__)
  tracebackAndroid(std::cout);
#elif defined(__LINUX__) || defined(__APPLE__)
  tracebackLinux(std::cout);
#endif
  std::cout << std::endl;

#if defined(__ANDROID__)
  char crashFilePath[1024];
  if(s_gameDir[0] != '\0')
    std::snprintf(crashFilePath, sizeof(crashFilePath), "%s/crash.log", s_gameDir);
  else
    std::snprintf(crashFilePath, sizeof(crashFilePath), "/sdcard/Gothic2/crash.log");
#else
  const char* crashFilePath = "crash.log";
#endif
  std::ofstream fout(crashFilePath);
  fout.setf(std::ios::unitbuf);
  fout << "---crashlog(" <<  sig << ")---" << std::endl;
  writeSysInfo(fout);
  tracebackGpu(fout, extGpuLog);
#if defined(__cpp_lib_stacktrace)
  tracebackStd(fout);
#elif defined(__WINDOWS__)
  traceback.log(db, fout);
#elif defined(__ANDROID__)
  tracebackAndroid(fout);
#elif defined(__LINUX__) || defined(__APPLE__)
  tracebackLinux(fout);
#endif
  fout.flush();

#if defined(__ANDROID__)
  // Write crash marker file so MainActivity immediately detects the crash if resumed
  if(s_gameDir[0] != '\0') {
    char markerPath[1024];
    std::snprintf(markerPath, sizeof(markerPath), "%s/crash_marker.txt", s_gameDir);
    std::ofstream mf(markerPath);
    if(mf) {
      mf << "SIGNAL: " << sig << std::endl;
      mf.flush();
      }
    }

  // Attempt to spawn CrashActivity directly
  pid_t pid = fork();
  if(pid == 0) {
    const char* gdir = (s_gameDir[0] != '\0') ? s_gameDir : "/sdcard/Gothic2";
    execl("/system/bin/am", "am", "start", "-n", "com.gothic.droid/com.opengothic.CrashActivity",
          "--es", "game_dir", gdir,
          "--es", "crash_source", sig,
          (char*)nullptr);
    _exit(0);
    }
#endif
  }

void CrashLog::tracebackStd(std::ostream &out) {
#if defined(__cpp_lib_stacktrace)
  auto trace = std::stacktrace::current();

  uint32_t frameId = 0;
  for (auto& i : trace) {
    out << " #" << frameId  << "\t" << i.description() << ":" << i.source_line() << " in " << i.source_file() << std::endl;
    ++frameId;
    }
#endif
  }

void CrashLog::tracebackLinux(std::ostream &out) {
#if (defined(__LINUX__) || defined(__APPLE__)) && !defined(__ANDROID__)
  // inspired by https://gist.github.com/fmela/591333/36faca4c2f68f7483cd0d3a357e8a8dd5f807edf (BSD)
  void *callstack[64] = {};
  char **symbols = nullptr;
  int framesNum = 0;
  framesNum = backtrace(callstack, 64);
  symbols = backtrace_symbols(callstack, framesNum);
  if(symbols != nullptr) {
    int skip = 4; // skip the signal handler frames
    bool loop = true;
    Dl_info info;
    const char* frame = "";
    for(int i = skip; i < framesNum && loop; i++) {
      if(dladdr(callstack[i], &info) && info.dli_sname) {
        int status = -1;
        if(info.dli_sname[0] == '_')
          frame = abi::__cxa_demangle(info.dli_sname, NULL, 0, &status);
        frame = status == 0 ? frame : info.dli_sname == 0 ? symbols[i] : info.dli_sname;
        }
      if(!strcmp("main", frame)) {
        // looping beyond the main() causes crashes
        loop = false;
        }
      if(strcmp(frame, symbols[i]))
        out << "#" << i-skip+1 << ": " << frame << " - " << symbols[i] << std::endl;
      else
        out << "#" << i-skip+1 << ": " << frame << std::endl;
      }
    free(symbols);
    }
#endif
  }

void CrashLog::tracebackAndroid(std::ostream& out) {
#if defined(__ANDROID__)
  struct AndroidBacktraceState {
    void** current;
    void** end;
  };

  auto unwindCallback = [](struct _Unwind_Context* context, void* arg) -> _Unwind_Reason_Code {
    auto* state = static_cast<AndroidBacktraceState*>(arg);
    uintptr_t pc = _Unwind_GetIP(context);
    if(pc) {
      if(state->current == state->end)
        return _URC_END_OF_STACK;
      *state->current++ = reinterpret_cast<void*>(pc);
    }
    return _URC_NO_REASON;
  };

  void* stack[64] = {};
  AndroidBacktraceState state = {stack, stack + 64};
  _Unwind_Backtrace(unwindCallback, &state);
  size_t count = static_cast<size_t>(state.current - stack);

  size_t skip = 3;
  for(size_t i = skip; i < count; ++i) {
    Dl_info info = {};
    if(dladdr(stack[i], &info) && info.dli_sname) {
      int status = -1;
      char* demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
      const char* name = (status == 0 && demangled) ? demangled : info.dli_sname;
      const char* fname = info.dli_fname ? info.dli_fname : "lib";
      out << "#" << (i - skip + 1) << " " << stack[i] << " in " << fname << " : " << name
          << " [fbase=" << info.dli_fbase << " off=" << (info.dli_fbase ? (reinterpret_cast<uintptr_t>(stack[i]) - reinterpret_cast<uintptr_t>(info.dli_fbase)) : 0) << "]" << std::endl;
      if(demangled)
        free(demangled);
    } else if(info.dli_fname) {
      out << "#" << (i - skip + 1) << " " << stack[i] << " in " << info.dli_fname
          << " [fbase=" << info.dli_fbase << " off=" << (info.dli_fbase ? (reinterpret_cast<uintptr_t>(stack[i]) - reinterpret_cast<uintptr_t>(info.dli_fbase)) : 0) << "]" << std::endl;
    } else {
      out << "#" << (i - skip + 1) << " " << stack[i] << std::endl;
    }
  }
#else
  (void)out;
#endif
  }

void CrashLog::tracebackGpu(std::ostream& out, const char* extGpuLog) {
  if(extGpuLog==nullptr || extGpuLog[0]=='\0')
    return;
  out << std::endl;
  out << "---gpulog begin---" << std::endl;
  out << extGpuLog << std::endl;
  out << "---gpulog end  ---" << std::endl;
  out << std::endl;
  }

void CrashLog::writeSysInfo(std::ostream &fout) {
  fout << "GPU: " << gpuName << std::endl;
  }
