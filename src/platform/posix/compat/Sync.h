#pragma once

// Win32 on POSIX: Sync. Included only by ../WinCompat.h (see its rules).
// Implementations: ../WinCompatSync.cpp.

#include <atomic>
#include <type_traits>

// --- Interlocked operations ------------------------------------------------------------
// Windows `long` is 32 bits, and the engine routinely reinterprets a 32-bit field as
// `volatile long*` to hand it to these (e.g. moho/misc/InstanceCounter.h). On macOS a
// `long` is 64 bits, so a 64-bit atomic there would also write the next 4 bytes. The
// `long` overloads therefore keep the Windows meaning: a 32-bit operation on the first
// four bytes, returning a 32-bit result widened to `long`.
namespace faf_posix_detail
{
  template <class T>
  inline volatile std::atomic<T>* AsAtomic(volatile void* p) noexcept
  {
    static_assert(sizeof(std::atomic<T>) == sizeof(T), "lock-free atomic layout");
    return reinterpret_cast<volatile std::atomic<T>*>(p);
  }
} // namespace faf_posix_detail

#define FAF_INTERLOCKED_32(PTR_T)                                                          \
  inline LONG faf_posixInterlockedExchangeAdd(volatile PTR_T* p, LONG v) noexcept                  \
  {                                                                                        \
    return faf_posix_detail::AsAtomic<std::int32_t>(p)->fetch_add(v);                      \
  }                                                                                        \
  inline LONG faf_posixInterlockedIncrement(volatile PTR_T* p) noexcept                            \
  {                                                                                        \
    return faf_posix_detail::AsAtomic<std::int32_t>(p)->fetch_add(1) + 1;                  \
  }                                                                                        \
  inline LONG faf_posixInterlockedDecrement(volatile PTR_T* p) noexcept                            \
  {                                                                                        \
    return faf_posix_detail::AsAtomic<std::int32_t>(p)->fetch_sub(1) - 1;                  \
  }                                                                                        \
  inline LONG faf_posixInterlockedExchange(volatile PTR_T* p, LONG v) noexcept                     \
  {                                                                                        \
    return faf_posix_detail::AsAtomic<std::int32_t>(p)->exchange(v);                       \
  }                                                                                        \
  inline LONG faf_posixInterlockedCompareExchange(volatile PTR_T* p, LONG exchange, LONG comparand) noexcept \
  {                                                                                        \
    std::int32_t expected = comparand;                                                     \
    faf_posix_detail::AsAtomic<std::int32_t>(p)->compare_exchange_strong(expected, exchange); \
    return expected;                                                                       \
  }                                                                                        \
  inline LONG faf_posixInterlockedOr(volatile PTR_T* p, LONG v) noexcept                           \
  {                                                                                        \
    return faf_posix_detail::AsAtomic<std::int32_t>(p)->fetch_or(v);                       \
  }                                                                                        \
  inline LONG faf_posixInterlockedAnd(volatile PTR_T* p, LONG v) noexcept                          \
  {                                                                                        \
    return faf_posix_detail::AsAtomic<std::int32_t>(p)->fetch_and(v);                      \
  }

FAF_INTERLOCKED_32(LONG)
FAF_INTERLOCKED_32(long)
FAF_INTERLOCKED_32(unsigned int)
FAF_INTERLOCKED_32(unsigned long)
#undef FAF_INTERLOCKED_32

inline LONGLONG faf_posixInterlockedExchangeAdd64(volatile LONGLONG* p, LONGLONG v) noexcept
{
  return faf_posix_detail::AsAtomic<std::int64_t>(p)->fetch_add(v);
}
inline LONGLONG faf_posixInterlockedIncrement64(volatile LONGLONG* p) noexcept
{
  return faf_posix_detail::AsAtomic<std::int64_t>(p)->fetch_add(1) + 1;
}
inline LONGLONG faf_posixInterlockedDecrement64(volatile LONGLONG* p) noexcept
{
  return faf_posix_detail::AsAtomic<std::int64_t>(p)->fetch_sub(1) - 1;
}
inline LONGLONG faf_posixInterlockedExchange64(volatile LONGLONG* p, LONGLONG v) noexcept
{
  return faf_posix_detail::AsAtomic<std::int64_t>(p)->exchange(v);
}
inline LONGLONG faf_posixInterlockedCompareExchange64(volatile LONGLONG* p, LONGLONG exchange, LONGLONG comparand) noexcept
{
  std::int64_t expected = comparand;
  faf_posix_detail::AsAtomic<std::int64_t>(p)->compare_exchange_strong(expected, exchange);
  return expected;
}
inline void* faf_posixInterlockedExchangePointer(void* volatile* p, void* v) noexcept
{
  return faf_posix_detail::AsAtomic<void*>(p)->exchange(v);
}
inline void* faf_posixInterlockedCompareExchangePointer(void* volatile* p, void* exchange, void* comparand) noexcept
{
  void* expected = comparand;
  faf_posix_detail::AsAtomic<void*>(p)->compare_exchange_strong(expected, exchange);
  return expected;
}

// clang's -fms-extensions knows the underscore names as builtins, and on macOS those
// operate on a 64-bit `long`. Route both spellings to the functions above instead.
#define _InterlockedExchangeAdd64 faf_posixInterlockedExchangeAdd64
#define _InterlockedIncrement64 faf_posixInterlockedIncrement64
#define _InterlockedDecrement64 faf_posixInterlockedDecrement64
#define _InterlockedExchange64 faf_posixInterlockedExchange64
#define _InterlockedCompareExchange64 faf_posixInterlockedCompareExchange64
#define _InterlockedExchangePointer faf_posixInterlockedExchangePointer
#define _InterlockedCompareExchangePointer faf_posixInterlockedCompareExchangePointer
#define _InterlockedExchangeAdd faf_posixInterlockedExchangeAdd
#define _InterlockedIncrement faf_posixInterlockedIncrement
#define _InterlockedDecrement faf_posixInterlockedDecrement
#define _InterlockedExchange faf_posixInterlockedExchange
#define _InterlockedCompareExchange faf_posixInterlockedCompareExchange
#define _InterlockedOr faf_posixInterlockedOr
#define _InterlockedAnd faf_posixInterlockedAnd
#define InterlockedExchangeAdd64 faf_posixInterlockedExchangeAdd64
#define InterlockedIncrement64 faf_posixInterlockedIncrement64
#define InterlockedDecrement64 faf_posixInterlockedDecrement64
#define InterlockedExchange64 faf_posixInterlockedExchange64
#define InterlockedCompareExchange64 faf_posixInterlockedCompareExchange64
#define InterlockedExchangePointer faf_posixInterlockedExchangePointer
#define InterlockedCompareExchangePointer faf_posixInterlockedCompareExchangePointer
#define InterlockedExchangeAdd faf_posixInterlockedExchangeAdd
#define InterlockedIncrement faf_posixInterlockedIncrement
#define InterlockedDecrement faf_posixInterlockedDecrement
#define InterlockedExchange faf_posixInterlockedExchange
#define InterlockedCompareExchange faf_posixInterlockedCompareExchange
#define InterlockedOr faf_posixInterlockedOr
#define InterlockedAnd faf_posixInterlockedAnd

inline void MemoryBarrier() noexcept { std::atomic_thread_fence(std::memory_order_seq_cst); }
#define _ReadWriteBarrier() std::atomic_signal_fence(std::memory_order_seq_cst)
#define YieldProcessor() __asm__ __volatile__("yield")

// --- Kernel object handles -----------------------------------------------------------------
// Every handle below is a slot in the layer's handle table (WinCompatInternal.h): closed
// with CloseHandle, duplicable, waitable where Windows makes it waitable.
typedef struct _SECURITY_ATTRIBUTES {
  DWORD nLength;
  LPVOID lpSecurityDescriptor;
  BOOL bInheritHandle;
} SECURITY_ATTRIBUTES, *PSECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;

#define READ_CONTROL 0x00020000L
#define WRITE_DAC 0x00040000L
#define WRITE_OWNER 0x00080000L
#define SYNCHRONIZE 0x00100000L
#define STANDARD_RIGHTS_REQUIRED 0x000F0000L
#define STANDARD_RIGHTS_READ READ_CONTROL
#define STANDARD_RIGHTS_WRITE READ_CONTROL
#define STANDARD_RIGHTS_EXECUTE READ_CONTROL
#define STANDARD_RIGHTS_ALL 0x001F0000L
#define SPECIFIC_RIGHTS_ALL 0x0000FFFFL
#define MAXIMUM_ALLOWED 0x02000000L
#define EVENT_MODIFY_STATE 0x0002
#define EVENT_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0x3)
#define MUTEX_MODIFY_STATE 0x0001
#define MUTEX_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | MUTEX_MODIFY_STATE)
#define SEMAPHORE_MODIFY_STATE 0x0002
#define SEMAPHORE_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0x3)
#define CREATE_EVENT_MANUAL_RESET 0x00000001
#define CREATE_EVENT_INITIAL_SET 0x00000002
#define CREATE_MUTEX_INITIAL_OWNER 0x00000001
#define DUPLICATE_CLOSE_SOURCE 0x00000001
#define DUPLICATE_SAME_ACCESS 0x00000002
#define HANDLE_FLAG_INHERIT 0x00000001
#define HANDLE_FLAG_PROTECT_FROM_CLOSE 0x00000002

#define STATUS_WAIT_0 ((DWORD)0x00000000L)
#define STATUS_ABANDONED_WAIT_0 ((DWORD)0x00000080L)
#define STATUS_USER_APC ((DWORD)0x000000C0L)
#define STATUS_TIMEOUT ((DWORD)0x00000102L)
#define STATUS_PENDING ((DWORD)0x00000103L)
#define WAIT_ABANDONED_0 WAIT_ABANDONED
#define WAIT_IO_COMPLETION 0x000000C0u

BOOL CloseHandle(HANDLE handle) noexcept;
BOOL DuplicateHandle(HANDLE sourceProcess, HANDLE source, HANDLE targetProcess, LPHANDLE target, DWORD access,
                     BOOL inherit, DWORD options) noexcept;
BOOL GetHandleInformation(HANDLE handle, LPDWORD flags) noexcept;
BOOL SetHandleInformation(HANDLE handle, DWORD mask, DWORD flags) noexcept;

// Events, mutexes and semaphores. Named objects are shared by name within the process
// (CreateX of an existing name returns it with ERROR_ALREADY_EXISTS); there is no
// cross-process namespace.
HANDLE CreateEventA(LPSECURITY_ATTRIBUTES attributes, BOOL manualReset, BOOL initialState, LPCSTR name) noexcept;
HANDLE CreateEventW(LPSECURITY_ATTRIBUTES attributes, BOOL manualReset, BOOL initialState, LPCWSTR name) noexcept;
HANDLE CreateEventExA(LPSECURITY_ATTRIBUTES attributes, LPCSTR name, DWORD flags, DWORD access) noexcept;
HANDLE CreateEventExW(LPSECURITY_ATTRIBUTES attributes, LPCWSTR name, DWORD flags, DWORD access) noexcept;
HANDLE OpenEventA(DWORD access, BOOL inherit, LPCSTR name) noexcept;
HANDLE OpenEventW(DWORD access, BOOL inherit, LPCWSTR name) noexcept;
BOOL SetEvent(HANDLE event) noexcept;
BOOL ResetEvent(HANDLE event) noexcept;
HANDLE CreateMutexA(LPSECURITY_ATTRIBUTES attributes, BOOL initialOwner, LPCSTR name) noexcept;
HANDLE CreateMutexW(LPSECURITY_ATTRIBUTES attributes, BOOL initialOwner, LPCWSTR name) noexcept;
HANDLE OpenMutexA(DWORD access, BOOL inherit, LPCSTR name) noexcept;
HANDLE OpenMutexW(DWORD access, BOOL inherit, LPCWSTR name) noexcept;
BOOL ReleaseMutex(HANDLE mutex) noexcept;
HANDLE CreateSemaphoreA(LPSECURITY_ATTRIBUTES attributes, LONG initialCount, LONG maximumCount, LPCSTR name) noexcept;
HANDLE CreateSemaphoreW(LPSECURITY_ATTRIBUTES attributes, LONG initialCount, LONG maximumCount, LPCWSTR name) noexcept;
HANDLE OpenSemaphoreA(DWORD access, BOOL inherit, LPCSTR name) noexcept;
HANDLE OpenSemaphoreW(DWORD access, BOOL inherit, LPCWSTR name) noexcept;
BOOL ReleaseSemaphore(HANDLE semaphore, LONG releaseCount, LPLONG previousCount) noexcept;

// Waits. Alertable waits run the thread's queued APCs (QueueUserAPC) and return
// WAIT_IO_COMPLETION. Thread handles signal when the thread exits; a mutex whose owner
// exited while holding it is acquired with WAIT_ABANDONED.
DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds) noexcept;
DWORD WaitForSingleObjectEx(HANDLE handle, DWORD milliseconds, BOOL alertable) noexcept;
DWORD WaitForMultipleObjects(DWORD count, const HANDLE* handles, BOOL waitAll, DWORD milliseconds) noexcept;
DWORD WaitForMultipleObjectsEx(DWORD count, const HANDLE* handles, BOOL waitAll, DWORD milliseconds,
                               BOOL alertable) noexcept;
DWORD SignalObjectAndWait(HANDLE toSignal, HANDLE toWaitOn, DWORD milliseconds, BOOL alertable) noexcept;

// MsgWaitForMultipleObjects(Ex): a thread has no window message queue on POSIX, unless the
// window layer gives it one with faf_posixSetThreadMessageEvent: that event then stands for
// "input available" (result WAIT_OBJECT_0 + count), whatever the wake mask.
#define MWMO_WAITALL 0x0001
#define MWMO_ALERTABLE 0x0002
#define MWMO_INPUTAVAILABLE 0x0004
DWORD MsgWaitForMultipleObjects(DWORD count, const HANDLE* handles, BOOL waitAll, DWORD milliseconds,
                                DWORD wakeMask) noexcept;
DWORD MsgWaitForMultipleObjectsEx(DWORD count, const HANDLE* handles, DWORD milliseconds, DWORD wakeMask,
                                  DWORD flags) noexcept;
void faf_posixSetThreadMessageEvent(HANDLE event) noexcept;

// --- Critical sections ---------------------------------------------------------------------
// Recursive, unfair (as since Vista), on an os_unfair_lock. The Windows fields keep their
// meaning where callers read them: OwningThread is the owner's thread id, RecursionCount
// the owner's depth.
#include <os/lock.h>
typedef struct _RTL_CRITICAL_SECTION {
  PVOID DebugInfo;
  LONG LockCount;
  LONG RecursionCount;
  HANDLE OwningThread;
  HANDLE LockSemaphore;
  ULONG_PTR SpinCount;
  os_unfair_lock faf_lock;
} RTL_CRITICAL_SECTION, *PRTL_CRITICAL_SECTION, CRITICAL_SECTION, *PCRITICAL_SECTION, *LPCRITICAL_SECTION;

#define CRITICAL_SECTION_NO_DEBUG_INFO 0x01000000
void InitializeCriticalSection(LPCRITICAL_SECTION section) noexcept;
BOOL InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION section, DWORD spinCount) noexcept;
BOOL InitializeCriticalSectionEx(LPCRITICAL_SECTION section, DWORD spinCount, DWORD flags) noexcept;
DWORD SetCriticalSectionSpinCount(LPCRITICAL_SECTION section, DWORD spinCount) noexcept;
void DeleteCriticalSection(LPCRITICAL_SECTION section) noexcept;
void EnterCriticalSection(LPCRITICAL_SECTION section) noexcept;
BOOL TryEnterCriticalSection(LPCRITICAL_SECTION section) noexcept;
void LeaveCriticalSection(LPCRITICAL_SECTION section) noexcept;

// --- Slim reader/writer locks, condition variables, one-time init -------------------------
// Pointer-sized and zero-initialised like Windows' (SRWLOCK_INIT, ...), so they can live in
// any object, need no destruction and never allocate (the engine allocator uses them).
typedef struct _RTL_SRWLOCK {
  PVOID Ptr;
} RTL_SRWLOCK, SRWLOCK, *PRTL_SRWLOCK, *PSRWLOCK;
#define RTL_SRWLOCK_INIT {0}
#define SRWLOCK_INIT RTL_SRWLOCK_INIT
void InitializeSRWLock(PSRWLOCK lock) noexcept;
void AcquireSRWLockExclusive(PSRWLOCK lock) noexcept;
void AcquireSRWLockShared(PSRWLOCK lock) noexcept;
void ReleaseSRWLockExclusive(PSRWLOCK lock) noexcept;
void ReleaseSRWLockShared(PSRWLOCK lock) noexcept;
BOOLEAN TryAcquireSRWLockExclusive(PSRWLOCK lock) noexcept;
BOOLEAN TryAcquireSRWLockShared(PSRWLOCK lock) noexcept;

typedef struct _RTL_CONDITION_VARIABLE {
  PVOID Ptr;
} RTL_CONDITION_VARIABLE, CONDITION_VARIABLE, *PRTL_CONDITION_VARIABLE, *PCONDITION_VARIABLE;
#define RTL_CONDITION_VARIABLE_INIT {0}
#define CONDITION_VARIABLE_INIT RTL_CONDITION_VARIABLE_INIT
#define CONDITION_VARIABLE_LOCKMODE_SHARED 0x1
void InitializeConditionVariable(PCONDITION_VARIABLE variable) noexcept;
void WakeConditionVariable(PCONDITION_VARIABLE variable) noexcept;
void WakeAllConditionVariable(PCONDITION_VARIABLE variable) noexcept;
BOOL SleepConditionVariableCS(PCONDITION_VARIABLE variable, PCRITICAL_SECTION section, DWORD milliseconds) noexcept;
BOOL SleepConditionVariableSRW(PCONDITION_VARIABLE variable, PSRWLOCK lock, DWORD milliseconds, ULONG flags) noexcept;

typedef union _RTL_RUN_ONCE {
  PVOID Ptr;
} RTL_RUN_ONCE, INIT_ONCE, *PRTL_RUN_ONCE, *PINIT_ONCE, *LPINIT_ONCE;
#define RTL_RUN_ONCE_INIT {0}
#define INIT_ONCE_STATIC_INIT RTL_RUN_ONCE_INIT
#define INIT_ONCE_CHECK_ONLY 0x00000001UL
#define INIT_ONCE_ASYNC 0x00000002UL
#define INIT_ONCE_INIT_FAILED 0x00000004UL
#define INIT_ONCE_CTX_RESERVED_BITS 2
typedef BOOL(CALLBACK* PINIT_ONCE_FN)(PINIT_ONCE initOnce, PVOID parameter, PVOID* context);
void InitOnceInitialize(PINIT_ONCE initOnce) noexcept;
BOOL InitOnceExecuteOnce(PINIT_ONCE initOnce, PINIT_ONCE_FN initFn, PVOID parameter, LPVOID* context) noexcept;
