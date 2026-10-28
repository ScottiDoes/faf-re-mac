#pragma once

// Win32 on POSIX: Threads. Included only by ../WinCompat.h (see its rules).
// Implementations: ../WinCompatThreads.cpp.
//
// Threads are pthreads. Every thread (started here or not) gets a Win32 thread id (a
// process-local, never reused, nonzero multiple of 4) and, on first use, a kernel object:
// thread handles are waitable (signalled on exit), OpenThread finds live threads by id,
// and QueueUserAPC delivers to the target's next alertable wait (SleepEx, *WaitEx,
// MsgWaitForMultipleObjectsEx with MWMO_ALERTABLE). Scheduling hints (priority, affinity,
// ideal processor) are recorded and reported back but not enforced: macOS has no CPU
// affinity on Apple Silicon.

typedef DWORD(WINAPI* PTHREAD_START_ROUTINE)(LPVOID parameter);
typedef PTHREAD_START_ROUTINE LPTHREAD_START_ROUTINE;
typedef void(CALLBACK* PAPCFUNC)(ULONG_PTR parameter);
typedef DWORD_PTR* PDWORD_PTR;

#ifndef MAXLONG
#define MAXLONG 0x7fffffff
#endif
#ifndef MAXDWORD
#define MAXDWORD 0xffffffff
#endif

#define CREATE_SUSPENDED 0x00000004
#define STACK_SIZE_PARAM_IS_A_RESERVATION 0x00010000
#define STILL_ACTIVE STATUS_PENDING

#define THREAD_TERMINATE 0x0001
#define THREAD_SUSPEND_RESUME 0x0002
#define THREAD_GET_CONTEXT 0x0008
#define THREAD_SET_CONTEXT 0x0010
#define THREAD_SET_INFORMATION 0x0020
#define THREAD_QUERY_INFORMATION 0x0040
#define THREAD_SET_THREAD_TOKEN 0x0080
#define THREAD_IMPERSONATE 0x0100
#define THREAD_DIRECT_IMPERSONATION 0x0200
#define THREAD_SET_LIMITED_INFORMATION 0x0400
#define THREAD_QUERY_LIMITED_INFORMATION 0x0800
#define THREAD_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0xFFFF)
#define PROCESS_TERMINATE 0x0001
#define PROCESS_VM_READ 0x0010
#define PROCESS_VM_WRITE 0x0020
#define PROCESS_DUP_HANDLE 0x0040
#define PROCESS_SET_INFORMATION 0x0200
#define PROCESS_QUERY_INFORMATION 0x0400
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#define PROCESS_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0xFFFF)

#define THREAD_BASE_PRIORITY_LOWRT 15
#define THREAD_BASE_PRIORITY_MAX 2
#define THREAD_BASE_PRIORITY_MIN (-2)
#define THREAD_BASE_PRIORITY_IDLE (-15)
#define THREAD_PRIORITY_LOWEST THREAD_BASE_PRIORITY_MIN
#define THREAD_PRIORITY_BELOW_NORMAL (THREAD_PRIORITY_LOWEST + 1)
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_HIGHEST THREAD_BASE_PRIORITY_MAX
#define THREAD_PRIORITY_ABOVE_NORMAL (THREAD_PRIORITY_HIGHEST - 1)
#define THREAD_PRIORITY_ERROR_RETURN (MAXLONG)
#define THREAD_PRIORITY_TIME_CRITICAL THREAD_BASE_PRIORITY_LOWRT
#define THREAD_PRIORITY_IDLE THREAD_BASE_PRIORITY_IDLE
#define THREAD_MODE_BACKGROUND_BEGIN 0x00010000
#define THREAD_MODE_BACKGROUND_END 0x00020000

#define NORMAL_PRIORITY_CLASS 0x00000020
#define IDLE_PRIORITY_CLASS 0x00000040
#define HIGH_PRIORITY_CLASS 0x00000080
#define REALTIME_PRIORITY_CLASS 0x00000100
#define BELOW_NORMAL_PRIORITY_CLASS 0x00004000
#define ABOVE_NORMAL_PRIORITY_CLASS 0x00008000

// --- Threads ---------------------------------------------------------------------------------
// Stacks: at least the requested size and at least 2 MB (twice Windows' 1 MB default
// reserve, for 64-bit frames).
HANDLE CreateThread(LPSECURITY_ATTRIBUTES attributes, SIZE_T stackSize, LPTHREAD_START_ROUTINE start,
                    LPVOID parameter, DWORD creationFlags, LPDWORD threadId) noexcept;
HANDLE OpenThread(DWORD access, BOOL inherit, DWORD threadId) noexcept;
HANDLE GetCurrentThread() noexcept;
DWORD GetCurrentThreadId() noexcept;
DWORD GetThreadId(HANDLE thread) noexcept;
[[noreturn]] void ExitThread(DWORD exitCode) noexcept;
BOOL TerminateThread(HANDLE thread, DWORD exitCode) noexcept;
BOOL GetExitCodeThread(HANDLE thread, LPDWORD exitCode) noexcept;
// Suspension uses Mach thread_suspend/thread_resume (and the start gate for
// CREATE_SUSPENDED); returns the previous suspend count, or (DWORD)-1.
DWORD SuspendThread(HANDLE thread) noexcept;
DWORD ResumeThread(HANDLE thread) noexcept;
int GetThreadPriority(HANDLE thread) noexcept;
BOOL SetThreadPriority(HANDLE thread, int priority) noexcept;
BOOL SetThreadPriorityBoost(HANDLE thread, BOOL disable) noexcept;
HRESULT SetThreadDescription(HANDLE thread, PCWSTR description) noexcept;

typedef ULONG_PTR KAFFINITY;
typedef struct _PROCESSOR_NUMBER {
  WORD Group;
  BYTE Number;
  BYTE Reserved;
} PROCESSOR_NUMBER, *PPROCESSOR_NUMBER;
typedef struct _GROUP_AFFINITY {
  KAFFINITY Mask;
  WORD Group;
  WORD Reserved[3];
} GROUP_AFFINITY, *PGROUP_AFFINITY;

DWORD_PTR SetThreadAffinityMask(HANDLE thread, DWORD_PTR mask) noexcept;
DWORD SetThreadIdealProcessor(HANDLE thread, DWORD idealProcessor) noexcept;
BOOL SetThreadIdealProcessorEx(HANDLE thread, PPROCESSOR_NUMBER idealProcessor,
                               PPROCESSOR_NUMBER previousIdealProcessor) noexcept;
BOOL GetThreadIdealProcessorEx(HANDLE thread, PPROCESSOR_NUMBER idealProcessor) noexcept;
BOOL SetThreadGroupAffinity(HANDLE thread, const GROUP_AFFINITY* affinity, PGROUP_AFFINITY previousAffinity) noexcept;
BOOL GetThreadGroupAffinity(HANDLE thread, PGROUP_AFFINITY affinity) noexcept;
DWORD GetCurrentProcessorNumber() noexcept;
void GetCurrentProcessorNumberEx(PPROCESSOR_NUMBER number) noexcept;

DWORD QueueUserAPC(PAPCFUNC routine, HANDLE thread, ULONG_PTR data) noexcept;
void Sleep(DWORD milliseconds) noexcept;
DWORD SleepEx(DWORD milliseconds, BOOL alertable) noexcept;
BOOL SwitchToThread() noexcept;

// Thread local storage (pthread keys).
#define TLS_OUT_OF_INDEXES ((DWORD)0xFFFFFFFF)
DWORD TlsAlloc() noexcept;
BOOL TlsFree(DWORD index) noexcept;
LPVOID TlsGetValue(DWORD index) noexcept;
BOOL TlsSetValue(DWORD index, LPVOID value) noexcept;

// CRT thread entry points (<process.h>).
std::uintptr_t _beginthreadex(void* security, unsigned stackSize, unsigned(__stdcall* start)(void*), void* argument,
                              unsigned initFlag, unsigned* threadId) noexcept;
std::uintptr_t _beginthread(void(__cdecl* start)(void*), unsigned stackSize, void* argument) noexcept;
[[noreturn]] void _endthreadex(unsigned exitCode) noexcept;
[[noreturn]] void _endthread() noexcept;

// --- Process ---------------------------------------------------------------------------------
HANDLE GetCurrentProcess() noexcept;
DWORD GetCurrentProcessId() noexcept;
DWORD GetProcessId(HANDLE process) noexcept;
// The process mask covers every logical CPU (bit i = CPU i, up to 64).
BOOL GetProcessAffinityMask(HANDLE process, PDWORD_PTR processMask, PDWORD_PTR systemMask) noexcept;
BOOL SetProcessAffinityMask(HANDLE process, DWORD_PTR mask) noexcept;
DWORD GetPriorityClass(HANDLE process) noexcept;
BOOL SetPriorityClass(HANDLE process, DWORD priorityClass) noexcept;

// --- Processor topology ------------------------------------------------------------------------
// From sysctl: one core record per physical core (Apple Silicon has no SMT), performance
// cores first (EfficiencyClass 1) then efficiency cores (EfficiencyClass 0), one NUMA node,
// one package, one group; caches per performance level.
#define ALL_PROCESSOR_GROUPS 0xffff
#define MAXIMUM_PROCESSORS 64
#define LTP_PC_SMT 0x1
#ifndef ANYSIZE_ARRAY
#define ANYSIZE_ARRAY 1
#endif
typedef enum _LOGICAL_PROCESSOR_RELATIONSHIP {
  RelationProcessorCore,
  RelationNumaNode,
  RelationCache,
  RelationProcessorPackage,
  RelationGroup,
  RelationProcessorDie,
  RelationNumaNodeEx,
  RelationProcessorModule,
  RelationAll = 0xffff
} LOGICAL_PROCESSOR_RELATIONSHIP;
typedef enum _PROCESSOR_CACHE_TYPE { CacheUnified, CacheInstruction, CacheData, CacheTrace } PROCESSOR_CACHE_TYPE;
typedef struct _PROCESSOR_RELATIONSHIP {
  BYTE Flags;
  BYTE EfficiencyClass;
  BYTE Reserved[20];
  WORD GroupCount;
  GROUP_AFFINITY GroupMask[ANYSIZE_ARRAY];
} PROCESSOR_RELATIONSHIP, *PPROCESSOR_RELATIONSHIP;
typedef struct _NUMA_NODE_RELATIONSHIP {
  DWORD NodeNumber;
  BYTE Reserved[18];
  WORD GroupCount;
  union {
    GROUP_AFFINITY GroupMask;
    GROUP_AFFINITY GroupMasks[ANYSIZE_ARRAY];
  };
} NUMA_NODE_RELATIONSHIP, *PNUMA_NODE_RELATIONSHIP;
typedef struct _CACHE_RELATIONSHIP {
  BYTE Level;
  BYTE Associativity;
  WORD LineSize;
  DWORD CacheSize;
  PROCESSOR_CACHE_TYPE Type;
  BYTE Reserved[18];
  WORD GroupCount;
  union {
    GROUP_AFFINITY GroupMask;
    GROUP_AFFINITY GroupMasks[ANYSIZE_ARRAY];
  };
} CACHE_RELATIONSHIP, *PCACHE_RELATIONSHIP;
typedef struct _PROCESSOR_GROUP_INFO {
  BYTE MaximumProcessorCount;
  BYTE ActiveProcessorCount;
  BYTE Reserved[38];
  KAFFINITY ActiveProcessorMask;
} PROCESSOR_GROUP_INFO, *PPROCESSOR_GROUP_INFO;
typedef struct _GROUP_RELATIONSHIP {
  WORD MaximumGroupCount;
  WORD ActiveGroupCount;
  BYTE Reserved[20];
  PROCESSOR_GROUP_INFO GroupInfo[ANYSIZE_ARRAY];
} GROUP_RELATIONSHIP, *PGROUP_RELATIONSHIP;
typedef struct _SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX {
  LOGICAL_PROCESSOR_RELATIONSHIP Relationship;
  DWORD Size;
  union {
    PROCESSOR_RELATIONSHIP Processor;
    NUMA_NODE_RELATIONSHIP NumaNode;
    CACHE_RELATIONSHIP Cache;
    GROUP_RELATIONSHIP Group;
  };
} SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, *PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX;
BOOL GetLogicalProcessorInformationEx(LOGICAL_PROCESSOR_RELATIONSHIP relationship,
                                      PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX buffer, PDWORD returnedLength) noexcept;
DWORD GetActiveProcessorCount(WORD groupNumber) noexcept;
DWORD GetMaximumProcessorCount(WORD groupNumber) noexcept;
WORD GetActiveProcessorGroupCount() noexcept;
