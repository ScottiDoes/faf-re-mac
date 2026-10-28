// Win32 on POSIX: Threads (declarations in compat/Threads.h).

#include "WinCompatInternal.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include <mach/mach.h>
#include <mach/thread_act.h>
#include <sched.h>
#include <sys/sysctl.h>
#include <time.h>
#include <unistd.h>

using namespace faf_posix;

namespace
{
  // Per-thread state lives in pthread keys, not thread_local: the object key's destructor
  // (thread exit) must still be able to read and reset it after the image's thread_local
  // storage has been torn down.
  pthread_once_t gKeysOnce = PTHREAD_ONCE_INIT;
  pthread_key_t gIdKey;     // the thread's Win32 id (no destructor: outlives everything)
  pthread_key_t gObjectKey; // its ThreadObject (destructor: thread exit)

  std::atomic<DWORD> gNextThreadId{0x104};

  pthread_mutex_t gRegistryLock = PTHREAD_MUTEX_INITIALIZER;
  ThreadObject* gRegistryHead = nullptr;

  std::atomic<DWORD_PTR> gProcessAffinity{0};
  std::atomic<DWORD> gPriorityClass{NORMAL_PRIORITY_CLASS};

  void FinalizeThread(void* value) noexcept;

  void CreateKeys() noexcept
  {
    (void)pthread_key_create(&gIdKey, nullptr);
    (void)pthread_key_create(&gObjectKey, &FinalizeThread);
  }

  inline void EnsureKeys() noexcept
  {
    (void)pthread_once(&gKeysOnce, &CreateKeys);
  }

  DWORD AllocateThreadId() noexcept
  {
    DWORD id = gNextThreadId.fetch_add(4, std::memory_order_relaxed);
    if (id == 0) {
      id = gNextThreadId.fetch_add(4, std::memory_order_relaxed);
    }
    return id;
  }

  void Register(ThreadObject* const thread) noexcept
  {
    pthread_mutex_lock(&gRegistryLock);
    thread->registryPrev = nullptr;
    thread->registryNext = gRegistryHead;
    if (gRegistryHead != nullptr) {
      gRegistryHead->registryPrev = thread;
    }
    gRegistryHead = thread;
    pthread_mutex_unlock(&gRegistryLock);
  }

  void Unregister(ThreadObject* const thread) noexcept
  {
    pthread_mutex_lock(&gRegistryLock);
    if (thread->registryPrev != nullptr) {
      thread->registryPrev->registryNext = thread->registryNext;
    } else if (gRegistryHead == thread) {
      gRegistryHead = thread->registryNext;
    }
    if (thread->registryNext != nullptr) {
      thread->registryNext->registryPrev = thread->registryPrev;
    }
    thread->registryNext = nullptr;
    thread->registryPrev = nullptr;
    pthread_mutex_unlock(&gRegistryLock);
  }

  // Thread exit (pthread key destructor): signal the handle, abandon owned mutexes, drop
  // queued APCs and the thread's own reference.
  void FinalizeThread(void* const value) noexcept
  {
    auto* const thread = static_cast<ThreadObject*>(value);
    if (thread == nullptr) {
      return;
    }
    {
      WaitGuard guard;
      if (thread->exitCode == STILL_ACTIVE) {
        thread->exitCode = 0;
      }
      thread->exited = true;
      AbandonOwnedMutexes(thread);
      thread->apcs.clear();
      NotifyWaiters(thread);
    }
    Unregister(thread);
    thread->Release();
  }

  struct Topology
  {
    DWORD logical = 1;
    DWORD physical = 1;
    // Physical cores per performance level (0 = fastest), and their logical counts.
    std::vector<DWORD> levelCores;
    std::vector<DWORD> levelLogical;
    std::vector<DWORD> levelL2Bytes;
    std::vector<DWORD> levelCpusPerL2;
    DWORD l1dBytes = 0;
    DWORD lineSize = 64;
  };

  bool ReadSysctl(const char* const name, DWORD* const out) noexcept
  {
    std::uint64_t value64 = 0;
    std::size_t size = sizeof(value64);
    if (sysctlbyname(name, &value64, &size, nullptr, 0) != 0) {
      return false;
    }
    if (size == sizeof(std::uint32_t)) {
      std::uint32_t value32 = 0;
      std::memcpy(&value32, &value64, sizeof(value32));
      *out = value32;
    } else {
      *out = static_cast<DWORD>(value64);
    }
    return true;
  }

  const Topology& SystemTopology() noexcept
  {
    static const Topology* const topology = [] {
      auto* t = new Topology();
      DWORD value = 0;
      if (ReadSysctl("hw.logicalcpu", &value) && value > 0) {
        t->logical = value;
      }
      if (ReadSysctl("hw.physicalcpu", &value) && value > 0) {
        t->physical = value;
      }
      DWORD levels = 0;
      if (ReadSysctl("hw.nperflevels", &levels) && levels > 0) {
        for (DWORD level = 0; level < levels; ++level) {
          char name[64];
          DWORD cores = 0;
          DWORD logical = 0;
          DWORD l2 = 0;
          DWORD perL2 = 0;
          std::snprintf(name, sizeof(name), "hw.perflevel%u.physicalcpu", level);
          (void)ReadSysctl(name, &cores);
          std::snprintf(name, sizeof(name), "hw.perflevel%u.logicalcpu", level);
          (void)ReadSysctl(name, &logical);
          std::snprintf(name, sizeof(name), "hw.perflevel%u.l2cachesize", level);
          (void)ReadSysctl(name, &l2);
          std::snprintf(name, sizeof(name), "hw.perflevel%u.cpusperl2", level);
          (void)ReadSysctl(name, &perL2);
          if (cores == 0) {
            continue;
          }
          t->levelCores.push_back(cores);
          t->levelLogical.push_back(logical != 0 ? logical : cores);
          t->levelL2Bytes.push_back(l2);
          t->levelCpusPerL2.push_back(perL2 != 0 ? perL2 : cores);
        }
      }
      if (t->levelCores.empty()) {
        t->levelCores.push_back(t->physical);
        t->levelLogical.push_back(t->logical);
        t->levelL2Bytes.push_back(0);
        t->levelCpusPerL2.push_back(t->physical);
      }
      (void)ReadSysctl("hw.l1dcachesize", &t->l1dBytes);
      (void)ReadSysctl("hw.cachelinesize", &t->lineSize);
      return t;
    }();
    return *topology;
  }

  DWORD_PTR SystemAffinityMask() noexcept
  {
    const DWORD logical = SystemTopology().logical;
    return logical >= 64 ? ~DWORD_PTR{0} : ((DWORD_PTR{1} << logical) - 1);
  }

  DWORD_PTR ProcessAffinityMask() noexcept
  {
    const DWORD_PTR mask = gProcessAffinity.load(std::memory_order_relaxed);
    return mask != 0 ? mask : SystemAffinityMask();
  }

  ThreadObject* ReferenceThread(const HANDLE thread) noexcept
  {
    return ReferenceHandleAs<ThreadObject>(thread, ObjectKind::Thread);
  }

  void* ThreadTrampoline(void* const argument)
  {
    auto* const thread = static_cast<ThreadObject*>(argument);
    EnsureKeys();
    (void)pthread_setspecific(gIdKey, reinterpret_cast<void*>(static_cast<std::uintptr_t>(thread->id)));
    (void)pthread_setspecific(gObjectKey, thread);
    {
      WaitGuard guard;
      thread->pthread = pthread_self();
      thread->machThread = pthread_mach_thread_np(pthread_self());
      thread->started = true;
      while (thread->suspendCount > 0) {
        pthread_cond_wait(&thread->wake, &gWaitLock);
      }
      thread->gated = false;
    }
    const DWORD exitCode = thread->start(thread->startParameter);
    {
      WaitGuard guard;
      thread->exitCode = exitCode;
    }
    return nullptr; // FinalizeThread runs from the key destructor
  }

  HANDLE StartThread(const SIZE_T stackSize, const ThreadStart start, void* const parameter, const DWORD flags,
                     DWORD* const threadId) noexcept
  {
    if (start == nullptr) {
      ::SetLastError(ERROR_INVALID_PARAMETER);
      return nullptr;
    }
    EnsureKeys();
    auto* const thread = new (std::nothrow) ThreadObject();
    if (thread == nullptr) {
      ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return nullptr;
    }
    thread->id = AllocateThreadId();
    thread->start = start;
    thread->startParameter = parameter;
    thread->gated = true;
    thread->suspendCount = (flags & CREATE_SUSPENDED) != 0 ? 1 : 0;
    thread->AddRef(); // one reference for the handle, one for the running thread
    Register(thread);

    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    constexpr SIZE_T kMinimumStack = 2u * 1024u * 1024u;
    const SIZE_T pageSize = static_cast<SIZE_T>(getpagesize());
    SIZE_T stack = stackSize > kMinimumStack ? stackSize : kMinimumStack;
    stack = (stack + pageSize - 1) & ~(pageSize - 1);
    pthread_attr_setstacksize(&attributes, stack);

    pthread_t native{};
    const int rc = pthread_create(&native, &attributes, &ThreadTrampoline, thread);
    pthread_attr_destroy(&attributes);
    if (rc != 0) {
      Unregister(thread);
      thread->Release();
      thread->Release();
      ::SetLastError(rc == EAGAIN ? ERROR_NOT_ENOUGH_MEMORY : Win32ErrorFromErrno(rc));
      return nullptr;
    }
    if (threadId != nullptr) {
      *threadId = thread->id;
    }
    return InsertHandle(thread);
  }

  struct CrtThreadStart
  {
    unsigned(__stdcall* start)(void*);
    void (*legacyStart)(void*);
    void* argument;
  };

  DWORD CrtThreadEntry(void* const parameter)
  {
    const CrtThreadStart start = *static_cast<CrtThreadStart*>(parameter);
    delete static_cast<CrtThreadStart*>(parameter);
    if (start.legacyStart != nullptr) {
      start.legacyStart(start.argument);
      return 0;
    }
    return start.start(start.argument);
  }
} // namespace

namespace faf_posix
{
  // A thread's id and object are created together on its first call into the layer, so
  // that OpenThread finds every thread whose id anyone could have obtained (Thread.cpp
  // queues APCs to the main thread by the id it recorded). Creation never touches the
  // engine's operator new (KernelObject allocates from the system heap).
  DWORD CurrentThreadId() noexcept
  {
    EnsureKeys();
    const auto value = reinterpret_cast<std::uintptr_t>(pthread_getspecific(gIdKey));
    if (value != 0) {
      return static_cast<DWORD>(value);
    }
    return CurrentThread()->id;
  }

  ThreadObject* CurrentThread() noexcept
  {
    EnsureKeys();
    if (auto* const existing = static_cast<ThreadObject*>(pthread_getspecific(gObjectKey))) {
      return existing;
    }
    auto* const thread = new (std::nothrow) ThreadObject();
    if (thread == nullptr) {
      std::abort(); // no way to report failure from GetCurrentThread-style callers
    }
    DWORD id = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(pthread_getspecific(gIdKey)));
    if (id == 0) {
      // First call on this thread (or the object was finalized: keep the id it had).
      id = AllocateThreadId();
      (void)pthread_setspecific(gIdKey, reinterpret_cast<void*>(static_cast<std::uintptr_t>(id)));
    }
    thread->id = id;
    thread->pthread = pthread_self();
    thread->machThread = pthread_mach_thread_np(pthread_self());
    thread->started = true;
    Register(thread);
    (void)pthread_setspecific(gObjectKey, thread);
    return thread;
  }

  ThreadObject* FindThread(const DWORD id) noexcept
  {
    pthread_mutex_lock(&gRegistryLock);
    ThreadObject* found = nullptr;
    for (ThreadObject* thread = gRegistryHead; thread != nullptr; thread = thread->registryNext) {
      if (thread->id == id && thread->TryAddRef()) {
        found = thread;
        break;
      }
    }
    pthread_mutex_unlock(&gRegistryLock);
    return found;
  }

  std::vector<DWORD> LiveThreadIds()
  {
    std::vector<DWORD> ids;
    pthread_mutex_lock(&gRegistryLock);
    try {
      for (ThreadObject* thread = gRegistryHead; thread != nullptr; thread = thread->registryNext) {
        ids.push_back(thread->id);
      }
    } catch (...) {
      pthread_mutex_unlock(&gRegistryLock);
      throw;
    }
    pthread_mutex_unlock(&gRegistryLock);
    return ids;
  }

  std::size_t RunQueuedApcs(ThreadObject* const self) noexcept
  {
    std::size_t ran = 0;
    for (;;) {
      std::pair<ApcRoutine, ULONG_PTR> apc{};
      {
        WaitGuard guard;
        if (self->apcs.empty()) {
          break;
        }
        apc = self->apcs.front();
        self->apcs.pop_front();
      }
      apc.first(apc.second);
      ++ran;
    }
    return ran;
  }
} // namespace faf_posix

// --- Threads --------------------------------------------------------------------------------

HANDLE CreateThread(LPSECURITY_ATTRIBUTES const attributes, const SIZE_T stackSize, LPTHREAD_START_ROUTINE const start,
                    LPVOID const parameter, const DWORD creationFlags, LPDWORD const threadId) noexcept
{
  (void)attributes;
  return StartThread(stackSize, reinterpret_cast<ThreadStart>(start), parameter, creationFlags, threadId);
}

HANDLE OpenThread(const DWORD access, const BOOL inherit, const DWORD threadId) noexcept
{
  (void)access;
  (void)inherit;
  if (threadId == CurrentThreadId()) {
    ThreadObject* const self = CurrentThread();
    self->AddRef();
    return InsertHandle(self);
  }
  ThreadObject* const thread = FindThread(threadId);
  if (thread == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return nullptr;
  }
  return InsertHandle(thread);
}

HANDLE GetCurrentThread() noexcept
{
  return CurrentThreadPseudoHandle();
}

DWORD GetCurrentThreadId() noexcept
{
  return CurrentThreadId();
}

DWORD GetThreadId(const HANDLE thread) noexcept
{
  ThreadObject* const object = ReferenceThread(thread);
  if (object == nullptr) {
    return 0;
  }
  const DWORD id = object->id;
  object->Release();
  return id;
}

void ExitThread(const DWORD exitCode) noexcept
{
  ThreadObject* const self = CurrentThread();
  {
    WaitGuard guard;
    self->exitCode = exitCode;
  }
  pthread_exit(nullptr);
}

BOOL TerminateThread(const HANDLE thread, const DWORD exitCode) noexcept
{
  (void)thread;
  (void)exitCode;
  FAF_POSIX_STUB("TerminateThread: threads cannot be killed safely on POSIX");
  ::SetLastError(ERROR_ACCESS_DENIED);
  return FALSE;
}

BOOL GetExitCodeThread(const HANDLE thread, LPDWORD const exitCode) noexcept
{
  ThreadObject* const object = ReferenceThread(thread);
  if (object == nullptr) {
    return FALSE;
  }
  {
    WaitGuard guard;
    if (exitCode != nullptr) {
      *exitCode = object->exited ? object->exitCode : STILL_ACTIVE;
    }
  }
  object->Release();
  return TRUE;
}

DWORD SuspendThread(const HANDLE thread) noexcept
{
  ThreadObject* const object = ReferenceThread(thread);
  if (object == nullptr) {
    return static_cast<DWORD>(-1);
  }
  DWORD previous = static_cast<DWORD>(-1);
  bool suspendMach = false;
  mach_port_t port = 0;
  {
    WaitGuard guard;
    if (object->exited) {
      ::SetLastError(ERROR_ACCESS_DENIED);
    } else {
      previous = static_cast<DWORD>(object->suspendCount);
      ++object->suspendCount;
      suspendMach = !object->gated;
      port = object->machThread;
    }
  }
  if (suspendMach && thread_suspend(port) != KERN_SUCCESS) {
    WaitGuard guard;
    --object->suspendCount;
    previous = static_cast<DWORD>(-1);
    ::SetLastError(ERROR_ACCESS_DENIED);
  }
  object->Release();
  return previous;
}

DWORD ResumeThread(const HANDLE thread) noexcept
{
  ThreadObject* const object = ReferenceThread(thread);
  if (object == nullptr) {
    return static_cast<DWORD>(-1);
  }
  DWORD previous = 0;
  bool resumeMach = false;
  mach_port_t port = 0;
  {
    WaitGuard guard;
    previous = static_cast<DWORD>(object->suspendCount);
    if (object->suspendCount > 0) {
      --object->suspendCount;
      if (object->gated) {
        if (object->suspendCount == 0) {
          pthread_cond_signal(&object->wake);
        }
      } else if (!object->exited) {
        resumeMach = true;
        port = object->machThread;
      }
    }
  }
  if (resumeMach) {
    (void)thread_resume(port);
  }
  object->Release();
  return previous;
}

int GetThreadPriority(const HANDLE thread) noexcept
{
  ThreadObject* const object = ReferenceThread(thread);
  if (object == nullptr) {
    return THREAD_PRIORITY_ERROR_RETURN;
  }
  const int priority = object->priority;
  object->Release();
  return priority;
}

BOOL SetThreadPriority(const HANDLE thread, const int priority) noexcept
{
  ThreadObject* const object = ReferenceThread(thread);
  if (object == nullptr) {
    return FALSE;
  }
  if (priority != THREAD_MODE_BACKGROUND_BEGIN && priority != THREAD_MODE_BACKGROUND_END) {
    object->priority = priority;
  }
  object->Release();
  return TRUE;
}

BOOL SetThreadPriorityBoost(const HANDLE thread, const BOOL disable) noexcept
{
  (void)disable;
  ThreadObject* const object = ReferenceThread(thread);
  if (object == nullptr) {
    return FALSE;
  }
  object->Release();
  return TRUE;
}

HRESULT SetThreadDescription(const HANDLE thread, PCWSTR const description) noexcept
{
  ThreadObject* const object = ReferenceThread(thread);
  if (object == nullptr) {
    return E_HANDLE;
  }
  const bool self = object->id == CurrentThreadId();
  object->Release();
  if (self && description != nullptr) {
    try {
      const std::string name = Utf8FromWide(description);
      (void)pthread_setname_np(name.substr(0, 63).c_str());
    } catch (...) {
      return E_OUTOFMEMORY;
    }
  }
  return S_OK; // another thread's name cannot be set on macOS; Windows callers ignore that
}

DWORD_PTR SetThreadAffinityMask(const HANDLE thread, const DWORD_PTR mask) noexcept
{
  if (mask == 0 || (mask & ~ProcessAffinityMask()) != 0) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  ThreadObject* const object = ReferenceThread(thread);
  if (object == nullptr) {
    return 0;
  }
  const DWORD_PTR previous = object->affinityMask != 0 ? object->affinityMask : ProcessAffinityMask();
  object->affinityMask = mask;
  object->Release();
  return previous;
}

DWORD SetThreadIdealProcessor(const HANDLE thread, const DWORD idealProcessor) noexcept
{
  ThreadObject* const object = ReferenceThread(thread);
  if (object == nullptr) {
    return static_cast<DWORD>(-1);
  }
  const DWORD previous = object->idealProcessor;
  if (idealProcessor != MAXIMUM_PROCESSORS) {
    if (idealProcessor >= SystemTopology().logical) {
      object->Release();
      ::SetLastError(ERROR_INVALID_PARAMETER);
      return static_cast<DWORD>(-1);
    }
    object->idealProcessor = idealProcessor;
  }
  object->Release();
  return previous;
}

BOOL SetThreadIdealProcessorEx(const HANDLE thread, PPROCESSOR_NUMBER const idealProcessor,
                               PPROCESSOR_NUMBER const previousIdealProcessor) noexcept
{
  if (idealProcessor == nullptr || idealProcessor->Group != 0 || idealProcessor->Number >= SystemTopology().logical) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  ThreadObject* const object = ReferenceThread(thread);
  if (object == nullptr) {
    return FALSE;
  }
  if (previousIdealProcessor != nullptr) {
    previousIdealProcessor->Group = 0;
    previousIdealProcessor->Number = static_cast<BYTE>(object->idealProcessor);
    previousIdealProcessor->Reserved = 0;
  }
  object->idealProcessor = idealProcessor->Number;
  object->Release();
  return TRUE;
}

BOOL GetThreadIdealProcessorEx(const HANDLE thread, PPROCESSOR_NUMBER const idealProcessor) noexcept
{
  ThreadObject* const object = ReferenceThread(thread);
  if (object == nullptr) {
    return FALSE;
  }
  if (idealProcessor != nullptr) {
    idealProcessor->Group = 0;
    idealProcessor->Number = static_cast<BYTE>(object->idealProcessor);
    idealProcessor->Reserved = 0;
  }
  object->Release();
  return TRUE;
}

BOOL SetThreadGroupAffinity(const HANDLE thread, const GROUP_AFFINITY* const affinity,
                            PGROUP_AFFINITY const previousAffinity) noexcept
{
  if (affinity == nullptr || affinity->Group != 0 || affinity->Mask == 0 ||
      (affinity->Mask & ~SystemAffinityMask()) != 0) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  ThreadObject* const object = ReferenceThread(thread);
  if (object == nullptr) {
    return FALSE;
  }
  if (previousAffinity != nullptr) {
    std::memset(previousAffinity, 0, sizeof(*previousAffinity));
    previousAffinity->Mask = object->affinityMask != 0 ? object->affinityMask : ProcessAffinityMask();
  }
  object->affinityMask = affinity->Mask;
  object->Release();
  return TRUE;
}

BOOL GetThreadGroupAffinity(const HANDLE thread, PGROUP_AFFINITY const affinity) noexcept
{
  ThreadObject* const object = ReferenceThread(thread);
  if (object == nullptr) {
    return FALSE;
  }
  if (affinity != nullptr) {
    std::memset(affinity, 0, sizeof(*affinity));
    affinity->Mask = object->affinityMask != 0 ? object->affinityMask : ProcessAffinityMask();
  }
  object->Release();
  return TRUE;
}

DWORD GetCurrentProcessorNumber() noexcept
{
  std::size_t cpu = 0;
  if (pthread_cpu_number_np(&cpu) != 0) {
    return 0;
  }
  const DWORD logical = SystemTopology().logical;
  return logical != 0 ? static_cast<DWORD>(cpu % logical) : 0;
}

void GetCurrentProcessorNumberEx(PPROCESSOR_NUMBER const number) noexcept
{
  if (number == nullptr) {
    return;
  }
  number->Group = 0;
  number->Number = static_cast<BYTE>(GetCurrentProcessorNumber());
  number->Reserved = 0;
}

DWORD QueueUserAPC(PAPCFUNC const routine, const HANDLE thread, const ULONG_PTR data) noexcept
{
  if (routine == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  ThreadObject* const object = ReferenceThread(thread);
  if (object == nullptr) {
    return 0;
  }
  DWORD result = 1;
  {
    WaitGuard guard;
    if (object->exited) {
      result = 0;
    } else {
      try {
        object->apcs.emplace_back(reinterpret_cast<ApcRoutine>(routine), data);
      } catch (...) {
        result = 0;
      }
      if (result != 0 && object->alertableWait) {
        pthread_cond_signal(&object->wake);
      }
    }
  }
  object->Release();
  if (result == 0) {
    ::SetLastError(ERROR_GEN_FAILURE);
  }
  return result;
}

void Sleep(const DWORD milliseconds) noexcept
{
  if (milliseconds == 0) {
    sched_yield();
    return;
  }
  if (milliseconds == INFINITE) {
    for (;;) {
      pause();
    }
  }
  timespec request{};
  request.tv_sec = static_cast<time_t>(milliseconds / 1000u);
  request.tv_nsec = static_cast<long>(milliseconds % 1000u) * 1000000L;
  timespec remaining{};
  while (nanosleep(&request, &remaining) != 0 && errno == EINTR) {
    request = remaining;
  }
}

DWORD SleepEx(const DWORD milliseconds, const BOOL alertable) noexcept
{
  if (alertable == FALSE) {
    Sleep(milliseconds);
    return 0;
  }
  const DWORD result = WaitForObjects(0, nullptr, false, milliseconds, true);
  if (result == WAIT_IO_COMPLETION) {
    return WAIT_IO_COMPLETION;
  }
  if (milliseconds == 0) {
    sched_yield();
  }
  return 0;
}

BOOL SwitchToThread() noexcept
{
  sched_yield();
  return TRUE;
}

// --- TLS ------------------------------------------------------------------------------------

DWORD TlsAlloc() noexcept
{
  pthread_key_t key{};
  if (pthread_key_create(&key, nullptr) != 0 || static_cast<std::uint64_t>(key) >= TLS_OUT_OF_INDEXES) {
    ::SetLastError(ERROR_NO_MORE_ITEMS);
    return TLS_OUT_OF_INDEXES;
  }
  return static_cast<DWORD>(key);
}

BOOL TlsFree(const DWORD index) noexcept
{
  if (pthread_key_delete(static_cast<pthread_key_t>(index)) != 0) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  return TRUE;
}

LPVOID TlsGetValue(const DWORD index) noexcept
{
  ::SetLastError(ERROR_SUCCESS); // as Windows: a null value is told from an error this way
  return pthread_getspecific(static_cast<pthread_key_t>(index));
}

BOOL TlsSetValue(const DWORD index, LPVOID const value) noexcept
{
  if (pthread_setspecific(static_cast<pthread_key_t>(index), value) != 0) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  return TRUE;
}

// --- CRT thread entry points -------------------------------------------------------------------

std::uintptr_t _beginthreadex(void* const security, const unsigned stackSize, unsigned(__stdcall* const start)(void*),
                              void* const argument, const unsigned initFlag, unsigned* const threadId) noexcept
{
  (void)security;
  if (start == nullptr) {
    errno = EINVAL;
    return 0;
  }
  auto* const parameter = new (std::nothrow) CrtThreadStart{start, nullptr, argument};
  if (parameter == nullptr) {
    errno = ENOMEM;
    return 0;
  }
  DWORD id = 0;
  const HANDLE handle = StartThread(stackSize, &CrtThreadEntry, parameter, initFlag, &id);
  if (handle == nullptr) {
    delete parameter;
    errno = EAGAIN;
    return 0;
  }
  if (threadId != nullptr) {
    *threadId = id;
  }
  return reinterpret_cast<std::uintptr_t>(handle);
}

std::uintptr_t _beginthread(void(__cdecl* const start)(void*), const unsigned stackSize, void* const argument) noexcept
{
  if (start == nullptr) {
    errno = EINVAL;
    return static_cast<std::uintptr_t>(-1);
  }
  auto* const parameter = new (std::nothrow) CrtThreadStart{nullptr, start, argument};
  if (parameter == nullptr) {
    errno = ENOMEM;
    return static_cast<std::uintptr_t>(-1);
  }
  const HANDLE handle = StartThread(stackSize, &CrtThreadEntry, parameter, 0, nullptr);
  if (handle == nullptr) {
    delete parameter;
    errno = EAGAIN;
    return static_cast<std::uintptr_t>(-1);
  }
  // _beginthread's handle is closed by the CRT when the thread ends; the caller may only
  // use it while the thread runs. The thread's own reference keeps the object alive.
  (void)CloseHandleSlot(handle);
  return reinterpret_cast<std::uintptr_t>(handle);
}

void _endthreadex(const unsigned exitCode) noexcept
{
  ExitThread(exitCode);
}

void _endthread() noexcept
{
  ExitThread(0);
}

// --- Process --------------------------------------------------------------------------------

HANDLE GetCurrentProcess() noexcept
{
  return CurrentProcessPseudoHandle();
}

DWORD GetCurrentProcessId() noexcept
{
  return static_cast<DWORD>(getpid());
}

DWORD GetProcessId(const HANDLE process) noexcept
{
  if (process == CurrentProcessPseudoHandle()) {
    return GetCurrentProcessId();
  }
  ::SetLastError(ERROR_INVALID_HANDLE);
  return 0;
}

BOOL GetProcessAffinityMask(const HANDLE process, PDWORD_PTR const processMask, PDWORD_PTR const systemMask) noexcept
{
  if (process != CurrentProcessPseudoHandle()) {
    ::SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (processMask == nullptr || systemMask == nullptr) {
    ::SetLastError(ERROR_NOACCESS);
    return FALSE;
  }
  *processMask = ProcessAffinityMask();
  *systemMask = SystemAffinityMask();
  return TRUE;
}

BOOL SetProcessAffinityMask(const HANDLE process, const DWORD_PTR mask) noexcept
{
  if (process != CurrentProcessPseudoHandle()) {
    ::SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (mask == 0 || (mask & ~SystemAffinityMask()) != 0) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  gProcessAffinity.store(mask, std::memory_order_relaxed);
  return TRUE;
}

DWORD GetPriorityClass(const HANDLE process) noexcept
{
  if (process != CurrentProcessPseudoHandle()) {
    ::SetLastError(ERROR_INVALID_HANDLE);
    return 0;
  }
  return gPriorityClass.load(std::memory_order_relaxed);
}

BOOL SetPriorityClass(const HANDLE process, const DWORD priorityClass) noexcept
{
  if (process != CurrentProcessPseudoHandle()) {
    ::SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  gPriorityClass.store(priorityClass, std::memory_order_relaxed);
  return TRUE;
}

// --- Processor topology ----------------------------------------------------------------------

namespace
{
  void AppendRecord(std::vector<unsigned char>& out, const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX& record)
  {
    const auto* const bytes = reinterpret_cast<const unsigned char*>(&record);
    out.insert(out.end(), bytes, bytes + record.Size);
  }

  bool Wants(const LOGICAL_PROCESSOR_RELATIONSHIP requested, const LOGICAL_PROCESSOR_RELATIONSHIP kind) noexcept
  {
    return requested == RelationAll || requested == kind;
  }
} // namespace

BOOL GetLogicalProcessorInformationEx(const LOGICAL_PROCESSOR_RELATIONSHIP relationship,
                                      PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX const buffer,
                                      PDWORD const returnedLength) noexcept
{
  if (returnedLength == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  try {
    const Topology& topology = SystemTopology();
    const DWORD_PTR all = SystemAffinityMask();
    std::vector<unsigned char> out;

    if (Wants(relationship, RelationProcessorCore)) {
      DWORD cpu = 0;
      for (std::size_t level = 0; level < topology.levelCores.size(); ++level) {
        const DWORD cores = topology.levelCores[level];
        const DWORD perCore = std::max<DWORD>(1, topology.levelLogical[level] / std::max<DWORD>(1, cores));
        for (DWORD core = 0; core < cores; ++core) {
          SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX record{};
          record.Relationship = RelationProcessorCore;
          record.Size = sizeof(record);
          record.Processor.Flags = perCore > 1 ? LTP_PC_SMT : 0;
          // Windows: a higher class is a faster core. Level 0 is the fastest on macOS.
          record.Processor.EfficiencyClass =
            static_cast<BYTE>(topology.levelCores.size() - 1 - level);
          record.Processor.GroupCount = 1;
          KAFFINITY mask = 0;
          for (DWORD i = 0; i < perCore && cpu < 64; ++i, ++cpu) {
            mask |= KAFFINITY{1} << cpu;
          }
          record.Processor.GroupMask[0].Mask = mask & all;
          AppendRecord(out, record);
        }
      }
    }
    if (Wants(relationship, RelationNumaNode)) {
      SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX record{};
      record.Relationship = RelationNumaNode;
      record.Size = sizeof(record);
      record.NumaNode.NodeNumber = 0;
      record.NumaNode.GroupCount = 1;
      record.NumaNode.GroupMask.Mask = all;
      AppendRecord(out, record);
    }
    if (Wants(relationship, RelationCache)) {
      DWORD cpu = 0;
      for (std::size_t level = 0; level < topology.levelCores.size(); ++level) {
        const DWORD logical = topology.levelLogical[level];
        const DWORD perL2 = std::max<DWORD>(1, topology.levelCpusPerL2[level]);
        for (DWORD first = 0; first < logical; first += perL2) {
          SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX record{};
          record.Relationship = RelationCache;
          record.Size = sizeof(record);
          record.Cache.Level = 2;
          record.Cache.Associativity = 0xFF;
          record.Cache.LineSize = static_cast<WORD>(topology.lineSize);
          record.Cache.CacheSize = topology.levelL2Bytes[level];
          record.Cache.Type = CacheUnified;
          record.Cache.GroupCount = 1;
          KAFFINITY mask = 0;
          for (DWORD i = first; i < first + perL2 && i < logical && cpu < 64; ++i, ++cpu) {
            mask |= KAFFINITY{1} << cpu;
          }
          record.Cache.GroupMask.Mask = mask & all;
          AppendRecord(out, record);
        }
      }
    }
    if (Wants(relationship, RelationProcessorPackage)) {
      SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX record{};
      record.Relationship = RelationProcessorPackage;
      record.Size = sizeof(record);
      record.Processor.GroupCount = 1;
      record.Processor.GroupMask[0].Mask = all;
      AppendRecord(out, record);
    }
    if (Wants(relationship, RelationGroup)) {
      SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX record{};
      record.Relationship = RelationGroup;
      record.Size = sizeof(record);
      record.Group.MaximumGroupCount = 1;
      record.Group.ActiveGroupCount = 1;
      record.Group.GroupInfo[0].MaximumProcessorCount = static_cast<BYTE>(std::min<DWORD>(topology.logical, 64));
      record.Group.GroupInfo[0].ActiveProcessorCount = static_cast<BYTE>(std::min<DWORD>(topology.logical, 64));
      record.Group.GroupInfo[0].ActiveProcessorMask = all;
      AppendRecord(out, record);
    }

    const DWORD needed = static_cast<DWORD>(out.size());
    if (buffer == nullptr || *returnedLength < needed) {
      *returnedLength = needed;
      ::SetLastError(ERROR_INSUFFICIENT_BUFFER);
      return FALSE;
    }
    std::memcpy(buffer, out.data(), out.size());
    *returnedLength = needed;
    return TRUE;
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

DWORD GetActiveProcessorCount(const WORD groupNumber) noexcept
{
  if (groupNumber != 0 && groupNumber != ALL_PROCESSOR_GROUPS) {
    return 0;
  }
  return std::min<DWORD>(SystemTopology().logical, 64);
}

DWORD GetMaximumProcessorCount(const WORD groupNumber) noexcept
{
  return GetActiveProcessorCount(groupNumber);
}

WORD GetActiveProcessorGroupCount() noexcept
{
  return 1;
}
