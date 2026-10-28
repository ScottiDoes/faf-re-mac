// Win32 on POSIX: Memory (declarations in compat/Memory.h).

#include "WinCompatInternal.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <vector>

#include <dlfcn.h>
#include <libkern/OSCacheControl.h>
#include <libproc.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <malloc/malloc.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <unistd.h>

using namespace faf_posix;

namespace
{
  constexpr std::uintptr_t kAllocationGranularity = 0x10000u;

  std::uintptr_t HostPageSize() noexcept
  {
    static const std::uintptr_t page = static_cast<std::uintptr_t>(getpagesize());
    return page;
  }

  std::uintptr_t RoundDown(const std::uintptr_t value, const std::uintptr_t unit) noexcept
  {
    return value & ~(unit - 1);
  }

  std::uintptr_t RoundUp(const std::uintptr_t value, const std::uintptr_t unit) noexcept
  {
    return (value + unit - 1) & ~(unit - 1);
  }

  int PosixProtection(const DWORD protect) noexcept
  {
    switch (protect & 0xFF) {
      case PAGE_NOACCESS:
        return PROT_NONE;
      case PAGE_READONLY:
        return PROT_READ;
      case PAGE_READWRITE:
      case PAGE_WRITECOPY:
        return PROT_READ | PROT_WRITE;
      case PAGE_EXECUTE:
        return PROT_EXEC;
      case PAGE_EXECUTE_READ:
        return PROT_READ | PROT_EXEC;
      case PAGE_EXECUTE_READWRITE:
      case PAGE_EXECUTE_WRITECOPY:
        return PROT_READ | PROT_WRITE | PROT_EXEC;
      default:
        return -1;
    }
  }

  DWORD Win32Protection(const vm_prot_t protection) noexcept
  {
    const bool read = (protection & VM_PROT_READ) != 0;
    const bool write = (protection & VM_PROT_WRITE) != 0;
    const bool execute = (protection & VM_PROT_EXECUTE) != 0;
    if (execute) {
      return write ? PAGE_EXECUTE_READWRITE : (read ? PAGE_EXECUTE_READ : PAGE_EXECUTE);
    }
    if (write) {
      return PAGE_READWRITE;
    }
    return read ? PAGE_READONLY : PAGE_NOACCESS;
  }

  // --- The layer's own VirtualAlloc regions -----------------------------------------------------
  // The region bookkeeping lives on the system heap: the engine allocator reserves and
  // commits through VirtualAlloc (see SystemAllocator in WinCompatInternal.h).
  struct Region
  {
    std::uintptr_t base = 0;
    std::size_t size = 0;
    DWORD allocationProtect = 0;
    // Per host page: 0 = reserved, else the PAGE_* protection it is committed with.
    std::vector<std::uint16_t, SystemAllocator<std::uint16_t>> pages;
  };
  using RegionMap = std::map<std::uintptr_t, Region, std::less<std::uintptr_t>,
                             SystemAllocator<std::pair<const std::uintptr_t, Region>>>;

  pthread_mutex_t gRegionLock = PTHREAD_MUTEX_INITIALIZER;
  RegionMap& Regions()
  {
    static RegionMap* const regions = [] {
      void* const memory = std::malloc(sizeof(RegionMap));
      if (memory == nullptr) {
        std::abort();
      }
      return new (memory) RegionMap();
    }();
    return *regions;
  }

  // The region containing `address` (gRegionLock held), or nullptr.
  Region* FindRegion(const std::uintptr_t address)
  {
    auto& regions = Regions();
    auto it = regions.upper_bound(address);
    if (it == regions.begin()) {
      return nullptr;
    }
    --it;
    Region& region = it->second;
    return address < region.base + region.size ? &region : nullptr;
  }

  void* ReserveRange(const std::uintptr_t hint, const std::size_t size) noexcept
  {
    if (hint != 0) {
      void* const mapped = ::mmap(reinterpret_cast<void*>(hint), size, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
      if (mapped == MAP_FAILED) {
        return nullptr;
      }
      if (reinterpret_cast<std::uintptr_t>(mapped) != hint) {
        ::munmap(mapped, size);
        return nullptr;
      }
      return mapped;
    }
    // Over-reserve, then trim to the 64 KB allocation granularity.
    const std::size_t padded = size + kAllocationGranularity;
    void* const mapped = ::mmap(nullptr, padded, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (mapped == MAP_FAILED) {
      return nullptr;
    }
    const auto start = reinterpret_cast<std::uintptr_t>(mapped);
    const std::uintptr_t aligned = RoundUp(start, kAllocationGranularity);
    if (aligned > start) {
      ::munmap(mapped, aligned - start);
    }
    const std::uintptr_t end = start + padded;
    if (end > aligned + size) {
      ::munmap(reinterpret_cast<void*>(aligned + size), end - (aligned + size));
    }
    return reinterpret_cast<void*>(aligned);
  }

  bool CommitPages(Region& region, const std::uintptr_t start, const std::uintptr_t end, const DWORD protect) noexcept
  {
    const int prot = PosixProtection(protect);
    if (::mprotect(reinterpret_cast<void*>(start), end - start, prot) != 0) {
      return false;
    }
    const std::uintptr_t page = HostPageSize();
    for (std::uintptr_t p = start; p < end; p += page) {
      region.pages[(p - region.base) / page] = static_cast<std::uint16_t>(protect & 0x7FF);
    }
    return true;
  }

  // Decommit: whole host pages are replaced by fresh reserved ones; the covered part of a
  // partially covered page is zeroed (it stays committed, as other data may share it).
  void DecommitRange(Region& region, const std::uintptr_t start, const std::uintptr_t end) noexcept
  {
    const std::uintptr_t page = HostPageSize();
    for (std::uintptr_t p = RoundDown(start, page); p < end; p += page) {
      const std::uintptr_t lo = std::max(p, start);
      const std::uintptr_t hi = std::min(p + page, end);
      std::uint16_t& state = region.pages[(p - region.base) / page];
      if (lo == p && hi == p + page) {
        (void)::mmap(reinterpret_cast<void*>(p), page, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
        state = 0;
      } else if (state != 0 && (PosixProtection(state) & PROT_WRITE) != 0) {
        std::memset(reinterpret_cast<void*>(lo), 0, hi - lo);
      }
    }
  }

  // VirtualQuery outside the layer's regions: the Mach VM map.
  SIZE_T QueryMachRegion(const std::uintptr_t page, PMEMORY_BASIC_INFORMATION const info) noexcept
  {
    mach_vm_address_t address = page;
    mach_vm_size_t size = 0;
    vm_region_basic_info_data_64_t basic{};
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object = MACH_PORT_NULL;
    const kern_return_t kr = mach_vm_region(mach_task_self(), &address, &size, VM_REGION_BASIC_INFO_64,
                                            reinterpret_cast<vm_region_info_t>(&basic), &count, &object);
    std::memset(info, 0, sizeof(*info));
    info->BaseAddress = reinterpret_cast<PVOID>(page);
    constexpr std::uintptr_t kUserLimit = 0x0000800000000000ull;
    if (kr != KERN_SUCCESS || address > page) {
      // Free up to the next region (or the end of the user address space).
      const std::uintptr_t next = kr == KERN_SUCCESS ? static_cast<std::uintptr_t>(address) : kUserLimit;
      if (page >= next) {
        ::SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
      }
      info->RegionSize = next - page;
      info->State = MEM_FREE;
      info->Protect = PAGE_NOACCESS;
      return sizeof(*info);
    }
    const std::uintptr_t regionEnd = static_cast<std::uintptr_t>(address + size);
    info->AllocationBase = reinterpret_cast<PVOID>(static_cast<std::uintptr_t>(address));
    info->RegionSize = regionEnd - page;
    if (basic.protection == VM_PROT_NONE) {
      info->State = MEM_RESERVE;
      info->AllocationProtect = PAGE_NOACCESS;
      info->Protect = 0;
    } else {
      info->State = MEM_COMMIT;
      info->Protect = Win32Protection(basic.protection);
      info->AllocationProtect = Win32Protection(basic.max_protection & (VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE));
    }
    Dl_info image{};
    char path[8];
    if (dladdr(reinterpret_cast<const void*>(page), &image) != 0 && image.dli_fbase != nullptr) {
      info->Type = MEM_IMAGE;
    } else if (proc_regionfilename(getpid(), page, path, sizeof(path)) > 0) {
      info->Type = MEM_MAPPED;
    } else {
      info->Type = MEM_PRIVATE;
    }
    return sizeof(*info);
  }

  bool RangeHasAccess(const std::uintptr_t start, const UINT_PTR size, const DWORD wanted) noexcept
  {
    if (size == 0) {
      return true;
    }
    if (start == 0 || start + size < start) {
      return false;
    }
    std::uintptr_t cursor = start;
    const std::uintptr_t end = start + size;
    while (cursor < end) {
      MEMORY_BASIC_INFORMATION info{};
      if (VirtualQuery(reinterpret_cast<LPCVOID>(cursor), &info, sizeof(info)) == 0 || info.State != MEM_COMMIT ||
          (info.Protect & PAGE_GUARD) != 0) {
        return false;
      }
      const DWORD protect = info.Protect & 0xFF;
      const bool readable = protect == PAGE_READONLY || protect == PAGE_READWRITE || protect == PAGE_WRITECOPY ||
                            protect == PAGE_EXECUTE_READ || protect == PAGE_EXECUTE_READWRITE ||
                            protect == PAGE_EXECUTE_WRITECOPY;
      const bool writable = protect == PAGE_READWRITE || protect == PAGE_WRITECOPY ||
                            protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY;
      if ((wanted == PAGE_READONLY && !readable) || (wanted == PAGE_READWRITE && !writable)) {
        return false;
      }
      cursor = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
    }
    return true;
  }

  // --- Heaps -------------------------------------------------------------------------------------
  malloc_zone_t* ZoneOf(const HANDLE heap) noexcept
  {
    return static_cast<malloc_zone_t*>(heap);
  }

  bool IsProcessHeap(const HANDLE heap) noexcept
  {
    return heap == GetProcessHeap();
  }

  // --- Global/Local blocks ------------------------------------------------------------------------
  struct alignas(16) BlockHeader
  {
    std::size_t size;
    std::uint32_t magic;
    std::uint32_t flags;
  };
  constexpr std::uint32_t kBlockMagic = 0x464D454Du; // 'FMEM'

  BlockHeader* HeaderOf(void* const memory) noexcept
  {
    if (memory == nullptr) {
      return nullptr;
    }
    auto* const header = static_cast<BlockHeader*>(memory) - 1;
    return header->magic == kBlockMagic ? header : nullptr;
  }

  void* BlockAlloc(const UINT flags, const SIZE_T bytes) noexcept
  {
    auto* const header = static_cast<BlockHeader*>((flags & GMEM_ZEROINIT) != 0
                                                     ? std::calloc(1, sizeof(BlockHeader) + bytes)
                                                     : std::malloc(sizeof(BlockHeader) + bytes));
    if (header == nullptr) {
      ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return nullptr;
    }
    header->size = bytes;
    header->magic = kBlockMagic;
    header->flags = flags;
    return header + 1;
  }

  void* BlockReAlloc(void* const memory, const SIZE_T bytes, const UINT flags) noexcept
  {
    BlockHeader* const header = HeaderOf(memory);
    if (header == nullptr) {
      ::SetLastError(ERROR_INVALID_HANDLE);
      return nullptr;
    }
    const std::size_t old = header->size;
    auto* const grown = static_cast<BlockHeader*>(std::realloc(header, sizeof(BlockHeader) + bytes));
    if (grown == nullptr) {
      ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return nullptr;
    }
    if ((flags & GMEM_ZEROINIT) != 0 && bytes > old) {
      std::memset(reinterpret_cast<unsigned char*>(grown + 1) + old, 0, bytes - old);
    }
    grown->size = bytes;
    return grown + 1;
  }

  void* BlockFree(void* const memory) noexcept
  {
    if (memory == nullptr) {
      return nullptr;
    }
    BlockHeader* const header = HeaderOf(memory);
    if (header == nullptr) {
      ::SetLastError(ERROR_INVALID_HANDLE);
      return memory;
    }
    header->magic = 0;
    std::free(header);
    return nullptr;
  }

  errno_t CheckedCopy(void* const destination, const std::size_t destinationSize, const void* const source,
                      const std::size_t count, const bool overlap) noexcept
  {
    if (count == 0) {
      return 0;
    }
    if (destination == nullptr) {
      _invalid_parameter_noinfo();
      return EINVAL;
    }
    if (source == nullptr || destinationSize < count) {
      if (!overlap || source == nullptr) {
        std::memset(destination, 0, destinationSize);
      }
      _invalid_parameter_noinfo();
      return source == nullptr ? EINVAL : ERANGE;
    }
    if (overlap) {
      std::memmove(destination, source, count);
    } else {
      std::memcpy(destination, source, count);
    }
    return 0;
  }
} // namespace

// --- Virtual memory -------------------------------------------------------------------------------

LPVOID VirtualAlloc(LPVOID const address, const SIZE_T size, const DWORD allocationType, const DWORD protect) noexcept
{
  if (size == 0 || PosixProtection(protect) < 0) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return nullptr;
  }
  const std::uintptr_t page = HostPageSize();
  const auto requested = reinterpret_cast<std::uintptr_t>(address);

  if ((allocationType & MEM_RESET) != 0) {
    const std::uintptr_t start = RoundUp(requested, page);
    const std::uintptr_t end = RoundDown(requested + size, page);
    if (end > start) {
      (void)::madvise(reinterpret_cast<void*>(start), end - start, MADV_FREE);
    }
    return address;
  }
  if ((allocationType & (MEM_RESERVE | MEM_COMMIT)) == 0) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return nullptr;
  }

  pthread_mutex_lock(&gRegionLock);
  try {
    // MEM_COMMIT of a range nobody reserved reserves it too, as Windows does.
    const bool reserve = (allocationType & MEM_RESERVE) != 0 || requested == 0 || FindRegion(requested) == nullptr;
    if (reserve) {
      const std::uintptr_t base = RoundDown(requested, kAllocationGranularity);
      const std::size_t length = RoundUp(requested + size, page) - (requested != 0 ? base : 0);
      const std::size_t total = requested != 0 ? length : RoundUp(size, page);
      void* const mapped = ReserveRange(base, total);
      if (mapped == nullptr) {
        pthread_mutex_unlock(&gRegionLock);
        ::SetLastError(requested != 0 ? ERROR_INVALID_ADDRESS : ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
      }
      Region region;
      region.base = reinterpret_cast<std::uintptr_t>(mapped);
      region.size = total;
      region.allocationProtect = protect;
      region.pages.assign(total / page, 0);
      Region& stored = Regions()[region.base] = std::move(region);
      if ((allocationType & MEM_COMMIT) != 0 && !CommitPages(stored, stored.base, stored.base + stored.size, protect)) {
        ::munmap(mapped, total);
        Regions().erase(stored.base);
        pthread_mutex_unlock(&gRegionLock);
        ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
      }
      pthread_mutex_unlock(&gRegionLock);
      return mapped;
    }

    // MEM_COMMIT inside an existing reservation.
    Region* const region = FindRegion(requested);
    const std::uintptr_t start = RoundDown(requested, page);
    const std::uintptr_t end = RoundUp(requested + size, page);
    if (region == nullptr || end > region->base + region->size) {
      pthread_mutex_unlock(&gRegionLock);
      ::SetLastError(ERROR_INVALID_ADDRESS);
      return nullptr;
    }
    if (!CommitPages(*region, start, end, protect)) {
      pthread_mutex_unlock(&gRegionLock);
      ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return nullptr;
    }
    pthread_mutex_unlock(&gRegionLock);
    return reinterpret_cast<LPVOID>(start);
  } catch (...) {
    pthread_mutex_unlock(&gRegionLock);
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return nullptr;
  }
}

BOOL VirtualFree(LPVOID const address, const SIZE_T size, const DWORD freeType) noexcept
{
  const auto requested = reinterpret_cast<std::uintptr_t>(address);
  pthread_mutex_lock(&gRegionLock);
  Region* const region = FindRegion(requested);
  if (region == nullptr) {
    pthread_mutex_unlock(&gRegionLock);
    ::SetLastError(ERROR_INVALID_ADDRESS);
    return FALSE;
  }
  if (freeType == MEM_RELEASE) {
    if (size != 0 || requested != region->base) {
      pthread_mutex_unlock(&gRegionLock);
      ::SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
    }
    ::munmap(reinterpret_cast<void*>(region->base), region->size);
    Regions().erase(region->base);
    pthread_mutex_unlock(&gRegionLock);
    return TRUE;
  }
  if (freeType == MEM_DECOMMIT) {
    const std::uintptr_t end = size == 0 ? region->base + region->size : requested + size;
    if (end > region->base + region->size) {
      pthread_mutex_unlock(&gRegionLock);
      ::SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
    }
    DecommitRange(*region, size == 0 ? region->base : requested, end);
    pthread_mutex_unlock(&gRegionLock);
    return TRUE;
  }
  pthread_mutex_unlock(&gRegionLock);
  ::SetLastError(ERROR_INVALID_PARAMETER);
  return FALSE;
}

BOOL VirtualProtect(LPVOID const address, const SIZE_T size, const DWORD newProtect, PDWORD const oldProtect) noexcept
{
  const int prot = PosixProtection(newProtect);
  if (prot < 0 || oldProtect == nullptr || size == 0) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  const std::uintptr_t page = HostPageSize();
  const auto requested = reinterpret_cast<std::uintptr_t>(address);
  const std::uintptr_t start = RoundDown(requested, page);
  const std::uintptr_t end = RoundUp(requested + size, page);

  pthread_mutex_lock(&gRegionLock);
  if (Region* const region = FindRegion(requested)) {
    if (end > region->base + region->size) {
      pthread_mutex_unlock(&gRegionLock);
      ::SetLastError(ERROR_INVALID_ADDRESS);
      return FALSE;
    }
    for (std::uintptr_t p = start; p < end; p += page) {
      if (region->pages[(p - region->base) / page] == 0) {
        pthread_mutex_unlock(&gRegionLock);
        ::SetLastError(ERROR_INVALID_ADDRESS);
        return FALSE;
      }
    }
    *oldProtect = region->pages[(start - region->base) / page];
    const bool ok = CommitPages(*region, start, end, newProtect);
    pthread_mutex_unlock(&gRegionLock);
    if (!ok) {
      SetLastErrorFromErrno(errno);
      return FALSE;
    }
    return TRUE;
  }
  pthread_mutex_unlock(&gRegionLock);

  MEMORY_BASIC_INFORMATION info{};
  if (VirtualQuery(address, &info, sizeof(info)) == 0 || info.State != MEM_COMMIT) {
    ::SetLastError(ERROR_INVALID_ADDRESS);
    return FALSE;
  }
  if (::mprotect(reinterpret_cast<void*>(start), end - start, prot) != 0) {
    SetLastErrorFromErrno(errno);
    return FALSE;
  }
  *oldProtect = info.Protect;
  return TRUE;
}

SIZE_T VirtualQuery(LPCVOID const address, PMEMORY_BASIC_INFORMATION const buffer, const SIZE_T length) noexcept
{
  if (buffer == nullptr || length < sizeof(MEMORY_BASIC_INFORMATION)) {
    ::SetLastError(ERROR_BAD_LENGTH);
    return 0;
  }
  const std::uintptr_t pageSize = HostPageSize();
  const std::uintptr_t page = RoundDown(reinterpret_cast<std::uintptr_t>(address), pageSize);
  pthread_mutex_lock(&gRegionLock);
  if (const Region* const region = FindRegion(page)) {
    const std::size_t first = (page - region->base) / pageSize;
    const std::uint16_t state = region->pages[first];
    std::size_t last = first + 1;
    while (last < region->pages.size() && region->pages[last] == state) {
      ++last;
    }
    std::memset(buffer, 0, sizeof(*buffer));
    buffer->BaseAddress = reinterpret_cast<PVOID>(page);
    buffer->AllocationBase = reinterpret_cast<PVOID>(region->base);
    buffer->AllocationProtect = region->allocationProtect;
    buffer->RegionSize = (last - first) * pageSize;
    buffer->State = state != 0 ? MEM_COMMIT : MEM_RESERVE;
    buffer->Protect = state;
    buffer->Type = MEM_PRIVATE;
    pthread_mutex_unlock(&gRegionLock);
    return sizeof(*buffer);
  }
  pthread_mutex_unlock(&gRegionLock);
  return QueryMachRegion(page, buffer);
}

BOOL VirtualLock(LPVOID const address, const SIZE_T size) noexcept
{
  if (::mlock(address, size) != 0) {
    SetLastErrorFromErrno(errno);
    return FALSE;
  }
  return TRUE;
}

BOOL VirtualUnlock(LPVOID const address, const SIZE_T size) noexcept
{
  if (::munlock(address, size) != 0) {
    SetLastErrorFromErrno(errno);
    return FALSE;
  }
  return TRUE;
}

BOOL IsBadReadPtr(const void* const address, const UINT_PTR size) noexcept
{
  return RangeHasAccess(reinterpret_cast<std::uintptr_t>(address), size, PAGE_READONLY) ? FALSE : TRUE;
}

BOOL IsBadWritePtr(LPVOID const address, const UINT_PTR size) noexcept
{
  return RangeHasAccess(reinterpret_cast<std::uintptr_t>(address), size, PAGE_READWRITE) ? FALSE : TRUE;
}

BOOL IsBadCodePtr(FARPROC const address) noexcept
{
  MEMORY_BASIC_INFORMATION info{};
  if (address == nullptr || VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == 0 ||
      info.State != MEM_COMMIT) {
    return TRUE;
  }
  const DWORD protect = info.Protect & 0xFF;
  return (protect == PAGE_EXECUTE || protect == PAGE_EXECUTE_READ || protect == PAGE_EXECUTE_READWRITE ||
          protect == PAGE_EXECUTE_WRITECOPY)
           ? FALSE
           : TRUE;
}

BOOL FlushInstructionCache(const HANDLE process, LPCVOID const baseAddress, const SIZE_T size) noexcept
{
  if (process != CurrentProcessPseudoHandle()) {
    ::SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (baseAddress != nullptr && size != 0) {
    sys_icache_invalidate(const_cast<void*>(baseAddress), size);
  }
  return TRUE;
}

// --- Heaps ---------------------------------------------------------------------------------------

HANDLE GetProcessHeap() noexcept
{
  return static_cast<HANDLE>(malloc_default_zone());
}

HANDLE HeapCreate(const DWORD options, const SIZE_T initialSize, const SIZE_T maximumSize) noexcept
{
  (void)options;
  (void)maximumSize;
  malloc_zone_t* const zone = malloc_create_zone(initialSize, 0);
  if (zone == nullptr) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return nullptr;
  }
  malloc_set_zone_name(zone, "faf HeapCreate");
  return static_cast<HANDLE>(zone);
}

BOOL HeapDestroy(const HANDLE heap) noexcept
{
  if (heap == nullptr || IsProcessHeap(heap)) {
    ::SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  malloc_destroy_zone(ZoneOf(heap));
  return TRUE;
}

LPVOID HeapAlloc(const HANDLE heap, const DWORD flags, const SIZE_T bytes) noexcept
{
  if (heap == nullptr) {
    return nullptr;
  }
  const SIZE_T size = bytes != 0 ? bytes : 1;
  void* memory = nullptr;
  if (IsProcessHeap(heap)) {
    memory = (flags & HEAP_ZERO_MEMORY) != 0 ? std::calloc(1, size) : std::malloc(size);
  } else {
    memory = (flags & HEAP_ZERO_MEMORY) != 0 ? malloc_zone_calloc(ZoneOf(heap), 1, size)
                                             : malloc_zone_malloc(ZoneOf(heap), size);
  }
  if (memory == nullptr) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
  }
  return memory;
}

LPVOID HeapReAlloc(const HANDLE heap, const DWORD flags, LPVOID const memory, const SIZE_T bytes) noexcept
{
  if (heap == nullptr || memory == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return nullptr;
  }
  const std::size_t old = malloc_size(memory);
  const SIZE_T size = bytes != 0 ? bytes : 1;
  if ((flags & HEAP_REALLOC_IN_PLACE_ONLY) != 0) {
    if (old >= size) {
      return memory;
    }
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return nullptr;
  }
  void* const grown = IsProcessHeap(heap) ? std::realloc(memory, size) : malloc_zone_realloc(ZoneOf(heap), memory, size);
  if (grown == nullptr) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return nullptr;
  }
  if ((flags & HEAP_ZERO_MEMORY) != 0 && size > old) {
    std::memset(static_cast<unsigned char*>(grown) + old, 0, size - old);
  }
  return grown;
}

BOOL HeapFree(const HANDLE heap, const DWORD flags, LPVOID const memory) noexcept
{
  (void)flags;
  if (memory == nullptr) {
    return TRUE;
  }
  if (heap == nullptr) {
    ::SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (IsProcessHeap(heap)) {
    std::free(memory);
  } else {
    malloc_zone_free(ZoneOf(heap), memory);
  }
  return TRUE;
}

SIZE_T HeapSize(const HANDLE heap, const DWORD flags, LPCVOID const memory) noexcept
{
  (void)flags;
  if (heap == nullptr || memory == nullptr) {
    return static_cast<SIZE_T>(-1);
  }
  if (!IsProcessHeap(heap) && malloc_zone_from_ptr(memory) != ZoneOf(heap)) {
    return static_cast<SIZE_T>(-1);
  }
  const std::size_t size = malloc_size(memory);
  return size != 0 ? size : static_cast<SIZE_T>(-1);
}

BOOL HeapValidate(const HANDLE heap, const DWORD flags, LPCVOID const memory) noexcept
{
  (void)flags;
  if (heap == nullptr) {
    return FALSE;
  }
  return (memory == nullptr || malloc_size(memory) != 0) ? TRUE : FALSE;
}

SIZE_T HeapCompact(const HANDLE heap, const DWORD flags) noexcept
{
  (void)flags;
  if (heap != nullptr && !IsProcessHeap(heap)) {
    (void)malloc_zone_pressure_relief(ZoneOf(heap), 0);
  }
  return 0;
}

// --- Global/Local --------------------------------------------------------------------------------

HGLOBAL GlobalAlloc(const UINT flags, const SIZE_T bytes) noexcept
{
  return BlockAlloc(flags, bytes);
}

HGLOBAL GlobalReAlloc(HGLOBAL const memory, const SIZE_T bytes, const UINT flags) noexcept
{
  return BlockReAlloc(memory, bytes, flags);
}

HGLOBAL GlobalFree(HGLOBAL const memory) noexcept
{
  return BlockFree(memory);
}

LPVOID GlobalLock(HGLOBAL const memory) noexcept
{
  if (HeaderOf(memory) == nullptr) {
    ::SetLastError(ERROR_INVALID_HANDLE);
    return nullptr;
  }
  return memory;
}

BOOL GlobalUnlock(HGLOBAL const memory) noexcept
{
  (void)memory;
  ::SetLastError(NO_ERROR);
  return FALSE; // "no longer locked", as Windows returns for an unlocked block
}

SIZE_T GlobalSize(HGLOBAL const memory) noexcept
{
  const BlockHeader* const header = HeaderOf(memory);
  return header != nullptr ? header->size : 0;
}

HLOCAL LocalAlloc(const UINT flags, const SIZE_T bytes) noexcept
{
  return BlockAlloc(flags, bytes);
}

HLOCAL LocalReAlloc(HLOCAL const memory, const SIZE_T bytes, const UINT flags) noexcept
{
  return BlockReAlloc(memory, bytes, flags);
}

HLOCAL LocalFree(HLOCAL const memory) noexcept
{
  return BlockFree(memory);
}

LPVOID LocalLock(HLOCAL const memory) noexcept
{
  return GlobalLock(memory);
}

BOOL LocalUnlock(HLOCAL const memory) noexcept
{
  return GlobalUnlock(memory);
}

SIZE_T LocalSize(HLOCAL const memory) noexcept
{
  return GlobalSize(memory);
}

// --- Memory status ---------------------------------------------------------------------------------

BOOL GlobalMemoryStatusEx(LPMEMORYSTATUSEX const status) noexcept
{
  if (status == nullptr || status->dwLength != sizeof(MEMORYSTATUSEX)) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  std::uint64_t total = 0;
  std::size_t size = sizeof(total);
  (void)sysctlbyname("hw.memsize", &total, &size, nullptr, 0);

  vm_statistics64_data_t vm{};
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
  std::uint64_t available = 0;
  if (host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm), &count) ==
      KERN_SUCCESS) {
    const std::uint64_t page = static_cast<std::uint64_t>(vm_kernel_page_size);
    available = (static_cast<std::uint64_t>(vm.free_count) + vm.inactive_count + vm.purgeable_count +
                 vm.external_page_count) *
                page;
    available = std::min(available, total);
  }
  struct xsw_usage swap{};
  size = sizeof(swap);
  (void)sysctlbyname("vm.swapusage", &swap, &size, nullptr, 0);

  constexpr std::uint64_t kVirtual = 0x00007FFFFFFE0000ull; // x64 Windows user space
  status->dwMemoryLoad = total != 0 ? static_cast<DWORD>(((total - available) * 100) / total) : 0;
  status->ullTotalPhys = total;
  status->ullAvailPhys = available;
  status->ullTotalPageFile = total + swap.xsu_total;
  status->ullAvailPageFile = available + swap.xsu_avail;
  status->ullTotalVirtual = kVirtual;
  status->ullAvailVirtual = kVirtual;
  status->ullAvailExtendedVirtual = 0;
  return TRUE;
}

void GlobalMemoryStatus(LPMEMORYSTATUS const status) noexcept
{
  if (status == nullptr) {
    return;
  }
  MEMORYSTATUSEX ex{};
  ex.dwLength = sizeof(ex);
  (void)GlobalMemoryStatusEx(&ex);
  status->dwLength = sizeof(*status);
  status->dwMemoryLoad = ex.dwMemoryLoad;
  status->dwTotalPhys = static_cast<SIZE_T>(ex.ullTotalPhys);
  status->dwAvailPhys = static_cast<SIZE_T>(ex.ullAvailPhys);
  status->dwTotalPageFile = static_cast<SIZE_T>(ex.ullTotalPageFile);
  status->dwAvailPageFile = static_cast<SIZE_T>(ex.ullAvailPageFile);
  status->dwTotalVirtual = static_cast<SIZE_T>(ex.ullTotalVirtual);
  status->dwAvailVirtual = static_cast<SIZE_T>(ex.ullAvailVirtual);
}

BOOL GetProcessMemoryInfo(const HANDLE process, PPROCESS_MEMORY_COUNTERS const counters, const DWORD size) noexcept
{
  if (process != CurrentProcessPseudoHandle()) {
    ::SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (counters == nullptr || size < sizeof(PROCESS_MEMORY_COUNTERS)) {
    ::SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  task_vm_info_data_t vm{};
  mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&vm), &count) != KERN_SUCCESS) {
    ::SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
  }
  mach_task_basic_info_data_t basic{};
  mach_msg_type_number_t basicCount = MACH_TASK_BASIC_INFO_COUNT;
  (void)task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&basic), &basicCount);
  task_events_info_data_t events{};
  mach_msg_type_number_t eventsCount = TASK_EVENTS_INFO_COUNT;
  (void)task_info(mach_task_self(), TASK_EVENTS_INFO, reinterpret_cast<task_info_t>(&events), &eventsCount);

  std::memset(counters, 0, size >= sizeof(PROCESS_MEMORY_COUNTERS_EX) ? sizeof(PROCESS_MEMORY_COUNTERS_EX)
                                                                      : sizeof(PROCESS_MEMORY_COUNTERS));
  counters->cb = size >= sizeof(PROCESS_MEMORY_COUNTERS_EX) ? sizeof(PROCESS_MEMORY_COUNTERS_EX)
                                                            : sizeof(PROCESS_MEMORY_COUNTERS);
  counters->PageFaultCount = static_cast<DWORD>(events.faults);
  counters->WorkingSetSize = static_cast<SIZE_T>(basic.resident_size);
  counters->PeakWorkingSetSize = static_cast<SIZE_T>(basic.resident_size_max);
  counters->PagefileUsage = static_cast<SIZE_T>(vm.phys_footprint);
  counters->PeakPagefileUsage = static_cast<SIZE_T>(vm.ledger_phys_footprint_peak);
  if (size >= sizeof(PROCESS_MEMORY_COUNTERS_EX)) {
    reinterpret_cast<PPROCESS_MEMORY_COUNTERS_EX>(counters)->PrivateUsage = static_cast<SIZE_T>(vm.phys_footprint);
  }
  return TRUE;
}

BOOL K32GetProcessMemoryInfo(const HANDLE process, PPROCESS_MEMORY_COUNTERS const counters, const DWORD size) noexcept
{
  return GetProcessMemoryInfo(process, counters, size);
}

// --- CRT memory --------------------------------------------------------------------------------------

errno_t memcpy_s(void* const destination, const std::size_t destinationSize, const void* const source,
                 const std::size_t count) noexcept
{
  return CheckedCopy(destination, destinationSize, source, count, false);
}

errno_t memmove_s(void* const destination, const std::size_t destinationSize, const void* const source,
                  const std::size_t count) noexcept
{
  return CheckedCopy(destination, destinationSize, source, count, true);
}

errno_t wmemcpy_s(wchar_t* const destination, const std::size_t destinationSize, const wchar_t* const source,
                  const std::size_t count) noexcept
{
  return CheckedCopy(destination, destinationSize * sizeof(wchar_t), source, count * sizeof(wchar_t), false);
}

errno_t wmemmove_s(wchar_t* const destination, const std::size_t destinationSize, const wchar_t* const source,
                   const std::size_t count) noexcept
{
  return CheckedCopy(destination, destinationSize * sizeof(wchar_t), source, count * sizeof(wchar_t), true);
}

void* _aligned_malloc(const std::size_t size, const std::size_t alignment) noexcept
{
  if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
    errno = EINVAL;
    return nullptr;
  }
  void* memory = nullptr;
  const std::size_t effective = std::max<std::size_t>(alignment, sizeof(void*));
  if (::posix_memalign(&memory, effective, size != 0 ? size : 1) != 0) {
    errno = ENOMEM;
    return nullptr;
  }
  return memory;
}

void* _aligned_realloc(void* const memory, const std::size_t size, const std::size_t alignment) noexcept
{
  if (memory == nullptr) {
    return _aligned_malloc(size, alignment);
  }
  if (size == 0) {
    std::free(memory);
    return nullptr;
  }
  void* const grown = _aligned_malloc(size, alignment);
  if (grown == nullptr) {
    return nullptr;
  }
  std::memcpy(grown, memory, std::min(size, malloc_size(memory)));
  std::free(memory);
  return grown;
}

void _aligned_free(void* const memory) noexcept
{
  std::free(memory);
}
