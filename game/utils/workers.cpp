#include "workers.h"
#include "utils/string_frm.h"

#include <Tempest/Platform>
#include <Tempest/Log>

#if defined(__WINDOWS__)
#include <windows.h>
#include <processthreadsapi.h>
#endif

#if defined(__GNUC__)
#include <pthread.h>
#endif

#if defined(__EMSCRIPTEN_PTHREADS__)
#include <emscripten/proxying.h>
#include <emscripten/threading.h>
#endif

#if defined(_MSC_VER)
void Workers::setThreadName(const char* threadName) {
  const DWORD MS_VC_EXCEPTION = 0x406D1388;
  DWORD dwThreadID = GetCurrentThreadId();
#pragma pack(push,8)
  struct THREADNAME_INFO {
    DWORD  dwType = 0x1000; // Must be 0x1000.
    LPCSTR szName;          // Pointer to name (in user addr space).
    DWORD  dwThreadID;      // Thread ID (-1=caller thread).
    DWORD  dwFlags;         // Reserved for future use, must be zero.
    };
#pragma pack(pop)

  THREADNAME_INFO info = {};
  info.szName     = threadName;
  info.dwThreadID = dwThreadID;
  info.dwFlags    = 0;

  __try {
    RaiseException(MS_VC_EXCEPTION, 0, sizeof(info)/sizeof(ULONG_PTR), (ULONG_PTR*)&info );
    }
  __except(EXCEPTION_EXECUTE_HANDLER) {
    }
  }
#elif defined(__WINDOWS__)
void Workers::setThreadName(const char* threadName) {
#if defined(__GNUC__)
  pthread_setname_np(pthread_self(), threadName);
#endif
  auto k32 = GetModuleHandleA("Kernel32");
  auto fn  = GetProcAddress(k32, "SetThreadDescription");
  if(fn==nullptr)
    return;

  // nsight does not care about pthread_setname_np
  WCHAR wname[64] = {};
  for(size_t i=0; i<63 && threadName[i]; ++i)
    wname[i] = WCHAR(threadName[i]);
  auto SetThreadDescription = reinterpret_cast<HRESULT(WINAPI*)(HANDLE,PCWSTR)>(fn);
  SetThreadDescription(GetCurrentThread(), wname);
  }
#elif defined(__GNUC__) && !defined(__clang__)
void Workers::setThreadName(const char* threadName){
  pthread_setname_np(pthread_self(), threadName);
  }
#elif defined(__OSX__)
void Workers::setThreadName(const char* threadName){
  pthread_setname_np(threadName);
  }
#else
void Workers::setThreadName(const char* threadName) { (void)threadName; }
#endif

using namespace Tempest;

const size_t Workers::taskPerThread = 128;
#if defined(__EMSCRIPTEN__)
// web: workers start one after another (a core may be busy with the browser):
// small steps keep the late ones from holding the last chunk
const size_t Workers::taskPerStep   = 4;
#else
const size_t Workers::taskPerStep   = 16;
#endif

Workers::Workers() {
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
  // single-threaded wasm build: pthread_create always fails without -pthread,
  // so no worker pool is created and all work runs inline on the main thread
#else
  // web: a thread is a Web Worker from a pool of fixed size, start only as
  // many as tasks are handed out (maxThreads)
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
  // android: no more than one batch ever runs on (maxThreads), the other threads only woke up on every batch (notify_all)
  // and went to sleep again
  const size_t count = maxThreads();
#else
  const size_t count = MAX_THREADS;
#endif
  thCount = count;
  for(size_t id=0; id<count; ++id) {
    th[id] = std::thread([this,id]() noexcept {
      threadFunc(id);
      });
    }
#endif
  }

Workers::~Workers() {
#if defined(__EMSCRIPTEN__)
  // the runtime never exits: workers stay parked
  running = false;
#else
  running  = false;
  workSet  = nullptr;
  workSize = thCount;
  execWork(minWorkSize<void,void>());
  for(auto& i:th)
    if(i.joinable())
      i.join();
#endif
  }

Workers &Workers::inst() {
  static Workers w;
  return w;
  }

uint8_t Workers::maxThreads() {
  int32_t th = int32_t(std::thread::hardware_concurrency());
#if defined(__EMSCRIPTEN__)
  // the main thread spins while workers run: leave it a core
  th -= 1;
#endif
  if(th<=0)
    th = 1;
  if(th>MAX_THREADS)
    return MAX_THREADS;
  return uint8_t(th);
  }

void Workers::threadFunc(size_t id) {
  {
  string_frm tname("Workers [",int(id),"]");
  setThreadName(tname.c_str());
  }

  while(true) {
    {
    std::unique_lock<std::mutex> lck(sync);
    workWait.wait(lck, [this]() { return workTbd>0; });
    --workTbd;
    }

    if(!running) {
      taskDone.fetch_add(1);
      return;
      }

    if(workSet==nullptr) {
      auto idx = progressIt.fetch_add(1);
      workFunc(workSet+idx, 1);
      } else {
      taskLoop();
      }

    taskDone.fetch_add(1);
    // if(size_t(taskDone.fetch_add(1)+1)==taskCount)
    //   std::this_thread::yield();
    }
  }

uint32_t Workers::taskLoop() {
  uint32_t count = 0;
  while(true) {
    size_t b = size_t(progressIt.fetch_add(taskPerStep));
    size_t e = std::min(b+taskPerStep, workSize);
    if(e<=b)
      break;

    void* d = workSet + b*workEltSize;
    workFunc(d,e-b);
    count += uint32_t(e-b);
    }
  return count;
  }

void Workers::execWork(uint32_t& minElts) {
  if(workSize==0)
    return;

#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
  // single-threaded fallback: process the whole batch on the calling thread
  if(workSet!=nullptr) {
    workFunc(workSet, workSize);
    } else {
    // task mode: the task index is passed through the data pointer
    for(size_t i=0; i<workSize; ++i)
      workFunc(reinterpret_cast<uint8_t*>(i), 1);
    }
  return;
#endif

  if(workSet!=nullptr) {
    const auto maxTheads = maxThreads();
    taskCount = uint32_t((workSize+taskPerThread-1)/taskPerThread);
    taskCount--; // main thread also do tasks
    if(taskCount>maxTheads)
      taskCount = maxTheads;
    if(taskCount<=0)
      taskCount = 1;
    } else {
    taskCount = uint32_t(workSize);
    }

  if(running && taskCount==1) {
    workFunc(workSet, workSize);
    return;
    }

  minElts = std::max<uint32_t>(minElts, taskPerThread);

  if(workSet!=nullptr && workSize<=minElts && true) {
    workFunc(workSet, workSize);
    if(minElts > workSize*2)
      minElts = 0;
    return;
    }

  progressIt.store(0);
  taskDone.store(0);

  {
  std::unique_lock<std::mutex> lck(sync);
  workTbd = int32_t(taskCount);
  }
  workWait.notify_all();

  uint32_t cnt = 0;
  if(workSet==nullptr) {
    std::this_thread::yield();
    } else {
    cnt = taskLoop(); (void)cnt;
    }

  while(true) {
    int expect = int(taskCount);
    if(taskDone.load()==expect) {
      taskDone.store(0);
      break;
      }
#if defined(__EMSCRIPTEN_PTHREADS__)
    // tasks may wait for calls proxied to the main thread (audio, files)
    if(emscripten_is_main_runtime_thread())
      emscripten_proxy_execute_queue(emscripten_proxy_get_system_queue());
#endif
    std::this_thread::yield();
    }
  }
