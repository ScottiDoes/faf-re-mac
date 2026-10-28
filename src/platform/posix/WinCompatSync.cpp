// Win32 on POSIX: Sync (declarations in compat/Sync.h).
//
// The handle table, the kernel synchronisation objects (events, mutexes, semaphores), the
// wait functions over every waitable handle, critical sections, SRW locks, condition
// variables and one-time initialisation. See WinCompatInternal.h for the model.

#include "WinCompatInternal.h"

#include "platform/posix/shell/ShellInternal.h" // MsgWaitForMultipleObjects(Ex): the window system's input

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <map>
#include <new>
#include <string>
#include <vector>

#include <sched.h>
#include <time.h>

namespace faf_posix
{
  pthread_mutex_t gWaitLock = PTHREAD_MUTEX_INITIALIZER;

  void* KernelObject::operator new(const std::size_t size)
  {
    void* const pointer = std::malloc(size);
    if (pointer == nullptr) {
      throw std::bad_alloc();
    }
    return pointer;
  }

  void* KernelObject::operator new(const std::size_t size, const std::nothrow_t&) noexcept
  {
    return std::malloc(size);
  }

  void KernelObject::operator delete(void* const pointer) noexcept
  {
    std::free(pointer);
  }

  void KernelObject::operator delete(void* const pointer, const std::nothrow_t&) noexcept
  {
    std::free(pointer);
  }
} // namespace faf_posix

using namespace faf_posix;

namespace
{
  // --- Handle table ------------------------------------------------------------------------
  // Slot i is handle value (i + 1) * 4, as Windows numbers its handles. Never destroyed, so
  // handles closed from static destructors still work.
  struct HandleTable
  {
    pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    std::vector<KernelObject*> slots;
    std::vector<std::uint32_t> freeSlots;
  };

  HandleTable& Handles() noexcept
  {
    static HandleTable* const table = new HandleTable();
    return *table;
  }

  bool SlotOf(const HANDLE handle, std::size_t* const slot) noexcept
  {
    const auto value = reinterpret_cast<std::uintptr_t>(handle);
    if (value == 0 || (value & 3u) != 0 || value > 0xFFFFFFFCu) {
      return false;
    }
    *slot = (value >> 2) - 1;
    return true;
  }

  // --- Monotonic deadlines ---------------------------------------------------------------
  std::uint64_t MonotonicNs() noexcept
  {
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
  }

  // Sleeps on `condition` (with gWaitLock held) until signalled or `deadlineNs` passes.
  // Returns false once the deadline has passed.
  bool WaitUntil(pthread_cond_t* const condition, const bool infinite, const std::uint64_t deadlineNs) noexcept
  {
    if (infinite) {
      pthread_cond_wait(condition, &gWaitLock);
      return true;
    }
    const std::uint64_t now = MonotonicNs();
    if (now >= deadlineNs) {
      return false;
    }
    const std::uint64_t remaining = deadlineNs - now;
    timespec relative{};
    relative.tv_sec = static_cast<time_t>(remaining / 1000000000ull);
    relative.tv_nsec = static_cast<long>(remaining % 1000000000ull);
    pthread_cond_timedwait_relative_np(condition, &gWaitLock, &relative);
    return true;
  }

  // --- Synchronisation objects -------------------------------------------------------------
  struct NamedObjects
  {
    pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    std::map<std::string, KernelObject*> byName;
  };

  NamedObjects& Names() noexcept
  {
    static NamedObjects* const names = new NamedObjects();
    return *names;
  }

  struct NamedKernelObject : KernelObject
  {
    using KernelObject::KernelObject;
    ~NamedKernelObject() override
    {
      if (!name.empty()) {
        NamedObjects& names = Names();
        pthread_mutex_lock(&names.lock);
        const auto it = names.byName.find(name);
        if (it != names.byName.end() && it->second == this) {
          names.byName.erase(it);
        }
        pthread_mutex_unlock(&names.lock);
      }
    }
    std::string name;
  };

  struct EventObject final : NamedKernelObject
  {
    EventObject(const bool manual, const bool initial) noexcept
      : NamedKernelObject(ObjectKind::Event), manualReset(manual), signaled(initial)
    {
    }
    bool IsSignaled(const ThreadObject*) const noexcept override { return signaled; }
    DWORD Acquire(ThreadObject*) noexcept override
    {
      if (!manualReset) {
        signaled = false;
      }
      return WAIT_OBJECT_0;
    }
    const bool manualReset;
    bool signaled;
  };

  struct SemaphoreObject final : NamedKernelObject
  {
    SemaphoreObject(const LONG initial, const LONG maximum) noexcept
      : NamedKernelObject(ObjectKind::Semaphore), count(initial), maximumCount(maximum)
    {
    }
    bool IsSignaled(const ThreadObject*) const noexcept override { return count > 0; }
    DWORD Acquire(ThreadObject*) noexcept override
    {
      --count;
      return WAIT_OBJECT_0;
    }
    LONG count;
    const LONG maximumCount;
  };
} // namespace

namespace faf_posix
{
  struct MutexObject final : NamedKernelObject
  {
    MutexObject() noexcept : NamedKernelObject(ObjectKind::Mutex) {}
    ~MutexObject() override
    {
      WaitGuard guard;
      if (owner != nullptr) {
        auto& owned = owner->ownedMutexes;
        owned.erase(std::remove(owned.begin(), owned.end(), this), owned.end());
      }
    }
    bool IsSignaled(const ThreadObject* waiter) const noexcept override { return owner == nullptr || owner == waiter; }
    DWORD Acquire(ThreadObject* waiter) noexcept override
    {
      if (owner == waiter) {
        ++recursion;
        return WAIT_OBJECT_0;
      }
      owner = waiter;
      recursion = 1;
      waiter->ownedMutexes.push_back(this);
      const DWORD result = abandoned ? WAIT_ABANDONED : WAIT_OBJECT_0;
      abandoned = false;
      return result;
    }
    ThreadObject* owner = nullptr;
    LONG recursion = 0;
    bool abandoned = false;
  };

  void AbandonOwnedMutexes(ThreadObject* const thread) noexcept
  {
    for (MutexObject* const mutex : thread->ownedMutexes) {
      mutex->owner = nullptr;
      mutex->recursion = 0;
      mutex->abandoned = true;
      NotifyWaiters(mutex);
    }
    thread->ownedMutexes.clear();
  }

  HANDLE InsertHandle(KernelObject* const object) noexcept
  {
    if (object == nullptr) {
      return nullptr;
    }
    HandleTable& table = Handles();
    pthread_mutex_lock(&table.lock);
    std::size_t slot = 0;
    try {
      if (!table.freeSlots.empty()) {
        slot = table.freeSlots.back();
        table.freeSlots.pop_back();
        table.slots[slot] = object;
      } else {
        slot = table.slots.size();
        table.slots.push_back(object);
      }
    } catch (...) {
      pthread_mutex_unlock(&table.lock);
      object->Release();
      ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return nullptr;
    }
    pthread_mutex_unlock(&table.lock);
    return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>((slot + 1) << 2));
  }

  KernelObject* ReferenceHandle(const HANDLE handle) noexcept
  {
    if (handle == CurrentThreadPseudoHandle()) {
      ThreadObject* const self = CurrentThread();
      self->AddRef();
      return self;
    }
    std::size_t slot = 0;
    if (!SlotOf(handle, &slot)) {
      ::SetLastError(ERROR_INVALID_HANDLE);
      return nullptr;
    }
    HandleTable& table = Handles();
    pthread_mutex_lock(&table.lock);
    KernelObject* const object = slot < table.slots.size() ? table.slots[slot] : nullptr;
    if (object != nullptr) {
      object->AddRef();
    }
    pthread_mutex_unlock(&table.lock);
    if (object == nullptr) {
      ::SetLastError(ERROR_INVALID_HANDLE);
    }
    return object;
  }

  bool CloseHandleSlot(const HANDLE handle, const ObjectKind* const expected) noexcept
  {
    std::size_t slot = 0;
    if (!SlotOf(handle, &slot)) {
      ::SetLastError(ERROR_INVALID_HANDLE);
      return false;
    }
    HandleTable& table = Handles();
    pthread_mutex_lock(&table.lock);
    KernelObject* const object = slot < table.slots.size() ? table.slots[slot] : nullptr;
    const bool accepted = object != nullptr && (expected == nullptr || object->kind == *expected);
    if (accepted) {
      table.slots[slot] = nullptr;
      try {
        table.freeSlots.push_back(static_cast<std::uint32_t>(slot));
      } catch (...) {
        // The slot is lost, the handle is still closed.
      }
    }
    pthread_mutex_unlock(&table.lock);
    if (!accepted) {
      ::SetLastError(ERROR_INVALID_HANDLE);
      return false;
    }
    object->Release();
    return true;
  }

  namespace
  {
    // Tries to satisfy `block` now (wait lock held). Consumes the signals it takes.
    bool TrySatisfy(WaitBlock& block) noexcept
    {
      ThreadObject* const self = block.thread;
      if (block.count != 0 && block.waitAll) {
        for (DWORD i = 0; i < block.count; ++i) {
          if (!block.objects[i]->IsSignaled(self)) {
            return false;
          }
        }
        block.result = WAIT_OBJECT_0;
        for (DWORD i = 0; i < block.count; ++i) {
          if (block.objects[i]->Acquire(self) == WAIT_ABANDONED) {
            block.result = WAIT_ABANDONED_0;
          }
        }
        block.satisfied = true;
        return true;
      }
      for (DWORD i = 0; i < block.count; ++i) {
        if (block.objects[i]->IsSignaled(self)) {
          block.result = block.objects[i]->Acquire(self) + i;
          block.satisfied = true;
          return true;
        }
      }
      return false;
    }
  } // namespace

  void NotifyWaiters(KernelObject* const object) noexcept
  {
    for (WaitBlock* const block : object->waiters) {
      if (block->satisfied) {
        continue;
      }
      if (TrySatisfy(*block)) {
        pthread_cond_signal(&block->thread->wake);
      }
    }
  }

  DWORD WaitForObjects(const DWORD count, KernelObject* const* const objects, const bool waitAll, const DWORD timeoutMs,
                       const bool alertable) noexcept
  {
    ThreadObject* const self = CurrentThread();
    const bool infinite = timeoutMs == INFINITE;
    const std::uint64_t deadline = infinite ? 0 : MonotonicNs() + static_cast<std::uint64_t>(timeoutMs) * 1000000ull;

    WaitBlock block;
    block.thread = self;
    block.objects = objects;
    block.count = count;
    block.waitAll = waitAll;

    pthread_mutex_lock(&gWaitLock);
    bool registered = false;
    bool runApcs = false;
    for (;;) {
      if (block.satisfied || TrySatisfy(block)) {
        break;
      }
      if (alertable && !self->apcs.empty()) {
        runApcs = true;
        block.result = WAIT_IO_COMPLETION;
        break;
      }
      if (timeoutMs == 0) {
        block.result = WAIT_TIMEOUT;
        break;
      }
      if (!registered) {
        try {
          for (DWORD i = 0; i < count; ++i) {
            objects[i]->waiters.push_back(&block);
          }
        } catch (...) {
          for (DWORD i = 0; i < count; ++i) {
            auto& list = objects[i]->waiters;
            list.erase(std::remove(list.begin(), list.end(), &block), list.end());
          }
          pthread_mutex_unlock(&gWaitLock);
          ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
          return WAIT_FAILED;
        }
        registered = true;
      }
      self->alertableWait = alertable;
      if (!WaitUntil(&self->wake, infinite, deadline)) {
        if (!block.satisfied) {
          block.result = WAIT_TIMEOUT;
        }
        break;
      }
    }
    self->alertableWait = false;
    if (registered) {
      for (DWORD i = 0; i < count; ++i) {
        auto& list = objects[i]->waiters;
        const auto it = std::find(list.begin(), list.end(), &block);
        if (it != list.end()) {
          list.erase(it);
        }
      }
    }
    pthread_mutex_unlock(&gWaitLock);

    if (runApcs) {
      (void)RunQueuedApcs(self);
    }
    return block.result;
  }
} // namespace faf_posix

namespace
{
  thread_local HANDLE tMessageEvent = nullptr;

  // Resolves `count` handles to referenced objects; false (ERROR_INVALID_HANDLE) if any is
  // bad, with nothing left referenced.
  bool ReferenceAll(const DWORD count, const HANDLE* const handles, KernelObject** const out) noexcept
  {
    for (DWORD i = 0; i < count; ++i) {
      out[i] = ReferenceHandle(handles[i]);
      if (out[i] == nullptr) {
        for (DWORD j = 0; j < i; ++j) {
          out[j]->Release();
        }
        ::SetLastError(ERROR_INVALID_HANDLE);
        return false;
      }
    }
    return true;
  }

  DWORD WaitHandles(const DWORD count, const HANDLE* const handles, const bool waitAll, const DWORD milliseconds,
                    const bool alertable, const HANDLE extra = nullptr) noexcept
  {
    if (count > MAXIMUM_WAIT_OBJECTS || (count != 0 && handles == nullptr)) {
      ::SetLastError(ERROR_INVALID_PARAMETER);
      return WAIT_FAILED;
    }
    KernelObject* objects[MAXIMUM_WAIT_OBJECTS + 1] = {};
    if (!ReferenceAll(count, handles, objects)) {
      return WAIT_FAILED;
    }
    DWORD total = count;
    if (extra != nullptr) {
      objects[total] = ReferenceHandle(extra);
      if (objects[total] != nullptr) {
        ++total;
      }
    }
    const DWORD result = WaitForObjects(total, objects, waitAll, milliseconds, alertable);
    for (DWORD i = 0; i < total; ++i) {
      objects[i]->Release();
    }
    return result;
  }

  bool ValidName(const std::string& name) noexcept
  {
    return name.find('\\') == std::string::npos || name.rfind("Local\\", 0) == 0 || name.rfind("Global\\", 0) == 0;
  }

  // Creates (or opens, when `create` is false) a named object. `make` builds a new one.
  template <class Make>
  HANDLE CreateNamed(const char* const nameUtf8, const ObjectKind kind, const bool create, Make&& make) noexcept
  {
    if (nameUtf8 == nullptr || nameUtf8[0] == '\0') {
      if (!create) {
        ::SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
      }
      NamedKernelObject* const object = make();
      if (object == nullptr) {
        ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
      }
      const HANDLE handle = InsertHandle(object);
      if (handle != nullptr) {
        ::SetLastError(ERROR_SUCCESS);
      }
      return handle;
    }

    std::string name(nameUtf8);
    if (!ValidName(name)) {
      ::SetLastError(ERROR_BAD_PATHNAME);
      return nullptr;
    }
    NamedObjects& names = Names();
    pthread_mutex_lock(&names.lock);
    const auto it = names.byName.find(name);
    if (it != names.byName.end() && it->second->TryAddRef()) {
      KernelObject* const existing = it->second;
      pthread_mutex_unlock(&names.lock);
      if (existing->kind != kind) {
        existing->Release();
        ::SetLastError(ERROR_INVALID_HANDLE);
        return nullptr;
      }
      const HANDLE handle = InsertHandle(existing);
      if (handle != nullptr) {
        ::SetLastError(ERROR_ALREADY_EXISTS);
      }
      return handle;
    }
    if (!create) {
      pthread_mutex_unlock(&names.lock);
      ::SetLastError(ERROR_FILE_NOT_FOUND);
      return nullptr;
    }
    NamedKernelObject* object = nullptr;
    try {
      object = make();
      object->name = name;
      names.byName[name] = object;
    } catch (...) {
      pthread_mutex_unlock(&names.lock);
      if (object != nullptr) {
        object->name.clear();
        object->Release();
      }
      ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return nullptr;
    }
    pthread_mutex_unlock(&names.lock);
    const HANDLE handle = InsertHandle(object);
    if (handle != nullptr) {
      ::SetLastError(ERROR_SUCCESS);
    }
    return handle;
  }

  std::string NameFromWide(const wchar_t* const name)
  {
    return name != nullptr ? Utf8FromWide(name) : std::string();
  }

  HANDLE CreateEventImpl(const char* const name, const bool manualReset, const bool initialState) noexcept
  {
    return CreateNamed(name, ObjectKind::Event, true, [&]() -> NamedKernelObject* {
      return new (std::nothrow) EventObject(manualReset, initialState);
    });
  }

  HANDLE CreateMutexImpl(const char* const name, const bool initialOwner) noexcept
  {
    ThreadObject* const self = CurrentThread();
    const HANDLE handle = CreateNamed(name, ObjectKind::Mutex, true, [&]() -> NamedKernelObject* {
      return new (std::nothrow) MutexObject();
    });
    // Initial ownership applies to a new mutex only: for an existing named one Windows
    // ignores bInitialOwner (and reports ERROR_ALREADY_EXISTS).
    if (handle != nullptr && initialOwner && ::GetLastError() != ERROR_ALREADY_EXISTS) {
      const DWORD lastError = ::GetLastError();
      KernelObject* const object = ReferenceHandle(handle);
      if (object != nullptr) {
        (void)WaitForObjects(1, &object, false, 0, false); // unowned: acquired at once
        object->Release();
      }
      (void)self;
      ::SetLastError(lastError);
    }
    return handle;
  }

  HANDLE CreateSemaphoreImpl(const char* const name, const LONG initialCount, const LONG maximumCount) noexcept
  {
    if (maximumCount <= 0 || initialCount < 0 || initialCount > maximumCount) {
      ::SetLastError(ERROR_INVALID_PARAMETER);
      return nullptr;
    }
    return CreateNamed(name, ObjectKind::Semaphore, true, [&]() -> NamedKernelObject* {
      return new (std::nothrow) SemaphoreObject(initialCount, maximumCount);
    });
  }

  HANDLE OpenNamed(const char* const name, const ObjectKind kind) noexcept
  {
    return CreateNamed(name, kind, false, []() -> NamedKernelObject* { return nullptr; });
  }

  // --- Parking lot -------------------------------------------------------------------------
  // Address-hashed buckets of (mutex, condition) for the pointer-sized lock words: a waiter
  // sleeps only while the word still holds the value it saw, checked under the bucket
  // lock, so a waker that changes the word and then wakes the bucket cannot be missed.
  struct ParkBucket
  {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
  };

#define FAF_PARK_BUCKET {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER}
#define FAF_PARK_BUCKET8 FAF_PARK_BUCKET, FAF_PARK_BUCKET, FAF_PARK_BUCKET, FAF_PARK_BUCKET, \
                         FAF_PARK_BUCKET, FAF_PARK_BUCKET, FAF_PARK_BUCKET, FAF_PARK_BUCKET
  ParkBucket gParkBuckets[64] = {FAF_PARK_BUCKET8, FAF_PARK_BUCKET8, FAF_PARK_BUCKET8, FAF_PARK_BUCKET8,
                                 FAF_PARK_BUCKET8, FAF_PARK_BUCKET8, FAF_PARK_BUCKET8, FAF_PARK_BUCKET8};
#undef FAF_PARK_BUCKET8
#undef FAF_PARK_BUCKET

  ParkBucket& BucketFor(const void* const address) noexcept
  {
    const auto value = reinterpret_cast<std::uintptr_t>(address);
    return gParkBuckets[((value >> 3) ^ (value >> 9)) & 63u];
  }

  using LockWord = std::atomic<std::uintptr_t>;

  LockWord* WordOf(void* const pointerField) noexcept
  {
    static_assert(sizeof(LockWord) == sizeof(void*), "lock word layout");
    return reinterpret_cast<LockWord*>(pointerField);
  }

  // Sleeps while *word == expected (or until the relative timeout). Returns false on timeout.
  bool ParkWhile(LockWord* const word, const std::uintptr_t expected, const bool infinite = true,
                 const std::uint64_t deadlineNs = 0) noexcept
  {
    ParkBucket& bucket = BucketFor(word);
    pthread_mutex_lock(&bucket.mutex);
    bool inTime = true;
    if (word->load(std::memory_order_acquire) == expected) {
      if (infinite) {
        pthread_cond_wait(&bucket.condition, &bucket.mutex);
      } else {
        const std::uint64_t now = MonotonicNs();
        if (now >= deadlineNs) {
          inTime = false;
        } else {
          const std::uint64_t remaining = deadlineNs - now;
          timespec relative{};
          relative.tv_sec = static_cast<time_t>(remaining / 1000000000ull);
          relative.tv_nsec = static_cast<long>(remaining % 1000000000ull);
          pthread_cond_timedwait_relative_np(&bucket.condition, &bucket.mutex, &relative);
        }
      }
    }
    pthread_mutex_unlock(&bucket.mutex);
    return inTime;
  }

  void UnparkAll(const void* const word) noexcept
  {
    ParkBucket& bucket = BucketFor(word);
    pthread_mutex_lock(&bucket.mutex);
    pthread_cond_broadcast(&bucket.condition);
    pthread_mutex_unlock(&bucket.mutex);
  }

  // SRW lock word: bit 0 exclusive owner, bit 1 "someone is parked", shared count from bit 2.
  constexpr std::uintptr_t kSrwExclusive = 1u;
  constexpr std::uintptr_t kSrwParked = 2u;
  constexpr std::uintptr_t kSrwShared = 4u;
  constexpr int kSrwSpins = 64;

  inline void CpuRelax() noexcept
  {
    __asm__ __volatile__("yield");
  }

  // Marks the word as having a parked waiter, then parks while it is unchanged.
  void ParkOnSrw(LockWord* const word, std::uintptr_t state) noexcept
  {
    if ((state & kSrwParked) == 0) {
      if (!word->compare_exchange_strong(state, state | kSrwParked, std::memory_order_relaxed)) {
        return; // changed under us: retry the acquire
      }
      state |= kSrwParked;
    }
    (void)ParkWhile(word, state);
  }
} // namespace

// --- Handles ------------------------------------------------------------------------------

BOOL CloseHandle(const HANDLE handle) noexcept
{
  if (handle == CurrentProcessPseudoHandle() || handle == CurrentThreadPseudoHandle()) {
    return TRUE;
  }
  return CloseHandleSlot(handle) ? TRUE : FALSE;
}

BOOL DuplicateHandle(const HANDLE sourceProcess, const HANDLE source, const HANDLE targetProcess,
                     LPHANDLE const target, const DWORD access, const BOOL inherit, const DWORD options) noexcept
{
  (void)access;
  (void)inherit;
  if (sourceProcess != CurrentProcessPseudoHandle() || targetProcess != CurrentProcessPseudoHandle()) {
    ::SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (source == CurrentProcessPseudoHandle()) {
    // A real handle to the own process: the pseudo handle serves every use the layer has.
    if (target != nullptr) {
      *target = CurrentProcessPseudoHandle();
    }
    return TRUE;
  }
  KernelObject* const object = ReferenceHandle(source);
  if (object == nullptr) {
    return FALSE;
  }
  const HANDLE duplicate = InsertHandle(object);
  if (duplicate == nullptr) {
    return FALSE;
  }
  if ((options & DUPLICATE_CLOSE_SOURCE) != 0 && source != CurrentThreadPseudoHandle()) {
    (void)CloseHandleSlot(source);
  }
  if (target != nullptr) {
    *target = duplicate;
  } else {
    (void)CloseHandleSlot(duplicate);
  }
  return TRUE;
}

BOOL GetHandleInformation(const HANDLE handle, LPDWORD const flags) noexcept
{
  KernelObject* const object = ReferenceHandle(handle);
  if (object == nullptr) {
    return FALSE;
  }
  object->Release();
  if (flags != nullptr) {
    *flags = 0;
  }
  return TRUE;
}

BOOL SetHandleInformation(const HANDLE handle, const DWORD mask, const DWORD flags) noexcept
{
  (void)mask;
  (void)flags;
  KernelObject* const object = ReferenceHandle(handle);
  if (object == nullptr) {
    return FALSE;
  }
  object->Release();
  return TRUE; // no child processes inherit handles on POSIX; nothing to record
}

// --- Events -------------------------------------------------------------------------------

HANDLE CreateEventA(LPSECURITY_ATTRIBUTES const attributes, const BOOL manualReset, const BOOL initialState,
                    LPCSTR const name) noexcept
{
  (void)attributes;
  return CreateEventImpl(name, manualReset != FALSE, initialState != FALSE);
}

HANDLE CreateEventW(LPSECURITY_ATTRIBUTES const attributes, const BOOL manualReset, const BOOL initialState,
                    LPCWSTR const name) noexcept
{
  (void)attributes;
  try {
    const std::string utf8 = NameFromWide(name);
    return CreateEventImpl(name != nullptr ? utf8.c_str() : nullptr, manualReset != FALSE, initialState != FALSE);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return nullptr;
  }
}

HANDLE CreateEventExA(LPSECURITY_ATTRIBUTES const attributes, LPCSTR const name, const DWORD flags,
                      const DWORD access) noexcept
{
  (void)access;
  return CreateEventA(attributes, (flags & CREATE_EVENT_MANUAL_RESET) != 0, (flags & CREATE_EVENT_INITIAL_SET) != 0,
                      name);
}

HANDLE CreateEventExW(LPSECURITY_ATTRIBUTES const attributes, LPCWSTR const name, const DWORD flags,
                      const DWORD access) noexcept
{
  (void)access;
  return CreateEventW(attributes, (flags & CREATE_EVENT_MANUAL_RESET) != 0, (flags & CREATE_EVENT_INITIAL_SET) != 0,
                      name);
}

HANDLE OpenEventA(const DWORD access, const BOOL inherit, LPCSTR const name) noexcept
{
  (void)access;
  (void)inherit;
  return OpenNamed(name, ObjectKind::Event);
}

HANDLE OpenEventW(const DWORD access, const BOOL inherit, LPCWSTR const name) noexcept
{
  try {
    return OpenEventA(access, inherit, NameFromWide(name).c_str());
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return nullptr;
  }
}

BOOL SetEvent(const HANDLE event) noexcept
{
  auto* const object = ReferenceHandleAs<EventObject>(event, ObjectKind::Event);
  if (object == nullptr) {
    return FALSE;
  }
  {
    WaitGuard guard;
    object->signaled = true;
    NotifyWaiters(object);
  }
  object->Release();
  return TRUE;
}

BOOL ResetEvent(const HANDLE event) noexcept
{
  auto* const object = ReferenceHandleAs<EventObject>(event, ObjectKind::Event);
  if (object == nullptr) {
    return FALSE;
  }
  {
    WaitGuard guard;
    object->signaled = false;
  }
  object->Release();
  return TRUE;
}

// --- Mutexes ------------------------------------------------------------------------------

HANDLE CreateMutexA(LPSECURITY_ATTRIBUTES const attributes, const BOOL initialOwner, LPCSTR const name) noexcept
{
  (void)attributes;
  return CreateMutexImpl(name, initialOwner != FALSE);
}

HANDLE CreateMutexW(LPSECURITY_ATTRIBUTES const attributes, const BOOL initialOwner, LPCWSTR const name) noexcept
{
  (void)attributes;
  try {
    const std::string utf8 = NameFromWide(name);
    return CreateMutexImpl(name != nullptr ? utf8.c_str() : nullptr, initialOwner != FALSE);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return nullptr;
  }
}

HANDLE OpenMutexA(const DWORD access, const BOOL inherit, LPCSTR const name) noexcept
{
  (void)access;
  (void)inherit;
  return OpenNamed(name, ObjectKind::Mutex);
}

HANDLE OpenMutexW(const DWORD access, const BOOL inherit, LPCWSTR const name) noexcept
{
  try {
    return OpenMutexA(access, inherit, NameFromWide(name).c_str());
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return nullptr;
  }
}

BOOL ReleaseMutex(const HANDLE mutex) noexcept
{
  auto* const object = ReferenceHandleAs<MutexObject>(mutex, ObjectKind::Mutex);
  if (object == nullptr) {
    return FALSE;
  }
  ThreadObject* const self = CurrentThread();
  BOOL result = TRUE;
  {
    WaitGuard guard;
    if (object->owner != self) {
      result = FALSE;
    } else if (--object->recursion == 0) {
      object->owner = nullptr;
      auto& owned = self->ownedMutexes;
      owned.erase(std::remove(owned.begin(), owned.end(), object), owned.end());
      NotifyWaiters(object);
    }
  }
  object->Release();
  if (result == FALSE) {
    ::SetLastError(ERROR_NOT_OWNER);
  }
  return result;
}

// --- Semaphores ---------------------------------------------------------------------------

HANDLE CreateSemaphoreA(LPSECURITY_ATTRIBUTES const attributes, const LONG initialCount, const LONG maximumCount,
                        LPCSTR const name) noexcept
{
  (void)attributes;
  return CreateSemaphoreImpl(name, initialCount, maximumCount);
}

HANDLE CreateSemaphoreW(LPSECURITY_ATTRIBUTES const attributes, const LONG initialCount, const LONG maximumCount,
                        LPCWSTR const name) noexcept
{
  (void)attributes;
  try {
    const std::string utf8 = NameFromWide(name);
    return CreateSemaphoreImpl(name != nullptr ? utf8.c_str() : nullptr, initialCount, maximumCount);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return nullptr;
  }
}

HANDLE OpenSemaphoreA(const DWORD access, const BOOL inherit, LPCSTR const name) noexcept
{
  (void)access;
  (void)inherit;
  return OpenNamed(name, ObjectKind::Semaphore);
}

HANDLE OpenSemaphoreW(const DWORD access, const BOOL inherit, LPCWSTR const name) noexcept
{
  try {
    return OpenSemaphoreA(access, inherit, NameFromWide(name).c_str());
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return nullptr;
  }
}

BOOL ReleaseSemaphore(const HANDLE semaphore, const LONG releaseCount, LPLONG const previousCount) noexcept
{
  auto* const object = ReferenceHandleAs<SemaphoreObject>(semaphore, ObjectKind::Semaphore);
  if (object == nullptr) {
    return FALSE;
  }
  BOOL result = TRUE;
  {
    WaitGuard guard;
    if (releaseCount <= 0 || releaseCount > object->maximumCount - object->count) {
      result = FALSE;
    } else {
      if (previousCount != nullptr) {
        *previousCount = object->count;
      }
      object->count += releaseCount;
      NotifyWaiters(object);
    }
  }
  object->Release();
  if (result == FALSE) {
    ::SetLastError(releaseCount <= 0 ? ERROR_INVALID_PARAMETER : ERROR_TOO_MANY_POSTS);
  }
  return result;
}

// --- Waits --------------------------------------------------------------------------------

DWORD WaitForSingleObject(const HANDLE handle, const DWORD milliseconds) noexcept
{
  return WaitHandles(1, &handle, false, milliseconds, false);
}

DWORD WaitForSingleObjectEx(const HANDLE handle, const DWORD milliseconds, const BOOL alertable) noexcept
{
  return WaitHandles(1, &handle, false, milliseconds, alertable != FALSE);
}

DWORD WaitForMultipleObjects(const DWORD count, const HANDLE* const handles, const BOOL waitAll,
                             const DWORD milliseconds) noexcept
{
  if (count == 0) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return WAIT_FAILED;
  }
  return WaitHandles(count, handles, waitAll != FALSE, milliseconds, false);
}

DWORD WaitForMultipleObjectsEx(const DWORD count, const HANDLE* const handles, const BOOL waitAll,
                               const DWORD milliseconds, const BOOL alertable) noexcept
{
  if (count == 0) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return WAIT_FAILED;
  }
  return WaitHandles(count, handles, waitAll != FALSE, milliseconds, alertable != FALSE);
}

DWORD SignalObjectAndWait(const HANDLE toSignal, const HANDLE toWaitOn, const DWORD milliseconds,
                          const BOOL alertable) noexcept
{
  KernelObject* const object = ReferenceHandle(toSignal);
  if (object == nullptr) {
    return WAIT_FAILED;
  }
  const ObjectKind kind = object->kind;
  object->Release();
  BOOL signaled = FALSE;
  if (kind == ObjectKind::Event) {
    signaled = SetEvent(toSignal);
  } else if (kind == ObjectKind::Mutex) {
    signaled = ReleaseMutex(toSignal);
  } else if (kind == ObjectKind::Semaphore) {
    signaled = ReleaseSemaphore(toSignal, 1, nullptr);
  } else {
    ::SetLastError(ERROR_INVALID_HANDLE);
  }
  if (signaled == FALSE) {
    return WAIT_FAILED;
  }
  return WaitHandles(1, &toWaitOn, false, milliseconds, alertable != FALSE);
}

DWORD MsgWaitForMultipleObjects(const DWORD count, const HANDLE* const handles, const BOOL waitAll,
                                const DWORD milliseconds, const DWORD wakeMask) noexcept
{
  return MsgWaitForMultipleObjectsEx(count, handles, milliseconds, wakeMask, waitAll != FALSE ? MWMO_WAITALL : 0);
}

DWORD MsgWaitForMultipleObjectsEx(const DWORD count, const HANDLE* const handles, const DWORD milliseconds,
                                  const DWORD wakeMask, const DWORD flags) noexcept
{
  const bool waitAll = (flags & MWMO_WAITALL) != 0;
  const bool alertable = (flags & MWMO_ALERTABLE) != 0;
  if (count >= MAXIMUM_WAIT_OBJECTS) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return WAIT_FAILED;
  }
  // With wait-all, the queue event is not part of the "all" set (Windows: all objects
  // signalled AND input available); without a message queue that pairing cannot occur.
  const HANDLE queueEvent = waitAll ? nullptr : tMessageEvent;

  // The main thread while the window system is up: its input is SDL's (AppKit's events reach
  // the process only through the main thread's run loop), so the wait polls it between
  // slices of the object wait. Objects and APCs still wake the wait at once; input within a
  // slice. Posted messages signal the queue event (result WAIT_OBJECT_0 + count). Any input
  // in the queue counts, as with MWMO_INPUTAVAILABLE (the engine's only caller passes it).
  if (!waitAll && (wakeMask & QS_ALLINPUT) != 0 && faf_posix::shell::IsActive() && faf_posix::shell::IsMainThread()) {
    constexpr DWORD kInputSliceMs = 2;
    const DWORD inputResult = WAIT_OBJECT_0 + count;
    // The queue event (auto-reset) may still be set from a message that was taken since:
    // input is reported only while some is there.
    const auto waitOnce = [&](DWORD milliseconds) -> DWORD {
      const DWORD result = WaitHandles(count, handles, false, milliseconds, alertable, queueEvent);
      if (result == inputResult && !faf_posix::shell::InputAvailable()) {
        return WAIT_TIMEOUT;
      }
      return result;
    };
    DWORD result = waitOnce(0);
    if (result != WAIT_TIMEOUT) {
      return result;
    }
    if (faf_posix::shell::InputAvailable()) {
      return inputResult;
    }
    const auto start = std::chrono::steady_clock::now();
    for (;;) {
      DWORD slice = kInputSliceMs;
      if (milliseconds != INFINITE) {
        const auto elapsed = static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
        if (elapsed >= milliseconds) {
          return WAIT_TIMEOUT;
        }
        slice = static_cast<DWORD>(std::min<std::uint64_t>(slice, milliseconds - elapsed));
      }
      result = waitOnce(slice);
      if (result != WAIT_TIMEOUT) {
        return result;
      }
      if (faf_posix::shell::InputAvailable()) {
        return inputResult;
      }
    }
  }
  return WaitHandles(count, handles, waitAll, milliseconds, alertable, queueEvent);
}

void faf_posixSetThreadMessageEvent(const HANDLE event) noexcept
{
  tMessageEvent = event;
}

// --- Critical sections --------------------------------------------------------------------

void InitializeCriticalSection(LPCRITICAL_SECTION const section) noexcept
{
  (void)InitializeCriticalSectionEx(section, 0, 0);
}

BOOL InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION const section, const DWORD spinCount) noexcept
{
  return InitializeCriticalSectionEx(section, spinCount, 0);
}

BOOL InitializeCriticalSectionEx(LPCRITICAL_SECTION const section, const DWORD spinCount, const DWORD flags) noexcept
{
  (void)flags;
  section->DebugInfo = nullptr;
  section->LockCount = -1;
  section->RecursionCount = 0;
  section->OwningThread = nullptr;
  section->LockSemaphore = nullptr;
  section->SpinCount = spinCount & 0x00FFFFFFu;
  section->faf_lock = OS_UNFAIR_LOCK_INIT;
  return TRUE;
}

DWORD SetCriticalSectionSpinCount(LPCRITICAL_SECTION const section, const DWORD spinCount) noexcept
{
  const DWORD previous = static_cast<DWORD>(section->SpinCount);
  section->SpinCount = spinCount & 0x00FFFFFFu;
  return previous;
}

void DeleteCriticalSection(LPCRITICAL_SECTION const section) noexcept
{
  section->OwningThread = nullptr;
  section->RecursionCount = 0;
}

namespace
{
  inline HANDLE OwnerTag() noexcept
  {
    return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(CurrentThreadId()));
  }

  inline std::atomic<HANDLE>& OwnerField(LPCRITICAL_SECTION const section) noexcept
  {
    static_assert(sizeof(std::atomic<HANDLE>) == sizeof(HANDLE), "owner field layout");
    return *reinterpret_cast<std::atomic<HANDLE>*>(&section->OwningThread);
  }
} // namespace

void EnterCriticalSection(LPCRITICAL_SECTION const section) noexcept
{
  const HANDLE self = OwnerTag();
  if (OwnerField(section).load(std::memory_order_relaxed) == self) {
    ++section->RecursionCount;
    return;
  }
  os_unfair_lock_lock(&section->faf_lock);
  OwnerField(section).store(self, std::memory_order_relaxed);
  section->RecursionCount = 1;
}

BOOL TryEnterCriticalSection(LPCRITICAL_SECTION const section) noexcept
{
  const HANDLE self = OwnerTag();
  if (OwnerField(section).load(std::memory_order_relaxed) == self) {
    ++section->RecursionCount;
    return TRUE;
  }
  if (!os_unfair_lock_trylock(&section->faf_lock)) {
    return FALSE;
  }
  OwnerField(section).store(self, std::memory_order_relaxed);
  section->RecursionCount = 1;
  return TRUE;
}

void LeaveCriticalSection(LPCRITICAL_SECTION const section) noexcept
{
  if (--section->RecursionCount > 0) {
    return;
  }
  section->RecursionCount = 0;
  OwnerField(section).store(nullptr, std::memory_order_relaxed);
  os_unfair_lock_unlock(&section->faf_lock);
}

// --- SRW locks ----------------------------------------------------------------------------

void InitializeSRWLock(PSRWLOCK const lock) noexcept
{
  lock->Ptr = nullptr;
}

void AcquireSRWLockExclusive(PSRWLOCK const lock) noexcept
{
  LockWord* const word = WordOf(&lock->Ptr);
  int spins = 0;
  for (;;) {
    std::uintptr_t state = word->load(std::memory_order_relaxed);
    if ((state & ~kSrwParked) == 0) {
      if (word->compare_exchange_weak(state, state | kSrwExclusive, std::memory_order_acquire)) {
        return;
      }
      continue;
    }
    if (spins < kSrwSpins) {
      ++spins;
      CpuRelax();
      continue;
    }
    ParkOnSrw(word, state);
  }
}

void AcquireSRWLockShared(PSRWLOCK const lock) noexcept
{
  LockWord* const word = WordOf(&lock->Ptr);
  int spins = 0;
  for (;;) {
    std::uintptr_t state = word->load(std::memory_order_relaxed);
    if ((state & kSrwExclusive) == 0) {
      if (word->compare_exchange_weak(state, state + kSrwShared, std::memory_order_acquire)) {
        return;
      }
      continue;
    }
    if (spins < kSrwSpins) {
      ++spins;
      CpuRelax();
      continue;
    }
    ParkOnSrw(word, state);
  }
}

void ReleaseSRWLockExclusive(PSRWLOCK const lock) noexcept
{
  LockWord* const word = WordOf(&lock->Ptr);
  const std::uintptr_t previous = word->exchange(0, std::memory_order_release);
  if ((previous & kSrwParked) != 0) {
    UnparkAll(word);
  }
}

void ReleaseSRWLockShared(PSRWLOCK const lock) noexcept
{
  LockWord* const word = WordOf(&lock->Ptr);
  std::uintptr_t state = word->load(std::memory_order_relaxed);
  for (;;) {
    std::uintptr_t next = state - kSrwShared;
    const bool last = (next & ~kSrwParked) == 0;
    const bool wake = last && (next & kSrwParked) != 0;
    if (wake) {
      next = 0;
    }
    if (word->compare_exchange_weak(state, next, std::memory_order_release, std::memory_order_relaxed)) {
      if (wake) {
        UnparkAll(word);
      }
      return;
    }
  }
}

BOOLEAN TryAcquireSRWLockExclusive(PSRWLOCK const lock) noexcept
{
  LockWord* const word = WordOf(&lock->Ptr);
  std::uintptr_t state = word->load(std::memory_order_relaxed);
  while ((state & ~kSrwParked) == 0) {
    if (word->compare_exchange_weak(state, state | kSrwExclusive, std::memory_order_acquire)) {
      return TRUE;
    }
  }
  return FALSE;
}

BOOLEAN TryAcquireSRWLockShared(PSRWLOCK const lock) noexcept
{
  LockWord* const word = WordOf(&lock->Ptr);
  std::uintptr_t state = word->load(std::memory_order_relaxed);
  while ((state & kSrwExclusive) == 0) {
    if (word->compare_exchange_weak(state, state + kSrwShared, std::memory_order_acquire)) {
      return TRUE;
    }
  }
  return FALSE;
}

// --- Condition variables -------------------------------------------------------------------
// The word is a wake sequence number: a sleeper records it, drops its lock, and parks while
// it is unchanged; Wake* bumps it (so no wake between unlock and park is lost).

void InitializeConditionVariable(PCONDITION_VARIABLE const variable) noexcept
{
  variable->Ptr = nullptr;
}

void WakeConditionVariable(PCONDITION_VARIABLE const variable) noexcept
{
  // Waking every sleeper is a valid (spurious-wakeup-permitted) WakeConditionVariable.
  WakeAllConditionVariable(variable);
}

void WakeAllConditionVariable(PCONDITION_VARIABLE const variable) noexcept
{
  LockWord* const word = WordOf(&variable->Ptr);
  word->fetch_add(1, std::memory_order_release);
  UnparkAll(word);
}

namespace
{
  template <class Unlock, class Relock>
  BOOL SleepOnCondition(PCONDITION_VARIABLE const variable, const DWORD milliseconds, Unlock&& unlock,
                        Relock&& relock) noexcept
  {
    LockWord* const word = WordOf(&variable->Ptr);
    const std::uintptr_t sequence = word->load(std::memory_order_acquire);
    const bool infinite = milliseconds == INFINITE;
    const std::uint64_t deadline = infinite ? 0 : MonotonicNs() + static_cast<std::uint64_t>(milliseconds) * 1000000ull;
    unlock();
    bool inTime = true;
    while (word->load(std::memory_order_acquire) == sequence && inTime) {
      inTime = ParkWhile(word, sequence, infinite, deadline);
      if (!infinite && MonotonicNs() >= deadline && word->load(std::memory_order_acquire) == sequence) {
        inTime = false;
      }
    }
    relock();
    if (!inTime) {
      ::SetLastError(ERROR_TIMEOUT);
      return FALSE;
    }
    return TRUE;
  }
} // namespace

BOOL SleepConditionVariableCS(PCONDITION_VARIABLE const variable, PCRITICAL_SECTION const section,
                              const DWORD milliseconds) noexcept
{
  // The critical section is released completely and re-entered to the same depth.
  const LONG depth = section->RecursionCount;
  return SleepOnCondition(
    variable, milliseconds,
    [&] {
      section->RecursionCount = 1;
      LeaveCriticalSection(section);
    },
    [&] {
      EnterCriticalSection(section);
      section->RecursionCount = depth;
    });
}

BOOL SleepConditionVariableSRW(PCONDITION_VARIABLE const variable, PSRWLOCK const lock, const DWORD milliseconds,
                               const ULONG flags) noexcept
{
  const bool shared = (flags & CONDITION_VARIABLE_LOCKMODE_SHARED) != 0;
  return SleepOnCondition(
    variable, milliseconds,
    [&] {
      if (shared) {
        ReleaseSRWLockShared(lock);
      } else {
        ReleaseSRWLockExclusive(lock);
      }
    },
    [&] {
      if (shared) {
        AcquireSRWLockShared(lock);
      } else {
        AcquireSRWLockExclusive(lock);
      }
    });
}

// --- One-time initialisation --------------------------------------------------------------
// Word: 0 = not run, 1 = running, (context | 2) = done.

void InitOnceInitialize(PINIT_ONCE const initOnce) noexcept
{
  initOnce->Ptr = nullptr;
}

BOOL InitOnceExecuteOnce(PINIT_ONCE const initOnce, PINIT_ONCE_FN const initFn, PVOID const parameter,
                         LPVOID* const context) noexcept
{
  constexpr std::uintptr_t kRunning = 1u;
  constexpr std::uintptr_t kDone = 2u;
  LockWord* const word = WordOf(&initOnce->Ptr);
  for (;;) {
    std::uintptr_t state = word->load(std::memory_order_acquire);
    if ((state & kDone) != 0) {
      if (context != nullptr) {
        *context = reinterpret_cast<LPVOID>(state & ~std::uintptr_t{3});
      }
      return TRUE;
    }
    if (state == 0) {
      if (!word->compare_exchange_strong(state, kRunning, std::memory_order_acquire)) {
        continue;
      }
      PVOID produced = nullptr;
      const BOOL ok = initFn(initOnce, parameter, &produced);
      if (ok == FALSE) {
        word->store(0, std::memory_order_release);
        UnparkAll(word);
        return FALSE;
      }
      const std::uintptr_t done = (reinterpret_cast<std::uintptr_t>(produced) & ~std::uintptr_t{3}) | kDone;
      word->store(done, std::memory_order_release);
      UnparkAll(word);
      if (context != nullptr) {
        *context = reinterpret_cast<LPVOID>(done & ~std::uintptr_t{3});
      }
      return TRUE;
    }
    (void)ParkWhile(word, kRunning);
  }
}
