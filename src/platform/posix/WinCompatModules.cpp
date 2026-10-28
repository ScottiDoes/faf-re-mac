// Win32 on POSIX: Modules (declarations in compat/Modules.h).

#include "WinCompatInternal.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <strings.h>
#include <unistd.h>

using namespace faf_posix;

namespace
{
  const mach_header* HeaderOf(const HMODULE module) noexcept
  {
    return reinterpret_cast<const mach_header*>(module);
  }

  HMODULE ModuleOf(const void* const header) noexcept
  {
    return reinterpret_cast<HMODULE>(const_cast<void*>(header));
  }

  HMODULE ExecutableModule() noexcept
  {
    for (uint32_t i = 0, n = _dyld_image_count(); i < n; ++i) {
      const mach_header* const header = _dyld_get_image_header(i);
      if (header != nullptr && header->filetype == MH_EXECUTE) {
        return ModuleOf(header);
      }
    }
    return ModuleOf(_dyld_get_image_header(0));
  }

  // The image path of a module header, or empty.
  std::string ImagePath(const HMODULE module)
  {
    if (module == nullptr) {
      char buffer[PATH_MAX];
      uint32_t size = sizeof(buffer);
      if (_NSGetExecutablePath(buffer, &size) != 0) {
        return std::string();
      }
      char resolved[PATH_MAX];
      return realpath(buffer, resolved) != nullptr ? std::string(resolved) : std::string(buffer);
    }
    for (uint32_t i = 0, n = _dyld_image_count(); i < n; ++i) {
      if (ModuleOf(_dyld_get_image_header(i)) == module) {
        const char* const name = _dyld_get_image_name(i);
        return name != nullptr ? std::string(name) : std::string();
      }
    }
    return std::string();
  }

  std::string BaseName(const std::string& path)
  {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
  }

  std::string StripExtension(const std::string& name)
  {
    const std::size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
  }

  bool IsWindowsLibraryName(const std::string& name) noexcept
  {
    return name.size() > 4 &&
           (::strcasecmp(name.c_str() + name.size() - 4, ".dll") == 0 ||
            ::strcasecmp(name.c_str() + name.size() - 4, ".drv") == 0 ||
            ::strcasecmp(name.c_str() + name.size() - 4, ".exe") == 0);
  }

  // A loaded image by file name (case-insensitive, extension optional) or full path.
  HMODULE FindLoadedModule(const std::string& name)
  {
    const std::string native = NativePath(name.c_str());
    const bool hasPath = native.find('/') != std::string::npos;
    const std::string wanted = BaseName(native);
    const std::string wantedStem = StripExtension(wanted);
    const std::string exe = ImagePath(nullptr);
    for (uint32_t i = 0, n = _dyld_image_count(); i < n; ++i) {
      const char* const imageName = _dyld_get_image_name(i);
      if (imageName == nullptr) {
        continue;
      }
      const std::string path = imageName;
      if (hasPath) {
        if (::strcasecmp(path.c_str(), native.c_str()) == 0) {
          return ModuleOf(_dyld_get_image_header(i));
        }
        continue;
      }
      const std::string base = BaseName(path);
      if (::strcasecmp(base.c_str(), wanted.c_str()) == 0 || ::strcasecmp(base.c_str(), wantedStem.c_str()) == 0 ||
          ::strcasecmp(StripExtension(base).c_str(), wantedStem.c_str()) == 0) {
        return ModuleOf(_dyld_get_image_header(i));
      }
    }
    // The executable may be named after its .exe ("ForgedAlliance.exe" -> "ForgedAlliance").
    if (!hasPath && ::strcasecmp(BaseName(exe).c_str(), wantedStem.c_str()) == 0) {
      return ExecutableModule();
    }
    return nullptr;
  }

  // A dlopen handle for a loaded module (one reference the caller must dlclose).
  void* DlHandleFor(const HMODULE module)
  {
    if (module == nullptr || module == ExecutableModule()) {
      return dlopen(nullptr, RTLD_LAZY | RTLD_NOLOAD);
    }
    const std::string path = ImagePath(module);
    if (path.empty()) {
      return nullptr;
    }
    return dlopen(path.c_str(), RTLD_LAZY | RTLD_NOLOAD);
  }

  // The header of the image a dlopen handle refers to.
  HMODULE ModuleForDlHandle(void* const handle)
  {
    for (uint32_t i = 0, n = _dyld_image_count(); i < n; ++i) {
      const char* const name = _dyld_get_image_name(i);
      if (name == nullptr) {
        continue;
      }
      void* const probe = dlopen(name, RTLD_LAZY | RTLD_NOLOAD);
      if (probe != nullptr) {
        dlclose(probe);
        if (probe == handle) {
          return ModuleOf(_dyld_get_image_header(i));
        }
      }
    }
    return nullptr;
  }

  HMODULE LoadLibraryImpl(const std::string& name, const DWORD flags)
  {
    (void)flags;
    if (name.empty()) {
      ::SetLastError(ERROR_INVALID_PARAMETER);
      return nullptr;
    }
    if (HMODULE const loaded = FindLoadedModule(name)) {
      if (loaded != ExecutableModule()) {
        // Count the reference, as LoadLibrary of a loaded module does.
        const std::string path = ImagePath(loaded);
        if (!path.empty()) {
          (void)dlopen(path.c_str(), RTLD_LAZY);
        }
      }
      return loaded;
    }
    if (IsWindowsLibraryName(name)) {
      ::SetLastError(ERROR_MOD_NOT_FOUND);
      return nullptr;
    }
    const std::string native = NativePath(name.c_str());
    void* const handle = dlopen(native.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (handle == nullptr) {
      ::SetLastError(ERROR_MOD_NOT_FOUND);
      return nullptr;
    }
    HMODULE const module = ModuleForDlHandle(handle);
    if (module == nullptr) {
      dlclose(handle);
      ::SetLastError(ERROR_MOD_NOT_FOUND);
      return nullptr;
    }
    return module;
  }

  std::uint64_t ImageSize(const mach_header* const header) noexcept
  {
    if (header == nullptr || (header->magic != MH_MAGIC_64)) {
      return 0;
    }
    const auto* const header64 = reinterpret_cast<const mach_header_64*>(header);
    const auto* command = reinterpret_cast<const load_command*>(header64 + 1);
    std::uint64_t low = UINT64_MAX;
    std::uint64_t high = 0;
    for (uint32_t i = 0; i < header64->ncmds; ++i) {
      if (command->cmd == LC_SEGMENT_64) {
        const auto* const segment = reinterpret_cast<const segment_command_64*>(command);
        if (std::strcmp(segment->segname, SEG_PAGEZERO) != 0 && segment->vmsize != 0) {
          low = std::min<std::uint64_t>(low, segment->vmaddr);
          high = std::max<std::uint64_t>(high, segment->vmaddr + segment->vmsize);
        }
      }
      command = reinterpret_cast<const load_command*>(reinterpret_cast<const char*>(command) + command->cmdsize);
    }
    return high > low ? high - low : 0;
  }

  template <class Char>
  DWORD ModuleFileNameImpl(const HMODULE module, Char* const buffer, const DWORD size)
  {
    if (buffer == nullptr || size == 0) {
      ::SetLastError(ERROR_INSUFFICIENT_BUFFER);
      return 0;
    }
    const std::string path = ImagePath(module == ExecutableModule() ? nullptr : module);
    if (path.empty()) {
      ::SetLastError(ERROR_MOD_NOT_FOUND);
      buffer[0] = 0;
      return 0;
    }
    const std::string windows = WindowsPath(path);
    std::basic_string<Char> text;
    if constexpr (sizeof(Char) == 1) {
      text = windows;
    } else {
      text = WideFromUtf8(windows.c_str());
    }
    if (text.size() + 1 > size) {
      // Truncated and terminated, returning the buffer size (Windows Vista and later).
      std::memcpy(buffer, text.data(), (size - 1) * sizeof(Char));
      buffer[size - 1] = 0;
      ::SetLastError(ERROR_INSUFFICIENT_BUFFER);
      return size;
    }
    std::memcpy(buffer, text.c_str(), (text.size() + 1) * sizeof(Char));
    ::SetLastError(ERROR_SUCCESS);
    return static_cast<DWORD>(text.size());
  }

  // --- Snapshots ---------------------------------------------------------------------------------
  struct SnapshotModule
  {
    HMODULE module;
    std::uint64_t size;
    std::string path;
  };

  struct SnapshotObject final : KernelObject
  {
    SnapshotObject() noexcept : KernelObject(ObjectKind::Snapshot) {}
    std::vector<DWORD> threads;
    std::vector<SnapshotModule> modules;
    std::size_t nextThread = 0;
    std::size_t nextModule = 0;
  };

  SnapshotObject* ReferenceSnapshot(const HANDLE snapshot) noexcept
  {
    return ReferenceHandleAs<SnapshotObject>(snapshot, ObjectKind::Snapshot);
  }

  BOOL NextThread(const HANDLE snapshot, LPTHREADENTRY32 const entry, const bool first) noexcept
  {
    SnapshotObject* const object = ReferenceSnapshot(snapshot);
    if (object == nullptr) {
      return FALSE;
    }
    if (first) {
      object->nextThread = 0;
    }
    BOOL result = FALSE;
    if (entry == nullptr || entry->dwSize < sizeof(THREADENTRY32)) {
      ::SetLastError(ERROR_BAD_LENGTH);
    } else if (object->nextThread >= object->threads.size()) {
      ::SetLastError(ERROR_NO_MORE_FILES);
    } else {
      entry->cntUsage = 0;
      entry->th32ThreadID = object->threads[object->nextThread++];
      entry->th32OwnerProcessID = static_cast<DWORD>(getpid());
      entry->tpBasePri = 8;
      entry->tpDeltaPri = 0;
      entry->dwFlags = 0;
      result = TRUE;
    }
    object->Release();
    return result;
  }

  template <class Entry>
  BOOL NextModule(const HANDLE snapshot, Entry* const entry, const bool first) noexcept
  {
    SnapshotObject* const object = ReferenceSnapshot(snapshot);
    if (object == nullptr) {
      return FALSE;
    }
    if (first) {
      object->nextModule = 0;
    }
    BOOL result = FALSE;
    try {
      if (entry == nullptr || entry->dwSize < sizeof(Entry)) {
        ::SetLastError(ERROR_BAD_LENGTH);
      } else if (object->nextModule >= object->modules.size()) {
        ::SetLastError(ERROR_NO_MORE_FILES);
      } else {
        const SnapshotModule& module = object->modules[object->nextModule++];
        entry->th32ModuleID = 1;
        entry->th32ProcessID = static_cast<DWORD>(getpid());
        entry->GlblcntUsage = 0xFFFF;
        entry->ProccntUsage = 0xFFFF;
        entry->modBaseAddr = reinterpret_cast<BYTE*>(module.module);
        entry->modBaseSize = static_cast<DWORD>(std::min<std::uint64_t>(module.size, 0xFFFFFFFFu));
        entry->hModule = module.module;
        const std::string name = BaseName(module.path);
        const std::string windows = WindowsPath(module.path);
        if constexpr (sizeof(entry->szModule[0]) == 1) {
          std::strncpy(entry->szModule, name.c_str(), sizeof(entry->szModule) - 1);
          entry->szModule[sizeof(entry->szModule) - 1] = '\0';
          std::strncpy(entry->szExePath, windows.c_str(), sizeof(entry->szExePath) - 1);
          entry->szExePath[sizeof(entry->szExePath) - 1] = '\0';
        } else {
          const std::wstring wideName = WideFromUtf8(name.c_str());
          const std::wstring widePath = WideFromUtf8(windows.c_str());
          const std::size_t nameLength = std::min<std::size_t>(wideName.size(), MAX_MODULE_NAME32);
          std::wmemcpy(entry->szModule, wideName.c_str(), nameLength);
          entry->szModule[nameLength] = L'\0';
          const std::size_t pathLength = std::min<std::size_t>(widePath.size(), MAX_PATH - 1);
          std::wmemcpy(entry->szExePath, widePath.c_str(), pathLength);
          entry->szExePath[pathLength] = L'\0';
        }
        result = TRUE;
      }
    } catch (...) {
      ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    }
    object->Release();
    return result;
  }
} // namespace

HMODULE GetModuleHandleA(LPCSTR const moduleName) noexcept
{
  if (moduleName == nullptr) {
    return ExecutableModule();
  }
  try {
    HMODULE const module = FindLoadedModule(moduleName);
    if (module == nullptr) {
      ::SetLastError(ERROR_MOD_NOT_FOUND);
    }
    return module;
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return nullptr;
  }
}

HMODULE GetModuleHandleW(LPCWSTR const moduleName) noexcept
{
  if (moduleName == nullptr) {
    return ExecutableModule();
  }
  try {
    return GetModuleHandleA(Utf8FromWide(moduleName).c_str());
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return nullptr;
  }
}

BOOL GetModuleHandleExA(const DWORD flags, LPCSTR const moduleName, HMODULE* const module) noexcept
{
  if (module == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  *module = nullptr;
  if ((flags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) != 0) {
    Dl_info info{};
    if (moduleName == nullptr || dladdr(moduleName, &info) == 0 || info.dli_fbase == nullptr) {
      ::SetLastError(ERROR_MOD_NOT_FOUND);
      return FALSE;
    }
    *module = ModuleOf(info.dli_fbase);
    return TRUE;
  }
  *module = GetModuleHandleA(moduleName);
  return *module != nullptr ? TRUE : FALSE;
}

BOOL GetModuleHandleExW(const DWORD flags, LPCWSTR const moduleName, HMODULE* const module) noexcept
{
  if ((flags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) != 0) {
    return GetModuleHandleExA(flags, reinterpret_cast<LPCSTR>(moduleName), module);
  }
  if (module == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  *module = GetModuleHandleW(moduleName);
  return *module != nullptr ? TRUE : FALSE;
}

DWORD GetModuleFileNameA(const HMODULE module, LPSTR const fileName, const DWORD size) noexcept
{
  try {
    return ModuleFileNameImpl(module, fileName, size);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
}

DWORD GetModuleFileNameW(const HMODULE module, LPWSTR const fileName, const DWORD size) noexcept
{
  try {
    return ModuleFileNameImpl(module, fileName, size);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
}

HMODULE LoadLibraryA(LPCSTR const fileName) noexcept
{
  return LoadLibraryExA(fileName, nullptr, 0);
}

HMODULE LoadLibraryW(LPCWSTR const fileName) noexcept
{
  return LoadLibraryExW(fileName, nullptr, 0);
}

HMODULE LoadLibraryExA(LPCSTR const fileName, const HANDLE file, const DWORD flags) noexcept
{
  (void)file;
  if (fileName == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return nullptr;
  }
  try {
    return LoadLibraryImpl(fileName, flags);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return nullptr;
  }
}

HMODULE LoadLibraryExW(LPCWSTR const fileName, const HANDLE file, const DWORD flags) noexcept
{
  (void)file;
  if (fileName == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return nullptr;
  }
  try {
    return LoadLibraryImpl(Utf8FromWide(fileName), flags);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return nullptr;
  }
}

BOOL FreeLibrary(const HMODULE module) noexcept
{
  if (module == nullptr) {
    ::SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (module == ExecutableModule()) {
    return TRUE;
  }
  try {
    void* const handle = DlHandleFor(module);
    if (handle == nullptr) {
      ::SetLastError(ERROR_INVALID_HANDLE);
      return FALSE;
    }
    dlclose(handle); // the probe's reference
    dlclose(handle); // the LoadLibrary reference
    return TRUE;
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

FARPROC GetProcAddress(const HMODULE module, LPCSTR const procName) noexcept
{
  if (procName == nullptr || (reinterpret_cast<std::uintptr_t>(procName) >> 16) == 0) {
    ::SetLastError(ERROR_PROC_NOT_FOUND); // ordinals: no export tables on POSIX
    return nullptr;
  }
  try {
    void* const handle = DlHandleFor(module);
    if (handle == nullptr) {
      ::SetLastError(ERROR_MOD_NOT_FOUND);
      return nullptr;
    }
    void* const symbol = dlsym(handle, procName);
    dlclose(handle);
    if (symbol == nullptr) {
      ::SetLastError(ERROR_PROC_NOT_FOUND);
    }
    return reinterpret_cast<FARPROC>(symbol);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return nullptr;
  }
}

BOOL DisableThreadLibraryCalls(const HMODULE module) noexcept
{
  (void)module;
  return TRUE;
}

// --- Tool help ----------------------------------------------------------------------------------

HANDLE CreateToolhelp32Snapshot(const DWORD flags, const DWORD processId) noexcept
{
  if (processId != 0 && processId != static_cast<DWORD>(getpid())) {
    ::SetLastError(ERROR_ACCESS_DENIED);
    return INVALID_HANDLE_VALUE;
  }
  auto* const snapshot = new (std::nothrow) SnapshotObject();
  if (snapshot == nullptr) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return INVALID_HANDLE_VALUE;
  }
  try {
    if ((flags & TH32CS_SNAPTHREAD) != 0) {
      snapshot->threads = LiveThreadIds();
    }
    if ((flags & (TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32)) != 0) {
      for (uint32_t i = 0, n = _dyld_image_count(); i < n; ++i) {
        const mach_header* const header = _dyld_get_image_header(i);
        const char* const name = _dyld_get_image_name(i);
        if (header == nullptr || name == nullptr) {
          continue;
        }
        snapshot->modules.push_back(SnapshotModule{ModuleOf(header), ImageSize(header), name});
      }
    }
  } catch (...) {
    snapshot->Release();
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return INVALID_HANDLE_VALUE;
  }
  const HANDLE handle = InsertHandle(snapshot);
  return handle != nullptr ? handle : INVALID_HANDLE_VALUE;
}

BOOL Thread32First(const HANDLE snapshot, LPTHREADENTRY32 const entry) noexcept
{
  return NextThread(snapshot, entry, true);
}

BOOL Thread32Next(const HANDLE snapshot, LPTHREADENTRY32 const entry) noexcept
{
  return NextThread(snapshot, entry, false);
}

BOOL Module32First(const HANDLE snapshot, LPMODULEENTRY32 const entry) noexcept
{
  return NextModule(snapshot, entry, true);
}

BOOL Module32Next(const HANDLE snapshot, LPMODULEENTRY32 const entry) noexcept
{
  return NextModule(snapshot, entry, false);
}

BOOL Module32FirstW(const HANDLE snapshot, LPMODULEENTRY32W const entry) noexcept
{
  return NextModule(snapshot, entry, true);
}

BOOL Module32NextW(const HANDLE snapshot, LPMODULEENTRY32W const entry) noexcept
{
  return NextModule(snapshot, entry, false);
}
