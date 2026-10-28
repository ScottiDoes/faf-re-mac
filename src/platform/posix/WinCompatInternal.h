#pragma once

// Shared internals of the WinCompat*.cpp implementations (Errors, Sync, Threads, Time,
// Files, Strings, Memory, Modules, Misc). Not part of the force-included surface: engine
// code never includes this.
//
// Kernel objects: every Win32 HANDLE the layer hands out (events, mutexes, semaphores,
// threads, files, find handles, mappings, snapshots) is a slot in one handle table, with
// values 4, 8, 12, ... as Windows uses. The slot holds one reference on a refcounted
// KernelObject; CloseHandle drops it. The pseudo handles GetCurrentProcess() (-1) and
// GetCurrentThread() (-2) are recognised wherever a process or thread handle is taken.
//
// Waiting: one process-wide wait lock guards every waitable object's state and its list
// of waiting threads. A waiting thread sleeps on its own condition variable (in its
// ThreadObject) and is notified by whoever changes the state of an object it waits on, so
// WaitForMultipleObjects (wait-any and wait-all), mutex ownership, semaphore counts and
// APC delivery to alertable waits are all decided atomically under that lock.

#if !defined(FAF_POSIX)
#error "POSIX build only"
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include <mach/mach_types.h>
#include <pthread.h>

namespace faf_posix
{
  // --- Allocation ----------------------------------------------------------------------------
  // The engine replaces the global operator new with its own allocator (gpg/core/utils/
  // Global.cpp), which itself calls VirtualAlloc, InitOnceExecuteOnce, SRW locks,
  // GetEnvironmentVariableA, GetCommandLineW and the heap functions while it starts up or
  // holds its locks. Those compat paths must never re-enter it: their containers use this
  // system-heap allocator instead of std::allocator.
  template <class T>
  struct SystemAllocator
  {
    using value_type = T;
    SystemAllocator() noexcept = default;
    template <class U>
    SystemAllocator(const SystemAllocator<U>&) noexcept
    {
    }
    T* allocate(const std::size_t count)
    {
      void* const memory = std::malloc(count * sizeof(T));
      if (memory == nullptr) {
        throw std::bad_alloc();
      }
      return static_cast<T*>(memory);
    }
    void deallocate(T* const memory, std::size_t) noexcept { std::free(memory); }
    template <class U>
    bool operator==(const SystemAllocator<U>&) const noexcept
    {
      return true;
    }
    template <class U>
    bool operator!=(const SystemAllocator<U>&) const noexcept
    {
      return false;
    }
  };

  // --- Errors ------------------------------------------------------------------------------
  DWORD Win32ErrorFromErrno(int error) noexcept;
  void SetLastErrorFromErrno(int error) noexcept;

  // --- Text ----------------------------------------------------------------------------------
  // wchar_t is UTF-32 on macOS. Invalid input is replaced (U+FFFD), never dropped silently.
  constexpr std::size_t kNulTerminated = static_cast<std::size_t>(-1);
  std::string Utf8FromWide(const wchar_t* text, std::size_t length = kNulTerminated);
  std::wstring WideFromUtf8(const char* text, std::size_t length = kNulTerminated);
  // Allocation-free decode of NUL-terminated UTF-8 into `out` (if not null, room for
  // `capacity` characters including the terminator). Returns the decoded length.
  std::size_t DecodeUtf8Raw(const char* text, wchar_t* out, std::size_t capacity) noexcept;

  // Copies `value` into a Win32 output buffer of `capacity` characters with Windows'
  // return convention for "query" APIs: on success the length without the terminator,
  // when too small the required size including the terminator (nothing written).
  DWORD CopyOutString(const std::string& value, char* buffer, DWORD capacity) noexcept;
  DWORD CopyOutString(const std::wstring& value, wchar_t* buffer, DWORD capacity) noexcept;

  // --- Paths -------------------------------------------------------------------------------
  // Windows accepts both separators; POSIX only '/'. A path handed to a compat file API is
  // converted with these (separators only: no drive letters, no case folding).
  std::string NativePath(const char* path);
  std::string NativePath(const wchar_t* path);
  // NativePath plus case-insensitive resolution of every component that exists (on a
  // case-sensitive volume, a component missing with the given case is looked up ignoring
  // case); components that do not exist are kept as given.
  std::string ResolvedPath(const char* path);
  std::string ResolvedPath(const wchar_t* path);
  // A POSIX path as the engine expects a system path: "/a/b" -> "C:\a\b".
  std::string WindowsPath(const std::string& posixPath);
  // The absolute, lexically normalised POSIX form of a native path (cwd-relative input).
  std::string AbsolutePosixPath(const std::string& nativePath);

  // --- Kernel objects ------------------------------------------------------------------------
  enum class ObjectKind : std::uint8_t
  {
    Event,
    Mutex,
    Semaphore,
    Thread,
    File,
    Find,
    FileMapping,
    Snapshot,
    Process,
  };

  struct ThreadObject;
  struct WaitBlock;

  struct KernelObject
  {
    explicit KernelObject(ObjectKind objectKind) noexcept : kind(objectKind) {}
    KernelObject(const KernelObject&) = delete;
    KernelObject& operator=(const KernelObject&) = delete;
    virtual ~KernelObject() = default;

    // Waitable protocol; called with the wait lock held. Objects that are not
    // synchronisation objects (files, mappings, ...) count as always signalled, as a file
    // handle with no I/O pending does on Windows.
    virtual bool IsSignaled(const ThreadObject* waiter) const noexcept { (void)waiter; return true; }
    // Consumes the signal for `waiter` (auto-reset, mutex ownership, semaphore count).
    // Returns WAIT_OBJECT_0, or WAIT_ABANDONED for a mutex whose owner exited holding it.
    virtual DWORD Acquire(ThreadObject* waiter) noexcept { (void)waiter; return WAIT_OBJECT_0; }

    // Kernel objects come from the system heap, never the engine's operator new: thread
    // objects are created lazily inside GetCurrentThreadId, which the engine allocator's
    // own locks may call.
    static void* operator new(std::size_t size);
    static void* operator new(std::size_t size, const std::nothrow_t&) noexcept;
    static void operator delete(void* pointer) noexcept;
    static void operator delete(void* pointer, const std::nothrow_t&) noexcept;

    void AddRef() noexcept { refs.fetch_add(1, std::memory_order_relaxed); }
    // AddRef unless the object is already being destroyed (named-object lookups).
    bool TryAddRef() noexcept
    {
      long count = refs.load(std::memory_order_relaxed);
      while (count > 0) {
        if (refs.compare_exchange_weak(count, count + 1, std::memory_order_acq_rel)) {
          return true;
        }
      }
      return false;
    }
    void Release() noexcept
    {
      if (refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        delete this;
      }
    }

    const ObjectKind kind;
    std::atomic<long> refs{1};
    std::vector<WaitBlock*> waiters; // waits blocked on this object, oldest first (wait lock)
  };

  // Moves the caller's reference into a new handle slot.
  HANDLE InsertHandle(KernelObject* object) noexcept;
  // A new reference to the handle's object, or nullptr with ERROR_INVALID_HANDLE.
  // GetCurrentThread()'s pseudo handle resolves to the calling thread.
  KernelObject* ReferenceHandle(HANDLE handle) noexcept;
  template <class T>
  T* ReferenceHandleAs(HANDLE handle, ObjectKind kind) noexcept
  {
    KernelObject* const object = ReferenceHandle(handle);
    if (object == nullptr) {
      return nullptr;
    }
    if (object->kind != kind) {
      object->Release();
      ::SetLastError(ERROR_INVALID_HANDLE);
      return nullptr;
    }
    return static_cast<T*>(object);
  }
  // Removes the slot and drops its reference; false (ERROR_INVALID_HANDLE) for a bad
  // handle. `expected` restricts the kinds a closer accepts (FindClose, ...).
  bool CloseHandleSlot(HANDLE handle, const ObjectKind* expected = nullptr) noexcept;

  // Pseudo handles.
  inline HANDLE CurrentProcessPseudoHandle() noexcept { return reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(-1)); }
  inline HANDLE CurrentThreadPseudoHandle() noexcept { return reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(-2)); }

  // --- Waiting -------------------------------------------------------------------------------
  // The process-wide wait lock (a plain pthread mutex: usable before and after static
  // construction and destruction).
  extern pthread_mutex_t gWaitLock;
  struct WaitGuard
  {
    WaitGuard() noexcept { pthread_mutex_lock(&gWaitLock); }
    ~WaitGuard() { pthread_mutex_unlock(&gWaitLock); }
    WaitGuard(const WaitGuard&) = delete;
    WaitGuard& operator=(const WaitGuard&) = delete;
  };
  // One blocked wait (on the waiting thread's stack). As in the NT kernel, a wait is
  // satisfied when an object it waits on is signalled, by whoever signals it: an
  // auto-reset event set twice releases two waiters, a mutex is handed to its oldest
  // waiter, and so on.
  struct WaitBlock
  {
    ThreadObject* thread = nullptr;
    KernelObject* const* objects = nullptr;
    DWORD count = 0;
    bool waitAll = false;
    bool satisfied = false;
    DWORD result = WAIT_TIMEOUT;
  };
  // Satisfies the waits on `object` that its new state allows (oldest first) and wakes
  // their threads; caller holds the wait lock and calls it after every signalling change.
  void NotifyWaiters(KernelObject* object) noexcept;

  // The Win32 wait: `objects` are referenced by the caller. Returns WAIT_OBJECT_0 + i,
  // WAIT_ABANDONED + i, WAIT_TIMEOUT, or WAIT_IO_COMPLETION after running the calling
  // thread's queued APCs (alertable waits only).
  DWORD WaitForObjects(DWORD count, KernelObject* const* objects, bool waitAll, DWORD timeoutMs, bool alertable) noexcept;

  // --- Threads -------------------------------------------------------------------------------
  using ApcRoutine = void (*)(ULONG_PTR);
  using ThreadStart = DWORD (*)(void*);

  struct MutexObject;

  struct ThreadObject final : KernelObject
  {
    ThreadObject() noexcept : KernelObject(ObjectKind::Thread) { pthread_cond_init(&wake, nullptr); }
    ~ThreadObject() override { pthread_cond_destroy(&wake); }

    bool IsSignaled(const ThreadObject* waiter) const noexcept override { (void)waiter; return exited; }

    DWORD id = 0;
    pthread_t pthread{};
    mach_port_t machThread = 0;
    bool started = false;           // pthread exists (false only while CreateThread sets up)
    bool exited = false;            // wait lock
    DWORD exitCode = 0x103;         // STILL_ACTIVE until it exits
    LONG suspendCount = 0;          // CREATE_SUSPENDED gate (wait lock), then mach suspends
    int priority = 0;               // THREAD_PRIORITY_* last set
    DWORD_PTR affinityMask = 0;     // last SetThreadAffinityMask value (0: process mask)
    DWORD idealProcessor = 0;

    bool gated = false;             // CreateThread'd thread still waiting at its start gate
    ThreadObject* registryNext = nullptr; // live-thread registry (intrusive, no allocation)
    ThreadObject* registryPrev = nullptr;

    // Start parameters of a CreateThread/_beginthreadex thread.
    ThreadStart start = nullptr;
    void* startParameter = nullptr;

    // Wait state (wait lock).
    pthread_cond_t wake;
    bool alertableWait = false;
    std::deque<std::pair<ApcRoutine, ULONG_PTR>> apcs;
    std::vector<MutexObject*> ownedMutexes;
  };

  // The calling thread's object; created (and registered under a fresh id) on first use
  // by a thread the layer did not start.
  ThreadObject* CurrentThread() noexcept;
  DWORD CurrentThreadId() noexcept;
  // A new reference to the live thread with that id, or nullptr.
  ThreadObject* FindThread(DWORD id) noexcept;
  // Runs the calling thread's queued APCs (outside the wait lock). Returns how many ran.
  std::size_t RunQueuedApcs(ThreadObject* self) noexcept;
  // Ids of every live thread the layer knows (tool help snapshots).
  std::vector<DWORD> LiveThreadIds();
  // Called by the mutex code on thread exit to abandon what the thread still owns.
  void AbandonOwnedMutexes(ThreadObject* thread) noexcept;

  // --- Time ----------------------------------------------------------------------------------
  // 100 ns intervals between 1601-01-01 and 1970-01-01 (FILETIME epoch to Unix epoch).
  constexpr std::uint64_t kFileTimeUnixEpoch = 116444736000000000ull;
  FILETIME FileTimeFromUnix(std::int64_t seconds, std::int64_t nanoseconds) noexcept;
  std::uint64_t FileTimeToU64(const FILETIME& time) noexcept;
  FILETIME FileTimeFromU64(std::uint64_t value) noexcept;
} // namespace faf_posix
