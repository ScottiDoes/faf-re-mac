#pragma once

// Win32 on POSIX: Memory. Included only by ../WinCompat.h (see its rules).
// Implementations: ../WinCompatMemory.cpp.
//
// Virtual memory: VirtualAlloc reserves with mmap(PROT_NONE) at 64 KB granularity and
// commits with mprotect; decommitted pages are replaced so they read as zero when committed
// again; regions are tracked per host page (16 KB on Apple Silicon: a sub-page decommit
// zeroes that part and leaves the page committed). VirtualQuery reports the layer's own
// regions exactly and everything else from the Mach VM map (image, mapped or private;
// committed, reserved or free). Heaps are malloc zones (the process heap is the default
// zone); HeapSize is the block's usable size.

#define MEM_COMMIT 0x00001000
#define MEM_RESERVE 0x00002000
#define MEM_DECOMMIT 0x00004000
#define MEM_RELEASE 0x00008000
#define MEM_FREE 0x00010000
#define MEM_PRIVATE 0x00020000
#define MEM_MAPPED 0x00040000
#define MEM_RESET 0x00080000
#define MEM_TOP_DOWN 0x00100000
#define MEM_WRITE_WATCH 0x00200000
#define MEM_PHYSICAL 0x00400000
#define MEM_IMAGE 0x01000000
#define MEM_RESET_UNDO 0x01000000
#define MEM_LARGE_PAGES 0x20000000

typedef struct _MEMORY_BASIC_INFORMATION {
  PVOID BaseAddress;
  PVOID AllocationBase;
  DWORD AllocationProtect;
  WORD PartitionId;
  SIZE_T RegionSize;
  DWORD State;
  DWORD Protect;
  DWORD Type;
} MEMORY_BASIC_INFORMATION, *PMEMORY_BASIC_INFORMATION;

LPVOID VirtualAlloc(LPVOID address, SIZE_T size, DWORD allocationType, DWORD protect) noexcept;
BOOL VirtualFree(LPVOID address, SIZE_T size, DWORD freeType) noexcept;
BOOL VirtualProtect(LPVOID address, SIZE_T size, DWORD newProtect, PDWORD oldProtect) noexcept;
SIZE_T VirtualQuery(LPCVOID address, PMEMORY_BASIC_INFORMATION buffer, SIZE_T length) noexcept;
BOOL VirtualLock(LPVOID address, SIZE_T size) noexcept;
BOOL VirtualUnlock(LPVOID address, SIZE_T size) noexcept;
BOOL IsBadReadPtr(const void* address, UINT_PTR size) noexcept;
BOOL IsBadWritePtr(LPVOID address, UINT_PTR size) noexcept;
BOOL IsBadCodePtr(FARPROC address) noexcept;
// Real: sys_icache_invalidate on the range (the process handle must be the own process).
BOOL FlushInstructionCache(HANDLE process, LPCVOID baseAddress, SIZE_T size) noexcept;

// --- Heaps -----------------------------------------------------------------------------------------
#define HEAP_NO_SERIALIZE 0x00000001
#define HEAP_GROWABLE 0x00000002
#define HEAP_GENERATE_EXCEPTIONS 0x00000004
#define HEAP_ZERO_MEMORY 0x00000008
#define HEAP_REALLOC_IN_PLACE_ONLY 0x00000010
#define HEAP_CREATE_ENABLE_EXECUTE 0x00040000
HANDLE GetProcessHeap() noexcept;
HANDLE HeapCreate(DWORD options, SIZE_T initialSize, SIZE_T maximumSize) noexcept;
BOOL HeapDestroy(HANDLE heap) noexcept;
LPVOID HeapAlloc(HANDLE heap, DWORD flags, SIZE_T bytes) noexcept;
LPVOID HeapReAlloc(HANDLE heap, DWORD flags, LPVOID memory, SIZE_T bytes) noexcept;
BOOL HeapFree(HANDLE heap, DWORD flags, LPVOID memory) noexcept;
SIZE_T HeapSize(HANDLE heap, DWORD flags, LPCVOID memory) noexcept;
BOOL HeapValidate(HANDLE heap, DWORD flags, LPCVOID memory) noexcept;
SIZE_T HeapCompact(HANDLE heap, DWORD flags) noexcept;

// Global/Local memory: fixed blocks (a moveable block's handle is its address, so
// GlobalLock returns the handle itself).
#define GMEM_FIXED 0x0000
#define GMEM_MOVEABLE 0x0002
#define GMEM_ZEROINIT 0x0040
#define GMEM_DDESHARE 0x2000
#define GPTR (GMEM_FIXED | GMEM_ZEROINIT)
#define GHND (GMEM_MOVEABLE | GMEM_ZEROINIT)
#define LMEM_FIXED 0x0000
#define LMEM_MOVEABLE 0x0002
#define LMEM_ZEROINIT 0x0040
#define LPTR (LMEM_FIXED | LMEM_ZEROINIT)
#define LHND (LMEM_MOVEABLE | LMEM_ZEROINIT)
HGLOBAL GlobalAlloc(UINT flags, SIZE_T bytes) noexcept;
HGLOBAL GlobalReAlloc(HGLOBAL memory, SIZE_T bytes, UINT flags) noexcept;
HGLOBAL GlobalFree(HGLOBAL memory) noexcept;
LPVOID GlobalLock(HGLOBAL memory) noexcept;
BOOL GlobalUnlock(HGLOBAL memory) noexcept;
SIZE_T GlobalSize(HGLOBAL memory) noexcept;
HLOCAL LocalAlloc(UINT flags, SIZE_T bytes) noexcept;
HLOCAL LocalReAlloc(HLOCAL memory, SIZE_T bytes, UINT flags) noexcept;
HLOCAL LocalFree(HLOCAL memory) noexcept;
LPVOID LocalLock(HLOCAL memory) noexcept;
BOOL LocalUnlock(HLOCAL memory) noexcept;
SIZE_T LocalSize(HLOCAL memory) noexcept;

// --- Memory status ---------------------------------------------------------------------------------
typedef struct _MEMORYSTATUSEX {
  DWORD dwLength;
  DWORD dwMemoryLoad;
  DWORDLONG ullTotalPhys;
  DWORDLONG ullAvailPhys;
  DWORDLONG ullTotalPageFile;
  DWORDLONG ullAvailPageFile;
  DWORDLONG ullTotalVirtual;
  DWORDLONG ullAvailVirtual;
  DWORDLONG ullAvailExtendedVirtual;
} MEMORYSTATUSEX, *LPMEMORYSTATUSEX;
typedef struct _MEMORYSTATUS {
  DWORD dwLength;
  DWORD dwMemoryLoad;
  SIZE_T dwTotalPhys;
  SIZE_T dwAvailPhys;
  SIZE_T dwTotalPageFile;
  SIZE_T dwAvailPageFile;
  SIZE_T dwTotalVirtual;
  SIZE_T dwAvailVirtual;
} MEMORYSTATUS, *LPMEMORYSTATUS;
BOOL GlobalMemoryStatusEx(LPMEMORYSTATUSEX status) noexcept;
void GlobalMemoryStatus(LPMEMORYSTATUS status) noexcept;

// <psapi.h>
typedef struct _PROCESS_MEMORY_COUNTERS {
  DWORD cb;
  DWORD PageFaultCount;
  SIZE_T PeakWorkingSetSize;
  SIZE_T WorkingSetSize;
  SIZE_T QuotaPeakPagedPoolUsage;
  SIZE_T QuotaPagedPoolUsage;
  SIZE_T QuotaPeakNonPagedPoolUsage;
  SIZE_T QuotaNonPagedPoolUsage;
  SIZE_T PagefileUsage;
  SIZE_T PeakPagefileUsage;
} PROCESS_MEMORY_COUNTERS, *PPROCESS_MEMORY_COUNTERS;
typedef struct _PROCESS_MEMORY_COUNTERS_EX {
  DWORD cb;
  DWORD PageFaultCount;
  SIZE_T PeakWorkingSetSize;
  SIZE_T WorkingSetSize;
  SIZE_T QuotaPeakPagedPoolUsage;
  SIZE_T QuotaPagedPoolUsage;
  SIZE_T QuotaPeakNonPagedPoolUsage;
  SIZE_T QuotaNonPagedPoolUsage;
  SIZE_T PagefileUsage;
  SIZE_T PeakPagefileUsage;
  SIZE_T PrivateUsage;
} PROCESS_MEMORY_COUNTERS_EX, *PPROCESS_MEMORY_COUNTERS_EX;
BOOL GetProcessMemoryInfo(HANDLE process, PPROCESS_MEMORY_COUNTERS counters, DWORD size) noexcept;
BOOL K32GetProcessMemoryInfo(HANDLE process, PPROCESS_MEMORY_COUNTERS counters, DWORD size) noexcept;

// --- CRT memory ----------------------------------------------------------------------------------
errno_t memcpy_s(void* destination, std::size_t destinationSize, const void* source, std::size_t count) noexcept;
errno_t memmove_s(void* destination, std::size_t destinationSize, const void* source, std::size_t count) noexcept;
errno_t wmemcpy_s(wchar_t* destination, std::size_t destinationSize, const wchar_t* source,
                  std::size_t count) noexcept;
errno_t wmemmove_s(wchar_t* destination, std::size_t destinationSize, const wchar_t* source,
                   std::size_t count) noexcept;
void* _aligned_malloc(std::size_t size, std::size_t alignment) noexcept;
void* _aligned_realloc(void* memory, std::size_t size, std::size_t alignment) noexcept;
void _aligned_free(void* memory) noexcept;
#define _alloca __builtin_alloca
