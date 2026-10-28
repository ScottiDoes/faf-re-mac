// Win32 on POSIX: Files (declarations in compat/Files.h).

#include "WinCompatInternal.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <new>
#include <string>
#include <vector>

#include <copyfile.h>
#include <dirent.h>
#include <fcntl.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

using namespace faf_posix;

namespace
{
  constexpr DWORD kStatusPending = 0x103;
  constexpr ULONG_PTR kStatusEndOfFile = 0xC0000011;

  // --- Path conversion ---------------------------------------------------------------------
  bool IsDriveLetter(const char c) noexcept
  {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
  }

  bool HasWildcards(const std::string& text) noexcept
  {
    return text.find_first_of("*?") != std::string::npos;
  }

  bool EqualsIgnoreCase(const std::string& a, const char* const b) noexcept
  {
    return ::strcasecmp(a.c_str(), b) == 0;
  }

  // Lexically normalises an absolute POSIX path ("." and ".." removed, single slashes),
  // keeping a trailing slash.
  std::string NormalizeAbsolute(const std::string& path)
  {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= path.size()) {
      const std::size_t end = path.find('/', start);
      const std::string part = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
      if (part.empty() || part == ".") {
        // skip
      } else if (part == "..") {
        if (!parts.empty()) {
          parts.pop_back();
        }
      } else {
        parts.push_back(part);
      }
      if (end == std::string::npos) {
        break;
      }
      start = end + 1;
    }
    std::string out;
    for (const std::string& part : parts) {
      out += '/';
      out += part;
    }
    if (out.empty()) {
      out = "/";
    } else if (path.size() > 1 && path.back() == '/') {
      out += '/';
    }
    return out;
  }

  std::string CurrentDirectoryPosix()
  {
    char buffer[PATH_MAX];
    if (getcwd(buffer, sizeof(buffer)) == nullptr) {
      return "/";
    }
    return buffer;
  }

  // Case-insensitive volumes need no component search.
  bool VolumeIsCaseSensitive(const std::string& existingDirectory) noexcept
  {
    const long result = pathconf(existingDirectory.empty() ? "." : existingDirectory.c_str(), _PC_CASE_SENSITIVE);
    return result == 1;
  }

  std::string ResolveCase(const std::string& path)
  {
    struct stat info{};
    if (path.empty() || lstat(path.c_str(), &info) == 0 || (errno != ENOENT && errno != ENOTDIR)) {
      return path;
    }
    const bool absolute = path[0] == '/';
    std::vector<std::string> parts;
    std::size_t start = absolute ? 1 : 0;
    while (start <= path.size()) {
      const std::size_t end = path.find('/', start);
      parts.push_back(path.substr(start, end == std::string::npos ? std::string::npos : end - start));
      if (end == std::string::npos) {
        break;
      }
      start = end + 1;
    }

    std::string resolved = absolute ? "/" : "";
    auto probeOf = [&](const std::string& prefix) { return prefix.empty() ? std::string(".") : prefix; };
    bool searching = true;
    bool checkedVolume = false;
    for (std::size_t i = 0; i < parts.size(); ++i) {
      const std::string& part = parts[i];
      const bool needsSlash = !resolved.empty() && resolved.back() != '/';
      std::string candidate = resolved + (needsSlash ? "/" : "") + part;
      if (!searching || part.empty() || part == "." || part == ".." || lstat(candidate.c_str(), &info) == 0) {
        resolved = candidate;
        continue;
      }
      if (!checkedVolume) {
        checkedVolume = true;
        if (!VolumeIsCaseSensitive(probeOf(resolved))) {
          return path; // a real miss
        }
      }
      DIR* const directory = opendir(probeOf(resolved).c_str());
      std::string match;
      if (directory != nullptr) {
        while (const dirent* const entry = readdir(directory)) {
          if (EqualsIgnoreCase(part, entry->d_name)) {
            match = entry->d_name;
            break;
          }
        }
        closedir(directory);
      }
      if (match.empty()) {
        searching = false;
        resolved = candidate;
      } else {
        resolved = resolved + (needsSlash ? "/" : "") + match;
      }
    }
    return resolved;
  }

  // --- Stat helpers ------------------------------------------------------------------------
  FILETIME FileTimeOf(const timespec& time) noexcept
  {
    return FileTimeFromUnix(time.tv_sec, time.tv_nsec);
  }

  DWORD AttributesOf(const struct stat& info) noexcept
  {
    DWORD attributes = 0;
    if (S_ISDIR(info.st_mode)) {
      attributes |= FILE_ATTRIBUTE_DIRECTORY;
    } else {
      attributes |= FILE_ATTRIBUTE_ARCHIVE;
    }
    if ((info.st_mode & S_IWUSR) == 0) {
      attributes |= FILE_ATTRIBUTE_READONLY;
    }
    if ((info.st_flags & UF_HIDDEN) != 0) {
      attributes |= FILE_ATTRIBUTE_HIDDEN;
    }
    if (S_ISCHR(info.st_mode) || S_ISBLK(info.st_mode)) {
      attributes = FILE_ATTRIBUTE_DEVICE;
    }
    return attributes;
  }

  // stat, falling back to lstat for a dangling symlink.
  bool StatPath(const std::string& path, struct stat* const info) noexcept
  {
    if (stat(path.c_str(), info) == 0) {
      return true;
    }
    const int error = errno;
    if (lstat(path.c_str(), info) == 0) {
      return true;
    }
    errno = error;
    return false;
  }

  bool ParentExists(const std::string& path) noexcept
  {
    const std::size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) {
      return true;
    }
    const std::string parent = slash == 0 ? "/" : path.substr(0, slash);
    struct stat info{};
    return stat(parent.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
  }

  // ERROR_FILE_NOT_FOUND vs ERROR_PATH_NOT_FOUND, as Windows tells them apart.
  void SetNotFoundError(const std::string& path, const int error) noexcept
  {
    if (error == ENOENT || error == ENOTDIR) {
      ::SetLastError(ParentExists(path) ? ERROR_FILE_NOT_FOUND : ERROR_PATH_NOT_FOUND);
    } else {
      SetLastErrorFromErrno(error);
    }
  }

  void SplitSize(const std::uint64_t size, DWORD* const high, DWORD* const low) noexcept
  {
    *high = static_cast<DWORD>(size >> 32);
    *low = static_cast<DWORD>(size & 0xFFFFFFFFu);
  }

  // --- Objects -----------------------------------------------------------------------------
  struct FileObject final : KernelObject
  {
    FileObject() noexcept : KernelObject(ObjectKind::File) {}
    ~FileObject() override
    {
      if (fd >= 0 && ownsFd) {
        ::close(fd);
      }
      if (deleteOnClose && !path.empty()) {
        if (isDirectory) {
          ::rmdir(path.c_str());
        } else {
          ::unlink(path.c_str());
        }
      }
    }
    int fd = -1;
    bool ownsFd = true;
    bool isDirectory = false;
    bool overlapped = false;
    bool deleteOnClose = false;
    std::string path;
  };

  struct FindObject final : KernelObject
  {
    FindObject() noexcept : KernelObject(ObjectKind::Find) {}
    std::string directory;
    std::vector<std::string> names;
    std::size_t next = 0;
  };

  struct MappingObject final : KernelObject
  {
    MappingObject() noexcept : KernelObject(ObjectKind::FileMapping) {}
    ~MappingObject() override
    {
      if (fd >= 0) {
        ::close(fd);
      }
    }
    int fd = -1;
    std::uint64_t size = 0;
    DWORD protect = PAGE_READONLY;
  };

  FileObject* ReferenceFile(const HANDLE handle) noexcept
  {
    return ReferenceHandleAs<FileObject>(handle, ObjectKind::File);
  }

  struct FileRef
  {
    explicit FileRef(const HANDLE handle) noexcept : object(ReferenceFile(handle)) {}
    ~FileRef()
    {
      if (object != nullptr) {
        object->Release();
      }
    }
    FileRef(const FileRef&) = delete;
    FileRef& operator=(const FileRef&) = delete;
    FileObject* object;
  };

  // --- CreateFile ----------------------------------------------------------------------------
  HANDLE CreateFileImpl(const char* const pathText, const std::string& path, const DWORD access, const DWORD disposition,
                        const DWORD flags) noexcept
  {
    if (pathText == nullptr || pathText[0] == '\0') {
      ::SetLastError(ERROR_PATH_NOT_FOUND);
      return INVALID_HANDLE_VALUE;
    }
    const bool wantsRead = (access & (GENERIC_READ | GENERIC_ALL | FILE_READ_DATA)) != 0;
    const bool wantsWrite = (access & (GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA | FILE_APPEND_DATA)) != 0;
    const bool appendOnly =
      (access & FILE_APPEND_DATA) != 0 && (access & (GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA)) == 0;

    struct stat info{};
    const bool existed = StatPath(path, &info);
    if (existed && S_ISDIR(info.st_mode)) {
      if ((flags & FILE_FLAG_BACKUP_SEMANTICS) == 0 || disposition == CREATE_NEW || disposition == CREATE_ALWAYS ||
          disposition == TRUNCATE_EXISTING) {
        ::SetLastError(disposition == CREATE_NEW ? ERROR_FILE_EXISTS : ERROR_ACCESS_DENIED);
        return INVALID_HANDLE_VALUE;
      }
      const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
      if (fd < 0) {
        SetLastErrorFromErrno(errno);
        return INVALID_HANDLE_VALUE;
      }
      auto* const object = new (std::nothrow) FileObject();
      if (object == nullptr) {
        ::close(fd);
        ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return INVALID_HANDLE_VALUE;
      }
      object->fd = fd;
      object->isDirectory = true;
      object->overlapped = (flags & FILE_FLAG_OVERLAPPED) != 0;
      try {
        object->path = path;
      } catch (...) {
      }
      const HANDLE handle = InsertHandle(object);
      if (handle == nullptr) {
        return INVALID_HANDLE_VALUE;
      }
      ::SetLastError(disposition == OPEN_ALWAYS ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
      return handle;
    }

    int oflags = O_CLOEXEC;
    if (wantsRead && wantsWrite) {
      oflags |= O_RDWR;
    } else if (wantsWrite) {
      oflags |= O_WRONLY;
    } else {
      oflags |= O_RDONLY;
    }
    if (appendOnly) {
      oflags |= O_APPEND;
    }
    switch (disposition) {
      case CREATE_NEW:
        oflags |= O_CREAT | O_EXCL;
        break;
      case CREATE_ALWAYS:
        oflags |= O_CREAT | O_TRUNC;
        break;
      case OPEN_EXISTING:
        break;
      case OPEN_ALWAYS:
        oflags |= O_CREAT;
        break;
      case TRUNCATE_EXISTING:
        if (!wantsWrite) {
          ::SetLastError(ERROR_INVALID_PARAMETER);
          return INVALID_HANDLE_VALUE;
        }
        oflags |= O_TRUNC;
        break;
      default:
        ::SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    if ((oflags & O_TRUNC) != 0 && (oflags & O_ACCMODE) == O_RDONLY) {
      // CREATE_ALWAYS with read-only access still truncates on Windows.
      oflags = (oflags & ~O_ACCMODE) | O_RDWR;
    }
    const mode_t mode = (flags & FILE_ATTRIBUTE_READONLY) != 0 ? 0444 : 0666;
    const int fd = ::open(path.c_str(), oflags, mode);
    if (fd < 0) {
      const int error = errno;
      if (error == EEXIST) {
        ::SetLastError(ERROR_FILE_EXISTS);
      } else {
        SetNotFoundError(path, error);
      }
      return INVALID_HANDLE_VALUE;
    }
    auto* const object = new (std::nothrow) FileObject();
    if (object == nullptr) {
      ::close(fd);
      ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return INVALID_HANDLE_VALUE;
    }
    object->fd = fd;
    object->overlapped = (flags & FILE_FLAG_OVERLAPPED) != 0;
    object->deleteOnClose = (flags & FILE_FLAG_DELETE_ON_CLOSE) != 0;
    try {
      object->path = path;
    } catch (...) {
      object->deleteOnClose = false;
    }
    const HANDLE handle = InsertHandle(object);
    if (handle == nullptr) {
      return INVALID_HANDLE_VALUE;
    }
    ::SetLastError((existed && (disposition == CREATE_ALWAYS || disposition == OPEN_ALWAYS)) ? ERROR_ALREADY_EXISTS
                                                                                            : ERROR_SUCCESS);
    return handle;
  }

  std::string SpecialDevicePath(const std::string& native)
  {
    std::string name = native;
    if (name.rfind("//./", 0) == 0) {
      name = name.substr(4);
    }
    if (::strcasecmp(name.c_str(), "nul") == 0) {
      return "/dev/null";
    }
    return std::string();
  }

  std::string OpenPath(const char* const path)
  {
    const std::string native = NativePath(path);
    const std::string device = SpecialDevicePath(native);
    return device.empty() ? ResolveCase(native) : device;
  }

  // --- Directory enumeration -------------------------------------------------------------------
  bool WildcardMatch(const char* name, const char* pattern) noexcept
  {
    const char* starPattern = nullptr;
    const char* starName = nullptr;
    while (*name != '\0') {
      if (*pattern == '*') {
        starPattern = ++pattern;
        starName = name;
        continue;
      }
      if (*pattern == '?' ||
          std::tolower(static_cast<unsigned char>(*pattern)) == std::tolower(static_cast<unsigned char>(*name))) {
        ++pattern;
        ++name;
        continue;
      }
      if (starPattern != nullptr) {
        pattern = starPattern;
        name = ++starName;
        continue;
      }
      return false;
    }
    while (*pattern == '*' || *pattern == '?') {
      ++pattern;
    }
    return *pattern == '\0';
  }

  // Windows (DOS) pattern semantics on top of '*' and '?': "x.*" also matches "x", "*."
  // matches names without an extension.
  bool DosMatch(const std::string& name, const std::string& pattern) noexcept
  {
    if (pattern == "*" || pattern == "*.*") {
      return true;
    }
    if (WildcardMatch(name.c_str(), pattern.c_str())) {
      return true;
    }
    const bool hasDot = name.find('.') != std::string::npos;
    if (pattern.size() >= 2 && pattern.compare(pattern.size() - 2, 2, ".*") == 0 && !hasDot) {
      return WildcardMatch(name.c_str(), pattern.substr(0, pattern.size() - 2).c_str());
    }
    if (!pattern.empty() && pattern.back() == '.' && !hasDot) {
      return WildcardMatch(name.c_str(), pattern.substr(0, pattern.size() - 1).c_str());
    }
    return false;
  }

  struct FoundEntry
  {
    std::string name;
    DWORD attributes = 0;
    FILETIME created{};
    FILETIME accessed{};
    FILETIME written{};
    std::uint64_t size = 0;
  };

  bool DescribeEntry(const std::string& directory, const std::string& name, FoundEntry* const entry)
  {
    const std::string full = directory == "/" ? "/" + name : directory + "/" + name;
    struct stat info{};
    if (!StatPath(full, &info)) {
      return false;
    }
    entry->name = name;
    entry->attributes = AttributesOf(info);
    entry->created = FileTimeOf(info.st_birthtimespec);
    entry->accessed = FileTimeOf(info.st_atimespec);
    entry->written = FileTimeOf(info.st_mtimespec);
    entry->size = S_ISDIR(info.st_mode) ? 0 : static_cast<std::uint64_t>(info.st_size);
    return true;
  }

  // NTFS returns a directory's entries in its index order: names compared as UTF-16 code
  // units after upper-casing through the volume's upcase table. readdir on APFS returns hash
  // order instead, and the engine's VFS/mod scans must see Windows' order (load order,
  // blueprint ids). Upper-casing covers ASCII and Latin-1, the names game data uses.
  std::u16string NtfsCollationKey(const std::string& name)
  {
    std::u16string key;
    const std::wstring wide = WideFromUtf8(name.c_str());
    key.reserve(wide.size());
    for (const wchar_t c : wide) {
      char32_t value = static_cast<char32_t>(c);
      if (value >= U'a' && value <= U'z') {
        value -= 0x20;
      } else if ((value >= 0xE0 && value <= 0xFE && value != 0xF7)) {
        value -= 0x20;
      } else if (value == 0xFF) {
        value = 0x178;
      }
      if (value >= 0x10000) {
        value -= 0x10000;
        key.push_back(static_cast<char16_t>(0xD800 + (value >> 10)));
        key.push_back(static_cast<char16_t>(0xDC00 + (value & 0x3FF)));
      } else {
        key.push_back(static_cast<char16_t>(value));
      }
    }
    return key;
  }

  void SortLikeNtfs(std::vector<std::string>& names)
  {
    std::vector<std::pair<std::u16string, std::string>> keyed;
    keyed.reserve(names.size());
    std::vector<std::string> dots;
    for (std::string& name : names) {
      if (name == "." || name == "..") {
        dots.push_back(std::move(name)); // FindFirstFile reports these first
      } else {
        keyed.emplace_back(NtfsCollationKey(name), std::move(name));
      }
    }
    std::stable_sort(keyed.begin(), keyed.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    std::sort(dots.begin(), dots.end());
    names.clear();
    for (std::string& dot : dots) {
      names.push_back(std::move(dot));
    }
    for (auto& entry : keyed) {
      names.push_back(std::move(entry.second));
    }
  }

  // Opens a search: on success the FindObject (not yet in the table) and its first entry.
  FindObject* BeginFind(const std::string& nativePattern, FoundEntry* const first)
  {
    const std::string resolved = ResolveCase(nativePattern);
    const std::size_t slash = resolved.find_last_of('/');
    std::string directory;
    std::string spec;
    if (slash == std::string::npos) {
      directory = ".";
      spec = resolved;
    } else {
      directory = slash == 0 ? "/" : resolved.substr(0, slash);
      spec = resolved.substr(slash + 1);
    }
    if (spec.empty()) {
      ::SetLastError(ERROR_FILE_NOT_FOUND);
      return nullptr;
    }
    DIR* const dir = opendir(directory.c_str());
    if (dir == nullptr) {
      ::SetLastError(errno == ENOENT || errno == ENOTDIR ? ERROR_PATH_NOT_FOUND : Win32ErrorFromErrno(errno));
      return nullptr;
    }
    auto* const find = new FindObject();
    find->directory = directory;
    const bool wild = HasWildcards(spec);
    while (const dirent* const entry = readdir(dir)) {
      const std::string name = entry->d_name;
      if (wild ? DosMatch(name, spec) : EqualsIgnoreCase(name, spec.c_str())) {
        find->names.push_back(name);
      }
    }
    closedir(dir);
    SortLikeNtfs(find->names);
    if (!wild && find->names.empty()) {
      // Not listed (e.g. "." or ".." of the root): fall back to the name itself.
      struct stat info{};
      if (StatPath(resolved, &info)) {
        find->names.push_back(spec);
      }
    }
    while (find->next < find->names.size()) {
      if (DescribeEntry(directory, find->names[find->next++], first)) {
        return find;
      }
    }
    find->Release();
    ::SetLastError(ERROR_FILE_NOT_FOUND);
    return nullptr;
  }

  bool NextFind(FindObject* const find, FoundEntry* const entry)
  {
    while (find->next < find->names.size()) {
      if (DescribeEntry(find->directory, find->names[find->next++], entry)) {
        return true;
      }
    }
    ::SetLastError(ERROR_NO_MORE_FILES);
    return false;
  }

  template <class Char, std::size_t N>
  void CopyName(Char (&target)[N], const std::string& name)
  {
    if constexpr (sizeof(Char) == 1) {
      std::size_t length = std::min(name.size(), N - 1);
      while (length > 0 && length < name.size() && (static_cast<unsigned char>(name[length]) & 0xC0) == 0x80) {
        --length; // do not split a UTF-8 sequence
      }
      std::memcpy(target, name.data(), length);
      target[length] = '\0';
    } else {
      const std::wstring wide = WideFromUtf8(name.c_str());
      const std::size_t length = std::min(wide.size(), N - 1);
      std::wmemcpy(target, wide.data(), length);
      target[length] = L'\0';
    }
  }

  template <class Data>
  void FillFindData(const FoundEntry& entry, Data* const data)
  {
    std::memset(data, 0, sizeof(*data));
    data->dwFileAttributes = entry.attributes;
    data->ftCreationTime = entry.created;
    data->ftLastAccessTime = entry.accessed;
    data->ftLastWriteTime = entry.written;
    SplitSize(entry.size, &data->nFileSizeHigh, &data->nFileSizeLow);
    CopyName(data->cFileName, entry.name);
  }

  template <class Data>
  void FillCrtFindData(const FoundEntry& entry, Data* const data)
  {
    std::memset(data, 0, sizeof(*data));
    data->attrib = entry.attributes & (_A_RDONLY | _A_HIDDEN | _A_SYSTEM | _A_SUBDIR | _A_ARCH);
    auto unixTime = [](const FILETIME& time) -> __time64_t {
      const std::uint64_t value = FileTimeToU64(time);
      return value < kFileTimeUnixEpoch ? 0 : static_cast<__time64_t>((value - kFileTimeUnixEpoch) / 10000000ull);
    };
    data->time_create = unixTime(entry.created);
    data->time_access = unixTime(entry.accessed);
    data->time_write = unixTime(entry.written);
    data->size = static_cast<__int64>(entry.size);
    CopyName(data->name, entry.name);
  }

  template <class Data>
  HANDLE FindFirstImpl(const std::string& nativePattern, Data* const data) noexcept
  {
    if (data == nullptr) {
      ::SetLastError(ERROR_INVALID_PARAMETER);
      return INVALID_HANDLE_VALUE;
    }
    try {
      FoundEntry first;
      FindObject* const find = BeginFind(nativePattern, &first);
      if (find == nullptr) {
        return INVALID_HANDLE_VALUE;
      }
      FillFindData(first, data);
      const HANDLE handle = InsertHandle(find);
      return handle != nullptr ? handle : INVALID_HANDLE_VALUE;
    } catch (...) {
      ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return INVALID_HANDLE_VALUE;
    }
  }

  template <class Data>
  BOOL FindNextImpl(const HANDLE handle, Data* const data) noexcept
  {
    auto* const find = ReferenceHandleAs<FindObject>(handle, ObjectKind::Find);
    if (find == nullptr) {
      return FALSE;
    }
    BOOL result = FALSE;
    try {
      FoundEntry entry;
      if (NextFind(find, &entry)) {
        FillFindData(entry, data);
        result = TRUE;
      }
    } catch (...) {
      ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    }
    find->Release();
    return result;
  }

  template <class Data>
  std::intptr_t CrtFindFirst(const std::string& nativePattern, Data* const data) noexcept
  {
    if (data == nullptr) {
      errno = EINVAL;
      return -1;
    }
    try {
      FoundEntry first;
      FindObject* const find = BeginFind(nativePattern, &first);
      if (find == nullptr) {
        errno = ::GetLastError() == ERROR_NOT_ENOUGH_MEMORY ? ENOMEM : ENOENT;
        return -1;
      }
      FillCrtFindData(first, data);
      const HANDLE handle = InsertHandle(find);
      if (handle == nullptr) {
        errno = ENOMEM;
        return -1;
      }
      return reinterpret_cast<std::intptr_t>(handle);
    } catch (...) {
      errno = ENOMEM;
      return -1;
    }
  }

  template <class Data>
  int CrtFindNext(const std::intptr_t handle, Data* const data) noexcept
  {
    auto* const find = ReferenceHandleAs<FindObject>(reinterpret_cast<HANDLE>(handle), ObjectKind::Find);
    if (find == nullptr || data == nullptr) {
      if (find != nullptr) {
        find->Release();
      }
      errno = EINVAL;
      return -1;
    }
    int result = -1;
    try {
      FoundEntry entry;
      if (NextFind(find, &entry)) {
        FillCrtFindData(entry, data);
        result = 0;
      } else {
        errno = ENOENT;
      }
    } catch (...) {
      errno = ENOMEM;
    }
    find->Release();
    return result;
  }

  // --- Attribute queries -------------------------------------------------------------------
  DWORD AttributesImpl(const std::string& path) noexcept
  {
    struct stat info{};
    if (!StatPath(path, &info)) {
      SetNotFoundError(path, errno);
      return INVALID_FILE_ATTRIBUTES;
    }
    return AttributesOf(info);
  }

  BOOL AttributesExImpl(const std::string& path, const GET_FILEEX_INFO_LEVELS level, void* const out) noexcept
  {
    if (level != GetFileExInfoStandard || out == nullptr) {
      ::SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
    }
    struct stat info{};
    if (!StatPath(path, &info)) {
      SetNotFoundError(path, errno);
      return FALSE;
    }
    auto* const data = static_cast<WIN32_FILE_ATTRIBUTE_DATA*>(out);
    data->dwFileAttributes = AttributesOf(info);
    data->ftCreationTime = FileTimeOf(info.st_birthtimespec);
    data->ftLastAccessTime = FileTimeOf(info.st_atimespec);
    data->ftLastWriteTime = FileTimeOf(info.st_mtimespec);
    SplitSize(S_ISDIR(info.st_mode) ? 0 : static_cast<std::uint64_t>(info.st_size), &data->nFileSizeHigh,
              &data->nFileSizeLow);
    return TRUE;
  }

  BOOL SetAttributesImpl(const std::string& path, const DWORD attributes) noexcept
  {
    struct stat info{};
    if (stat(path.c_str(), &info) != 0) {
      SetNotFoundError(path, errno);
      return FALSE;
    }
    mode_t mode = info.st_mode & 07777;
    if ((attributes & FILE_ATTRIBUTE_READONLY) != 0) {
      mode &= static_cast<mode_t>(~(S_IWUSR | S_IWGRP | S_IWOTH));
    } else {
      mode |= S_IWUSR;
    }
    if (chmod(path.c_str(), mode) != 0) {
      SetLastErrorFromErrno(errno);
      return FALSE;
    }
    return TRUE;
  }

  // --- Directory and file operations -----------------------------------------------------------
  BOOL CreateDirectoryImpl(const std::string& path) noexcept
  {
    if (::mkdir(path.c_str(), 0777) != 0) {
      const int error = errno;
      if (error == EEXIST) {
        ::SetLastError(ERROR_ALREADY_EXISTS);
      } else if (error == ENOENT || error == ENOTDIR) {
        ::SetLastError(ERROR_PATH_NOT_FOUND);
      } else {
        SetLastErrorFromErrno(error);
      }
      return FALSE;
    }
    return TRUE;
  }

  BOOL RemoveDirectoryImpl(const std::string& path) noexcept
  {
    if (::rmdir(path.c_str()) != 0) {
      const int error = errno;
      if (error == ENOTDIR) {
        ::SetLastError(ERROR_DIRECTORY);
      } else {
        SetNotFoundError(path, error);
      }
      return FALSE;
    }
    return TRUE;
  }

  BOOL DeleteFileImpl(const std::string& path) noexcept
  {
    struct stat info{};
    if (lstat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode)) {
      ::SetLastError(ERROR_ACCESS_DENIED);
      return FALSE;
    }
    if (::unlink(path.c_str()) != 0) {
      SetNotFoundError(path, errno);
      return FALSE;
    }
    return TRUE;
  }

  bool CopyFileData(const std::string& from, const std::string& to, const bool failIfExists) noexcept
  {
    copyfile_flags_t flags = COPYFILE_ALL;
    if (failIfExists) {
      flags |= COPYFILE_EXCL;
    }
    if (copyfile(from.c_str(), to.c_str(), nullptr, flags) != 0) {
      const int error = errno;
      if (error == EEXIST) {
        ::SetLastError(ERROR_FILE_EXISTS);
      } else if (error == ENOENT) {
        struct stat info{};
        SetNotFoundError(stat(from.c_str(), &info) == 0 ? to : from, error);
      } else {
        SetLastErrorFromErrno(error);
      }
      return false;
    }
    return true;
  }

  BOOL MoveFileImpl(const std::string& from, const std::string& to, const DWORD flags) noexcept
  {
    struct stat source{};
    if (lstat(from.c_str(), &source) != 0) {
      SetNotFoundError(from, errno);
      return FALSE;
    }
    struct stat target{};
    const bool targetExists = lstat(to.c_str(), &target) == 0;
    const bool sameFile = targetExists && source.st_dev == target.st_dev && source.st_ino == target.st_ino;
    if (targetExists && !sameFile) {
      if ((flags & MOVEFILE_REPLACE_EXISTING) == 0) {
        ::SetLastError(ERROR_ALREADY_EXISTS);
        return FALSE;
      }
      if (S_ISDIR(target.st_mode)) {
        ::SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
      }
    }
    if (::rename(from.c_str(), to.c_str()) == 0) {
      return TRUE;
    }
    const int error = errno;
    if (error == EXDEV && !S_ISDIR(source.st_mode)) {
      // MoveFile copies across volumes; MoveFileEx needs MOVEFILE_COPY_ALLOWED.
      if ((flags & MOVEFILE_COPY_ALLOWED) == 0 && (flags & 0x80000000u) == 0) {
        ::SetLastError(ERROR_NOT_SAME_DEVICE);
        return FALSE;
      }
      if (!CopyFileData(from, to, false)) {
        return FALSE;
      }
      (void)::unlink(from.c_str());
      return TRUE;
    }
    SetNotFoundError(error == ENOENT ? to : from, error);
    return FALSE;
  }

  BOOL ReplaceFileImpl(const std::string& replaced, const std::string& replacement, const std::string* const backup) noexcept
  {
    struct stat info{};
    if (lstat(replaced.c_str(), &info) != 0) {
      ::SetLastError(ERROR_FILE_NOT_FOUND);
      return FALSE;
    }
    if (lstat(replacement.c_str(), &info) != 0) {
      ::SetLastError(ERROR_FILE_NOT_FOUND);
      return FALSE;
    }
    if (backup != nullptr && !backup->empty()) {
      if (::rename(replaced.c_str(), backup->c_str()) != 0) {
        ::SetLastError(ERROR_UNABLE_TO_REMOVE_REPLACED);
        return FALSE;
      }
    }
    if (::rename(replacement.c_str(), replaced.c_str()) != 0) {
      ::SetLastError(backup != nullptr && !backup->empty() ? ERROR_UNABLE_TO_MOVE_REPLACEMENT_2
                                                           : ERROR_UNABLE_TO_MOVE_REPLACEMENT);
      return FALSE;
    }
    return TRUE;
  }

  // --- Standard handles ------------------------------------------------------------------------
  pthread_mutex_t gStdLock = PTHREAD_MUTEX_INITIALIZER;
  HANDLE gStdHandles[3] = {nullptr, nullptr, nullptr};
  bool gStdOverridden[3] = {false, false, false};

  int StdIndex(const DWORD which) noexcept
  {
    switch (which) {
      case STD_INPUT_HANDLE:
        return 0;
      case STD_OUTPUT_HANDLE:
        return 1;
      case STD_ERROR_HANDLE:
        return 2;
      default:
        return -1;
    }
  }

  // --- Mapped views --------------------------------------------------------------------------
  pthread_mutex_t gViewLock = PTHREAD_MUTEX_INITIALIZER;
  std::map<std::uintptr_t, std::size_t>& Views()
  {
    static auto* const views = new std::map<std::uintptr_t, std::size_t>();
    return *views;
  }

  template <class Char>
  DWORD CopyOutPath(const std::string& posix, Char* const buffer, const DWORD capacity)
  {
    const std::string windows = WindowsPath(posix);
    if constexpr (sizeof(Char) == 1) {
      return CopyOutString(windows, buffer, capacity);
    } else {
      return CopyOutString(WideFromUtf8(windows.c_str()), buffer, capacity);
    }
  }

  std::string TempDirectoryPosix()
  {
    const char* const tmp = std::getenv("TMPDIR");
    std::string directory = (tmp != nullptr && tmp[0] != '\0') ? tmp : "/tmp";
    if (directory.back() != '/') {
      directory += '/';
    }
    return directory;
  }

  std::string FullPathWindows(const char* const path)
  {
    return WindowsPath(AbsolutePosixPath(NativePath(path)));
  }

  // MSVC fopen mode -> POSIX fopen mode ("rt, ccs=UTF-8" -> "r").
  std::string PosixMode(const char* const mode)
  {
    std::string out;
    for (const char* p = mode; *p != '\0' && *p != ','; ++p) {
      switch (*p) {
        case 'r':
        case 'w':
        case 'a':
        case '+':
        case 'b':
        case 'x':
          out.push_back(*p);
          break;
        default:
          break; // t (text), c/n (commit), S/R (cache hints), T/D (temporary), N (no-inherit)
      }
    }
    return out;
  }

  // FAF_POSIX_TODO: text-mode streams ("r"/"w" without 'b') do no CRLF translation here;
  // on Windows they read "\r\n" as "\n" and write "\n" as "\r\n".
  FILE* OpenStream(const char* const path, const char* const mode, int* const error) noexcept
  {
    if (path == nullptr || mode == nullptr) {
      *error = EINVAL;
      return nullptr;
    }
    try {
      const std::string native = OpenPath(path);
      const std::string posixMode = PosixMode(mode);
      FILE* const stream = std::fopen(native.c_str(), posixMode.c_str());
      *error = stream == nullptr ? errno : 0;
      return stream;
    } catch (...) {
      *error = ENOMEM;
      return nullptr;
    }
  }

  template <class Char>
  Char* GetCwdImpl(Char* buffer, const int maxLength) noexcept
  {
    try {
      const std::string windows = WindowsPath(CurrentDirectoryPosix());
      if constexpr (sizeof(Char) == 1) {
        const std::size_t needed = windows.size() + 1;
        if (buffer == nullptr) {
          const std::size_t size = std::max<std::size_t>(needed, maxLength > 0 ? static_cast<std::size_t>(maxLength) : 0);
          buffer = static_cast<char*>(std::malloc(size));
          if (buffer == nullptr) {
            errno = ENOMEM;
            return nullptr;
          }
        } else if (maxLength <= 0 || needed > static_cast<std::size_t>(maxLength)) {
          errno = ERANGE;
          return nullptr;
        }
        std::memcpy(buffer, windows.c_str(), needed);
        return buffer;
      } else {
        const std::wstring wide = WideFromUtf8(windows.c_str());
        const std::size_t needed = wide.size() + 1;
        if (buffer == nullptr) {
          const std::size_t size = std::max<std::size_t>(needed, maxLength > 0 ? static_cast<std::size_t>(maxLength) : 0);
          buffer = static_cast<wchar_t*>(std::malloc(size * sizeof(wchar_t)));
          if (buffer == nullptr) {
            errno = ENOMEM;
            return nullptr;
          }
        } else if (maxLength <= 0 || needed > static_cast<std::size_t>(maxLength)) {
          errno = ERANGE;
          return nullptr;
        }
        std::wmemcpy(buffer, wide.c_str(), needed);
        return buffer;
      }
    } catch (...) {
      errno = ENOMEM;
      return nullptr;
    }
  }

  template <class Char>
  Char* FullPathCrt(Char* absolutePath, const Char* const relativePath, const std::size_t maxLength) noexcept
  {
    try {
      std::string input;
      if (relativePath != nullptr && relativePath[0] != 0) {
        if constexpr (sizeof(Char) == 1) {
          input = relativePath;
        } else {
          input = Utf8FromWide(relativePath);
        }
      } else {
        input = ".";
      }
      const std::string windows = FullPathWindows(input.c_str());
      if constexpr (sizeof(Char) == 1) {
        const std::size_t needed = windows.size() + 1;
        if (absolutePath == nullptr) {
          absolutePath = static_cast<char*>(std::malloc(std::max<std::size_t>(needed, 260)));
          if (absolutePath == nullptr) {
            errno = ENOMEM;
            return nullptr;
          }
        } else if (needed > maxLength) {
          errno = ERANGE;
          return nullptr;
        }
        std::memcpy(absolutePath, windows.c_str(), needed);
      } else {
        const std::wstring wide = WideFromUtf8(windows.c_str());
        const std::size_t needed = wide.size() + 1;
        if (absolutePath == nullptr) {
          absolutePath = static_cast<wchar_t*>(std::malloc(std::max<std::size_t>(needed, 260) * sizeof(wchar_t)));
          if (absolutePath == nullptr) {
            errno = ENOMEM;
            return nullptr;
          }
        } else if (needed > maxLength) {
          errno = ERANGE;
          return nullptr;
        }
        std::wmemcpy(absolutePath, wide.c_str(), needed);
      }
      return absolutePath;
    } catch (...) {
      errno = ENOMEM;
      return nullptr;
    }
  }

  int AccessMode(const int mode) noexcept
  {
    int posix = F_OK;
    if ((mode & 2) != 0) {
      posix |= W_OK;
    }
    if ((mode & 4) != 0) {
      posix |= R_OK;
    }
    return posix;
  }
} // namespace

// --- Path helpers (WinCompatInternal.h) ---------------------------------------------------------

namespace faf_posix
{
  std::string NativePath(const char* const path)
  {
    if (path == nullptr) {
      return std::string();
    }
    std::string native(path);
    std::replace(native.begin(), native.end(), '\\', '/');
    if (native.rfind("//?/", 0) == 0) {
      native.erase(0, 4);
    }
    if (native.size() >= 2 && IsDriveLetter(native[0]) && native[1] == ':') {
      // Every drive is the POSIX root; "C:x" is relative to the current directory.
      native.erase(0, 2);
      if (native.empty()) {
        native = ".";
      }
    }
    return native;
  }

  std::string NativePath(const wchar_t* const path)
  {
    if (path == nullptr) {
      return std::string();
    }
    return NativePath(Utf8FromWide(path).c_str());
  }

  std::string ResolvedPath(const char* const path)
  {
    return OpenPath(path);
  }

  std::string ResolvedPath(const wchar_t* const path)
  {
    if (path == nullptr) {
      return std::string();
    }
    return OpenPath(Utf8FromWide(path).c_str());
  }

  std::string WindowsPath(const std::string& posixPath)
  {
    std::string windows = posixPath;
    std::replace(windows.begin(), windows.end(), '/', '\\');
    if (!posixPath.empty() && posixPath[0] == '/' && (posixPath.size() < 2 || posixPath[1] != '/')) {
      windows.insert(0, "C:");
    }
    return windows;
  }

  std::string AbsolutePosixPath(const std::string& nativePath)
  {
    if (!nativePath.empty() && nativePath[0] == '/') {
      return NormalizeAbsolute(nativePath);
    }
    return NormalizeAbsolute(CurrentDirectoryPosix() + "/" + nativePath);
  }
} // namespace faf_posix

// --- Handles and I/O ------------------------------------------------------------------------

HANDLE CreateFileA(LPCSTR const path, const DWORD access, const DWORD share, LPSECURITY_ATTRIBUTES const attributes,
                   const DWORD disposition, const DWORD flags, const HANDLE templateFile) noexcept
{
  (void)share;
  (void)attributes;
  (void)templateFile;
  try {
    return CreateFileImpl(path, path != nullptr ? OpenPath(path) : std::string(), access, disposition, flags);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return INVALID_HANDLE_VALUE;
  }
}

HANDLE CreateFileW(LPCWSTR const path, const DWORD access, const DWORD share, LPSECURITY_ATTRIBUTES const attributes,
                   const DWORD disposition, const DWORD flags, const HANDLE templateFile) noexcept
{
  try {
    const std::string utf8 = path != nullptr ? Utf8FromWide(path) : std::string();
    return CreateFileA(path != nullptr ? utf8.c_str() : nullptr, access, share, attributes, disposition, flags,
                       templateFile);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return INVALID_HANDLE_VALUE;
  }
}

BOOL ReadFile(const HANDLE file, LPVOID const buffer, const DWORD bytesToRead, LPDWORD const bytesRead,
              LPOVERLAPPED const overlapped) noexcept
{
  if (bytesRead != nullptr && overlapped == nullptr) {
    *bytesRead = 0;
  }
  FileRef ref(file);
  FileObject* const object = ref.object;
  if (object == nullptr) {
    return FALSE;
  }
  if (bytesToRead != 0 && buffer == nullptr) {
    ::SetLastError(ERROR_NOACCESS);
    return FALSE;
  }
  struct stat info{};
  const bool regular = fstat(object->fd, &info) == 0 && S_ISREG(info.st_mode);
  auto* const out = static_cast<unsigned char*>(buffer);
  std::uint64_t offset = 0;
  if (overlapped != nullptr) {
    offset = (static_cast<std::uint64_t>(overlapped->OffsetHigh) << 32) | overlapped->Offset;
    if (overlapped->hEvent != nullptr) {
      (void)ResetEvent(overlapped->hEvent);
    }
  }
  std::size_t total = 0;
  while (total < bytesToRead) {
    const ssize_t n = overlapped != nullptr
                        ? ::pread(object->fd, out + total, bytesToRead - total, static_cast<off_t>(offset + total))
                        : ::read(object->fd, out + total, bytesToRead - total);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      SetLastErrorFromErrno(errno == EISDIR ? EACCES : errno);
      return FALSE;
    }
    if (n == 0) {
      break;
    }
    total += static_cast<std::size_t>(n);
    if (!regular) {
      break; // pipes and terminals return what is available
    }
  }
  if (overlapped != nullptr) {
    if (!object->overlapped) {
      (void)::lseek(object->fd, static_cast<off_t>(offset + total), SEEK_SET);
    }
    overlapped->Internal = 0;
    overlapped->InternalHigh = total;
    if (bytesRead != nullptr) {
      *bytesRead = static_cast<DWORD>(total);
    }
    if (overlapped->hEvent != nullptr) {
      (void)SetEvent(overlapped->hEvent);
    }
    if (total == 0 && bytesToRead != 0) {
      overlapped->Internal = kStatusEndOfFile;
      ::SetLastError(ERROR_HANDLE_EOF);
      return FALSE;
    }
    return TRUE;
  }
  if (bytesRead != nullptr) {
    *bytesRead = static_cast<DWORD>(total);
  }
  return TRUE;
}

BOOL WriteFile(const HANDLE file, LPCVOID const buffer, const DWORD bytesToWrite, LPDWORD const bytesWritten,
               LPOVERLAPPED const overlapped) noexcept
{
  if (bytesWritten != nullptr && overlapped == nullptr) {
    *bytesWritten = 0;
  }
  FileRef ref(file);
  FileObject* const object = ref.object;
  if (object == nullptr) {
    return FALSE;
  }
  if (bytesToWrite != 0 && buffer == nullptr) {
    ::SetLastError(ERROR_NOACCESS);
    return FALSE;
  }
  const auto* const in = static_cast<const unsigned char*>(buffer);
  bool positioned = overlapped != nullptr;
  std::uint64_t offset = 0;
  if (overlapped != nullptr) {
    if (overlapped->Offset == 0xFFFFFFFFu && overlapped->OffsetHigh == 0xFFFFFFFFu) {
      positioned = false; // append
      (void)::lseek(object->fd, 0, SEEK_END);
    } else {
      offset = (static_cast<std::uint64_t>(overlapped->OffsetHigh) << 32) | overlapped->Offset;
    }
    if (overlapped->hEvent != nullptr) {
      (void)ResetEvent(overlapped->hEvent);
    }
  }
  std::size_t total = 0;
  while (total < bytesToWrite) {
    const ssize_t n = positioned
                        ? ::pwrite(object->fd, in + total, bytesToWrite - total, static_cast<off_t>(offset + total))
                        : ::write(object->fd, in + total, bytesToWrite - total);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      SetLastErrorFromErrno(errno);
      return FALSE;
    }
    total += static_cast<std::size_t>(n);
  }
  if (overlapped != nullptr) {
    if (positioned && !object->overlapped) {
      (void)::lseek(object->fd, static_cast<off_t>(offset + total), SEEK_SET);
    }
    overlapped->Internal = 0;
    overlapped->InternalHigh = total;
    if (overlapped->hEvent != nullptr) {
      (void)SetEvent(overlapped->hEvent);
    }
  }
  if (bytesWritten != nullptr) {
    *bytesWritten = static_cast<DWORD>(total);
  }
  return TRUE;
}

DWORD SetFilePointer(const HANDLE file, const LONG distance, PLONG const distanceHigh, const DWORD moveMethod) noexcept
{
  FileRef ref(file);
  if (ref.object == nullptr) {
    return INVALID_SET_FILE_POINTER;
  }
  const int whence = moveMethod == FILE_BEGIN ? SEEK_SET : (moveMethod == FILE_CURRENT ? SEEK_CUR : SEEK_END);
  if (moveMethod > FILE_END) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return INVALID_SET_FILE_POINTER;
  }
  const std::int64_t move = distanceHigh != nullptr
                              ? static_cast<std::int64_t>((static_cast<std::uint64_t>(static_cast<std::uint32_t>(*distanceHigh)) << 32) |
                                                          static_cast<std::uint32_t>(distance))
                              : static_cast<std::int64_t>(distance);
  const off_t previous = ::lseek(ref.object->fd, 0, SEEK_CUR);
  const off_t result = ::lseek(ref.object->fd, static_cast<off_t>(move), whence);
  if (result < 0) {
    ::SetLastError(errno == EINVAL ? ERROR_NEGATIVE_SEEK : Win32ErrorFromErrno(errno));
    return INVALID_SET_FILE_POINTER;
  }
  if (distanceHigh == nullptr && static_cast<std::uint64_t>(result) > 0xFFFFFFFEull) {
    (void)::lseek(ref.object->fd, previous, SEEK_SET);
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return INVALID_SET_FILE_POINTER;
  }
  if (distanceHigh != nullptr) {
    *distanceHigh = static_cast<LONG>(static_cast<std::uint64_t>(result) >> 32);
  }
  ::SetLastError(NO_ERROR);
  return static_cast<DWORD>(static_cast<std::uint64_t>(result) & 0xFFFFFFFFu);
}

BOOL SetFilePointerEx(const HANDLE file, const LARGE_INTEGER distance, PLARGE_INTEGER const newPosition,
                      const DWORD moveMethod) noexcept
{
  FileRef ref(file);
  if (ref.object == nullptr) {
    return FALSE;
  }
  if (moveMethod > FILE_END) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  const int whence = moveMethod == FILE_BEGIN ? SEEK_SET : (moveMethod == FILE_CURRENT ? SEEK_CUR : SEEK_END);
  const off_t result = ::lseek(ref.object->fd, static_cast<off_t>(distance.QuadPart), whence);
  if (result < 0) {
    ::SetLastError(errno == EINVAL ? ERROR_NEGATIVE_SEEK : Win32ErrorFromErrno(errno));
    return FALSE;
  }
  if (newPosition != nullptr) {
    newPosition->QuadPart = static_cast<LONGLONG>(result);
  }
  return TRUE;
}

DWORD GetFileSize(const HANDLE file, LPDWORD const fileSizeHigh) noexcept
{
  FileRef ref(file);
  if (ref.object == nullptr) {
    return INVALID_FILE_SIZE;
  }
  struct stat info{};
  if (fstat(ref.object->fd, &info) != 0) {
    SetLastErrorFromErrno(errno);
    return INVALID_FILE_SIZE;
  }
  const std::uint64_t size = static_cast<std::uint64_t>(info.st_size);
  if (fileSizeHigh != nullptr) {
    *fileSizeHigh = static_cast<DWORD>(size >> 32);
  }
  ::SetLastError(NO_ERROR);
  return static_cast<DWORD>(size & 0xFFFFFFFFu);
}

BOOL GetFileSizeEx(const HANDLE file, PLARGE_INTEGER const fileSize) noexcept
{
  FileRef ref(file);
  if (ref.object == nullptr) {
    return FALSE;
  }
  struct stat info{};
  if (fstat(ref.object->fd, &info) != 0) {
    SetLastErrorFromErrno(errno);
    return FALSE;
  }
  if (fileSize != nullptr) {
    fileSize->QuadPart = static_cast<LONGLONG>(info.st_size);
  }
  return TRUE;
}

BOOL SetEndOfFile(const HANDLE file) noexcept
{
  FileRef ref(file);
  if (ref.object == nullptr) {
    return FALSE;
  }
  const off_t position = ::lseek(ref.object->fd, 0, SEEK_CUR);
  if (position < 0 || ::ftruncate(ref.object->fd, position) != 0) {
    SetLastErrorFromErrno(errno);
    return FALSE;
  }
  return TRUE;
}

BOOL FlushFileBuffers(const HANDLE file) noexcept
{
  FileRef ref(file);
  if (ref.object == nullptr) {
    return FALSE;
  }
  if (::fsync(ref.object->fd) != 0 && errno != EINVAL && errno != ENOTSUP) {
    SetLastErrorFromErrno(errno);
    return FALSE;
  }
  return TRUE;
}

DWORD GetFileType(const HANDLE file) noexcept
{
  FileRef ref(file);
  if (ref.object == nullptr) {
    return FILE_TYPE_UNKNOWN;
  }
  struct stat info{};
  if (fstat(ref.object->fd, &info) != 0) {
    SetLastErrorFromErrno(errno);
    return FILE_TYPE_UNKNOWN;
  }
  ::SetLastError(NO_ERROR);
  if (S_ISREG(info.st_mode) || S_ISDIR(info.st_mode)) {
    return FILE_TYPE_DISK;
  }
  if (S_ISCHR(info.st_mode)) {
    return FILE_TYPE_CHAR;
  }
  if (S_ISFIFO(info.st_mode) || S_ISSOCK(info.st_mode)) {
    return FILE_TYPE_PIPE;
  }
  return FILE_TYPE_UNKNOWN;
}

BOOL GetFileTime(const HANDLE file, LPFILETIME const creationTime, LPFILETIME const lastAccessTime,
                 LPFILETIME const lastWriteTime) noexcept
{
  FileRef ref(file);
  if (ref.object == nullptr) {
    return FALSE;
  }
  struct stat info{};
  if (fstat(ref.object->fd, &info) != 0) {
    SetLastErrorFromErrno(errno);
    return FALSE;
  }
  if (creationTime != nullptr) {
    *creationTime = FileTimeOf(info.st_birthtimespec);
  }
  if (lastAccessTime != nullptr) {
    *lastAccessTime = FileTimeOf(info.st_atimespec);
  }
  if (lastWriteTime != nullptr) {
    *lastWriteTime = FileTimeOf(info.st_mtimespec);
  }
  return TRUE;
}

BOOL SetFileTime(const HANDLE file, const FILETIME* const creationTime, const FILETIME* const lastAccessTime,
                 const FILETIME* const lastWriteTime) noexcept
{
  (void)creationTime; // birth time is not settable through POSIX
  FileRef ref(file);
  if (ref.object == nullptr) {
    return FALSE;
  }
  auto toTimespec = [](const FILETIME* const time) {
    timespec value{};
    if (time == nullptr || (time->dwLowDateTime == 0 && time->dwHighDateTime == 0)) {
      value.tv_nsec = UTIME_OMIT;
      return value;
    }
    const std::uint64_t ticks = FileTimeToU64(*time);
    const std::int64_t since = static_cast<std::int64_t>(ticks) - static_cast<std::int64_t>(kFileTimeUnixEpoch);
    value.tv_sec = static_cast<time_t>(since / 10000000);
    value.tv_nsec = static_cast<long>((since % 10000000) * 100);
    if (value.tv_nsec < 0) {
      value.tv_nsec += 1000000000L;
      value.tv_sec -= 1;
    }
    return value;
  };
  const timespec times[2] = {toTimespec(lastAccessTime), toTimespec(lastWriteTime)};
  if (::futimens(ref.object->fd, times) != 0) {
    SetLastErrorFromErrno(errno);
    return FALSE;
  }
  return TRUE;
}

BOOL GetFileInformationByHandle(const HANDLE file, LPBY_HANDLE_FILE_INFORMATION const information) noexcept
{
  FileRef ref(file);
  if (ref.object == nullptr) {
    return FALSE;
  }
  struct stat info{};
  if (information == nullptr || fstat(ref.object->fd, &info) != 0) {
    SetLastErrorFromErrno(information == nullptr ? EINVAL : errno);
    return FALSE;
  }
  information->dwFileAttributes = AttributesOf(info);
  information->ftCreationTime = FileTimeOf(info.st_birthtimespec);
  information->ftLastAccessTime = FileTimeOf(info.st_atimespec);
  information->ftLastWriteTime = FileTimeOf(info.st_mtimespec);
  information->dwVolumeSerialNumber = static_cast<DWORD>(info.st_dev);
  SplitSize(static_cast<std::uint64_t>(info.st_size), &information->nFileSizeHigh, &information->nFileSizeLow);
  information->nNumberOfLinks = static_cast<DWORD>(info.st_nlink);
  SplitSize(static_cast<std::uint64_t>(info.st_ino), &information->nFileIndexHigh, &information->nFileIndexLow);
  return TRUE;
}

HANDLE GetStdHandle(const DWORD stdHandle) noexcept
{
  const int index = StdIndex(stdHandle);
  if (index < 0) {
    ::SetLastError(ERROR_INVALID_HANDLE);
    return INVALID_HANDLE_VALUE;
  }
  pthread_mutex_lock(&gStdLock);
  if (gStdHandles[index] == nullptr && !gStdOverridden[index]) {
    auto* const object = new (std::nothrow) FileObject();
    if (object != nullptr) {
      object->fd = index;
      object->ownsFd = false;
      gStdHandles[index] = InsertHandle(object);
    }
  }
  const HANDLE handle = gStdHandles[index];
  pthread_mutex_unlock(&gStdLock);
  return handle;
}

BOOL SetStdHandle(const DWORD stdHandle, const HANDLE handle) noexcept
{
  const int index = StdIndex(stdHandle);
  if (index < 0) {
    ::SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  pthread_mutex_lock(&gStdLock);
  gStdHandles[index] = handle;
  gStdOverridden[index] = true;
  pthread_mutex_unlock(&gStdLock);
  return TRUE;
}

BOOL GetOverlappedResult(const HANDLE file, LPOVERLAPPED const overlapped, LPDWORD const bytesTransferred,
                         const BOOL wait) noexcept
{
  (void)file;
  if (overlapped == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (overlapped->Internal == kStatusPending) {
    if (wait != FALSE && overlapped->hEvent != nullptr) {
      (void)WaitForSingleObject(overlapped->hEvent, INFINITE);
    }
    if (overlapped->Internal == kStatusPending) {
      ::SetLastError(ERROR_IO_INCOMPLETE);
      return FALSE;
    }
  }
  if (bytesTransferred != nullptr) {
    *bytesTransferred = static_cast<DWORD>(overlapped->InternalHigh);
  }
  if (overlapped->Internal == kStatusEndOfFile) {
    ::SetLastError(ERROR_HANDLE_EOF);
    return FALSE;
  }
  if (overlapped->Internal != 0) {
    ::SetLastError(ERROR_OPERATION_ABORTED);
    return FALSE;
  }
  return TRUE;
}

BOOL CancelIo(const HANDLE file) noexcept
{
  FileRef ref(file);
  return ref.object != nullptr ? TRUE : FALSE; // all I/O completes synchronously: nothing is pending
}

BOOL CancelIoEx(const HANDLE file, LPOVERLAPPED const overlapped) noexcept
{
  (void)overlapped;
  FileRef ref(file);
  if (ref.object == nullptr) {
    return FALSE;
  }
  ::SetLastError(ERROR_NOT_FOUND);
  return FALSE;
}

BOOL ReadDirectoryChangesW(const HANDLE directory, LPVOID const buffer, const DWORD bufferLength,
                           const BOOL watchSubtree, const DWORD notifyFilter, LPDWORD const bytesReturned,
                           LPOVERLAPPED const overlapped,
                           LPOVERLAPPED_COMPLETION_ROUTINE const completionRoutine) noexcept
{
  (void)directory;
  (void)buffer;
  (void)bufferLength;
  (void)watchSubtree;
  (void)notifyFilter;
  (void)overlapped;
  (void)completionRoutine;
  FAF_POSIX_STUB("ReadDirectoryChangesW: directory watching not implemented (FSEvents/kqueue port pending)");
  if (bytesReturned != nullptr) {
    *bytesReturned = 0;
  }
  ::SetLastError(ERROR_NOT_SUPPORTED);
  return FALSE;
}

// --- Names and attributes -------------------------------------------------------------------------

DWORD GetFileAttributesA(LPCSTR const path) noexcept
{
  if (path == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return INVALID_FILE_ATTRIBUTES;
  }
  try {
    return AttributesImpl(OpenPath(path));
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return INVALID_FILE_ATTRIBUTES;
  }
}

DWORD GetFileAttributesW(LPCWSTR const path) noexcept
{
  if (path == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return INVALID_FILE_ATTRIBUTES;
  }
  try {
    return AttributesImpl(ResolvedPath(path));
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return INVALID_FILE_ATTRIBUTES;
  }
}

BOOL GetFileAttributesExA(LPCSTR const path, const GET_FILEEX_INFO_LEVELS level, LPVOID const information) noexcept
{
  if (path == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  try {
    return AttributesExImpl(OpenPath(path), level, information);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

BOOL GetFileAttributesExW(LPCWSTR const path, const GET_FILEEX_INFO_LEVELS level, LPVOID const information) noexcept
{
  if (path == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  try {
    return AttributesExImpl(ResolvedPath(path), level, information);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

BOOL SetFileAttributesA(LPCSTR const path, const DWORD attributes) noexcept
{
  try {
    return SetAttributesImpl(OpenPath(path), attributes);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

BOOL SetFileAttributesW(LPCWSTR const path, const DWORD attributes) noexcept
{
  try {
    return SetAttributesImpl(ResolvedPath(path), attributes);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

HANDLE FindFirstFileA(LPCSTR const pattern, LPWIN32_FIND_DATAA const data) noexcept
{
  try {
    return FindFirstImpl(NativePath(pattern), data);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return INVALID_HANDLE_VALUE;
  }
}

HANDLE FindFirstFileW(LPCWSTR const pattern, LPWIN32_FIND_DATAW const data) noexcept
{
  try {
    return FindFirstImpl(NativePath(pattern), data);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return INVALID_HANDLE_VALUE;
  }
}

BOOL FindNextFileA(const HANDLE find, LPWIN32_FIND_DATAA const data) noexcept
{
  return FindNextImpl(find, data);
}

BOOL FindNextFileW(const HANDLE find, LPWIN32_FIND_DATAW const data) noexcept
{
  return FindNextImpl(find, data);
}

BOOL FindClose(const HANDLE find) noexcept
{
  const ObjectKind kind = ObjectKind::Find;
  return CloseHandleSlot(find, &kind) ? TRUE : FALSE;
}

BOOL CreateDirectoryA(LPCSTR const path, LPSECURITY_ATTRIBUTES const attributes) noexcept
{
  (void)attributes;
  try {
    return CreateDirectoryImpl(OpenPath(path));
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

BOOL CreateDirectoryW(LPCWSTR const path, LPSECURITY_ATTRIBUTES const attributes) noexcept
{
  (void)attributes;
  try {
    return CreateDirectoryImpl(ResolvedPath(path));
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

BOOL RemoveDirectoryA(LPCSTR const path) noexcept
{
  try {
    return RemoveDirectoryImpl(OpenPath(path));
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

BOOL RemoveDirectoryW(LPCWSTR const path) noexcept
{
  try {
    return RemoveDirectoryImpl(ResolvedPath(path));
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

BOOL DeleteFileA(LPCSTR const path) noexcept
{
  try {
    return DeleteFileImpl(OpenPath(path));
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

BOOL DeleteFileW(LPCWSTR const path) noexcept
{
  try {
    return DeleteFileImpl(ResolvedPath(path));
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

BOOL MoveFileA(LPCSTR const existing, LPCSTR const target) noexcept
{
  try {
    return MoveFileImpl(OpenPath(existing), OpenPath(target), 0x80000000u /* plain MoveFile: copy allowed */);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

BOOL MoveFileW(LPCWSTR const existing, LPCWSTR const target) noexcept
{
  try {
    return MoveFileImpl(ResolvedPath(existing), ResolvedPath(target), 0x80000000u);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

BOOL MoveFileExA(LPCSTR const existing, LPCSTR const target, const DWORD flags) noexcept
{
  if (target == nullptr) {
    FAF_POSIX_STUB("MoveFileEx(DELAY_UNTIL_REBOOT): no reboot-time operations on POSIX");
    ::SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
  }
  try {
    return MoveFileImpl(OpenPath(existing), OpenPath(target), flags & ~0x80000000u);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

BOOL MoveFileExW(LPCWSTR const existing, LPCWSTR const target, const DWORD flags) noexcept
{
  if (target == nullptr) {
    FAF_POSIX_STUB("MoveFileEx(DELAY_UNTIL_REBOOT): no reboot-time operations on POSIX");
    ::SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
  }
  try {
    return MoveFileImpl(ResolvedPath(existing), ResolvedPath(target), flags & ~0x80000000u);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

BOOL CopyFileA(LPCSTR const existing, LPCSTR const target, const BOOL failIfExists) noexcept
{
  try {
    return CopyFileData(OpenPath(existing), OpenPath(target), failIfExists != FALSE) ? TRUE : FALSE;
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

BOOL CopyFileW(LPCWSTR const existing, LPCWSTR const target, const BOOL failIfExists) noexcept
{
  try {
    return CopyFileData(ResolvedPath(existing), ResolvedPath(target), failIfExists != FALSE) ? TRUE : FALSE;
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

BOOL ReplaceFileA(LPCSTR const replaced, LPCSTR const replacement, LPCSTR const backup, const DWORD flags,
                  LPVOID const exclude, LPVOID const reserved) noexcept
{
  (void)flags;
  (void)exclude;
  (void)reserved;
  try {
    const std::string backupPath = backup != nullptr ? OpenPath(backup) : std::string();
    return ReplaceFileImpl(OpenPath(replaced), OpenPath(replacement), backup != nullptr ? &backupPath : nullptr);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

BOOL ReplaceFileW(LPCWSTR const replaced, LPCWSTR const replacement, LPCWSTR const backup, const DWORD flags,
                  LPVOID const exclude, LPVOID const reserved) noexcept
{
  (void)flags;
  (void)exclude;
  (void)reserved;
  try {
    const std::string backupPath = backup != nullptr ? ResolvedPath(backup) : std::string();
    return ReplaceFileImpl(ResolvedPath(replaced), ResolvedPath(replacement), backup != nullptr ? &backupPath : nullptr);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

DWORD GetCurrentDirectoryA(const DWORD bufferLength, LPSTR const buffer) noexcept
{
  try {
    return CopyOutPath(CurrentDirectoryPosix(), buffer, bufferLength);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
}

DWORD GetCurrentDirectoryW(const DWORD bufferLength, LPWSTR const buffer) noexcept
{
  try {
    return CopyOutPath(CurrentDirectoryPosix(), buffer, bufferLength);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
}

BOOL SetCurrentDirectoryA(LPCSTR const path) noexcept
{
  try {
    const std::string native = OpenPath(path);
    if (::chdir(native.c_str()) != 0) {
      SetNotFoundError(native, errno);
      return FALSE;
    }
    return TRUE;
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

BOOL SetCurrentDirectoryW(LPCWSTR const path) noexcept
{
  try {
    const std::string utf8 = path != nullptr ? Utf8FromWide(path) : std::string();
    return SetCurrentDirectoryA(utf8.c_str());
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

namespace
{
  template <class Char>
  DWORD FullPathName(const Char* const path, const DWORD bufferLength, Char* const buffer, Char** const filePart)
  {
    if (path == nullptr || path[0] == 0) {
      ::SetLastError(ERROR_INVALID_NAME);
      return 0;
    }
    std::string input;
    if constexpr (sizeof(Char) == 1) {
      input = path;
    } else {
      input = Utf8FromWide(path);
    }
    const std::string windows = FullPathWindows(input.c_str());
    DWORD result = 0;
    if constexpr (sizeof(Char) == 1) {
      result = CopyOutString(windows, buffer, bufferLength);
    } else {
      result = CopyOutString(WideFromUtf8(windows.c_str()), buffer, bufferLength);
    }
    if (filePart != nullptr) {
      *filePart = nullptr;
      if (result != 0 && result < bufferLength) {
        Char* last = nullptr;
        for (Char* p = buffer; *p != 0; ++p) {
          if (*p == '\\') {
            last = p;
          }
        }
        if (last != nullptr && last[1] != 0) {
          *filePart = last + 1;
        }
      }
    }
    return result;
  }
} // namespace

DWORD GetFullPathNameA(LPCSTR const path, const DWORD bufferLength, LPSTR const buffer, LPSTR* const filePart) noexcept
{
  try {
    return FullPathName(path, bufferLength, buffer, filePart);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
}

DWORD GetFullPathNameW(LPCWSTR const path, const DWORD bufferLength, LPWSTR const buffer, LPWSTR* const filePart) noexcept
{
  try {
    return FullPathName(path, bufferLength, buffer, filePart);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
}

DWORD GetTempPathA(const DWORD bufferLength, LPSTR const buffer) noexcept
{
  try {
    return CopyOutPath(TempDirectoryPosix(), buffer, bufferLength);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
}

DWORD GetTempPathW(const DWORD bufferLength, LPWSTR const buffer) noexcept
{
  try {
    return CopyOutPath(TempDirectoryPosix(), buffer, bufferLength);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
}

namespace
{
  // GetTempFileName: "<dir>\<pre><hex>.TMP"; with unique == 0 the file is created.
  UINT TempFileNameImpl(const std::string& directory, const std::string& prefix, const UINT unique, std::string* const out)
  {
    std::string base = directory;
    if (!base.empty() && base.back() != '\\' && base.back() != '/') {
      base += '\\';
    }
    base += prefix.substr(0, 3);
    UINT number = unique;
    if (number != 0) {
      char hex[16];
      std::snprintf(hex, sizeof(hex), "%X.TMP", number & 0xFFFFu);
      *out = base + hex;
      return number & 0xFFFFu;
    }
    UINT start = static_cast<UINT>((static_cast<unsigned>(getpid()) ^ static_cast<unsigned>(::time(nullptr))) & 0xFFFFu);
    for (UINT i = 0; i < 0x10000u; ++i) {
      number = ((start + i) & 0xFFFFu);
      if (number == 0) {
        continue;
      }
      char hex[16];
      std::snprintf(hex, sizeof(hex), "%X.TMP", number);
      const std::string candidate = base + hex;
      const std::string native = OpenPath(candidate.c_str());
      const int fd = ::open(native.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0666);
      if (fd >= 0) {
        ::close(fd);
        *out = candidate;
        return number;
      }
      if (errno != EEXIST) {
        SetNotFoundError(native, errno);
        return 0;
      }
    }
    ::SetLastError(ERROR_FILE_EXISTS);
    return 0;
  }
} // namespace

UINT GetTempFileNameA(LPCSTR const directory, LPCSTR const prefix, const UINT unique, LPSTR const tempFileName) noexcept
{
  if (directory == nullptr || tempFileName == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  try {
    std::string out;
    const UINT result = TempFileNameImpl(directory, prefix != nullptr ? prefix : "", unique, &out);
    if (result != 0) {
      std::strncpy(tempFileName, out.c_str(), MAX_PATH - 1);
      tempFileName[MAX_PATH - 1] = '\0';
    }
    return result;
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
}

UINT GetTempFileNameW(LPCWSTR const directory, LPCWSTR const prefix, const UINT unique, LPWSTR const tempFileName) noexcept
{
  if (directory == nullptr || tempFileName == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  try {
    std::string out;
    const UINT result =
      TempFileNameImpl(Utf8FromWide(directory), prefix != nullptr ? Utf8FromWide(prefix) : std::string(), unique, &out);
    if (result != 0) {
      const std::wstring wide = WideFromUtf8(out.c_str());
      const std::size_t length = std::min<std::size_t>(wide.size(), MAX_PATH - 1);
      std::wmemcpy(tempFileName, wide.c_str(), length);
      tempFileName[length] = L'\0';
    }
    return result;
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
}

BOOL PathFileExistsA(LPCSTR const path) noexcept
{
  return (path != nullptr && GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) ? TRUE : FALSE;
}

BOOL PathFileExistsW(LPCWSTR const path) noexcept
{
  return (path != nullptr && GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) ? TRUE : FALSE;
}

namespace
{
  int CreateDirectoryTree(const std::string& native)
  {
    struct stat info{};
    if (stat(native.c_str(), &info) == 0) {
      return S_ISDIR(info.st_mode) ? ERROR_ALREADY_EXISTS : ERROR_FILE_EXISTS;
    }
    const std::string absolute = AbsolutePosixPath(native);
    std::string current;
    std::size_t start = 1;
    while (start <= absolute.size()) {
      const std::size_t end = absolute.find('/', start);
      current = absolute.substr(0, end);
      if (!current.empty() && ::mkdir(current.c_str(), 0777) != 0 && errno != EEXIST) {
        const int error = errno;
        return static_cast<int>(error == ENOENT || error == ENOTDIR ? ERROR_PATH_NOT_FOUND : Win32ErrorFromErrno(error));
      }
      if (end == std::string::npos) {
        break;
      }
      start = end + 1;
    }
    return ERROR_SUCCESS;
  }
} // namespace

int SHCreateDirectoryExA(const HWND window, LPCSTR const path, const SECURITY_ATTRIBUTES* const attributes) noexcept
{
  (void)window;
  (void)attributes;
  if (path == nullptr || path[0] == '\0') {
    return ERROR_BAD_PATHNAME;
  }
  try {
    const int result = CreateDirectoryTree(OpenPath(path));
    ::SetLastError(static_cast<DWORD>(result));
    return result;
  } catch (...) {
    return ERROR_NOT_ENOUGH_MEMORY;
  }
}

int SHCreateDirectoryExW(const HWND window, LPCWSTR const path, const SECURITY_ATTRIBUTES* const attributes) noexcept
{
  try {
    const std::string utf8 = path != nullptr ? Utf8FromWide(path) : std::string();
    return SHCreateDirectoryExA(window, path != nullptr ? utf8.c_str() : nullptr, attributes);
  } catch (...) {
    return ERROR_NOT_ENOUGH_MEMORY;
  }
}

// --- SHFileOperation ---------------------------------------------------------------------------

namespace
{
  int RemoveTree(const std::string& path)
  {
    struct stat info{};
    if (lstat(path.c_str(), &info) != 0) {
      return static_cast<int>(Win32ErrorFromErrno(errno));
    }
    if (S_ISDIR(info.st_mode)) {
      std::vector<std::string> children;
      if (DIR* const directory = opendir(path.c_str())) {
        while (const dirent* const entry = readdir(directory)) {
          if (std::strcmp(entry->d_name, ".") != 0 && std::strcmp(entry->d_name, "..") != 0) {
            children.push_back(entry->d_name);
          }
        }
        closedir(directory);
      }
      for (const std::string& child : children) {
        const int result = RemoveTree(path + "/" + child);
        if (result != 0) {
          return result;
        }
      }
      return ::rmdir(path.c_str()) == 0 ? 0 : static_cast<int>(Win32ErrorFromErrno(errno));
    }
    return ::unlink(path.c_str()) == 0 ? 0 : static_cast<int>(Win32ErrorFromErrno(errno));
  }

  // The Recycle Bin's counterpart: ~/.Trash, with a unique name.
  int MoveToTrash(const std::string& path)
  {
    const char* const home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') {
      return RemoveTree(path);
    }
    const std::string trash = std::string(home) + "/.Trash";
    (void)::mkdir(trash.c_str(), 0700);
    const std::size_t slash = path.find_last_of('/');
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    std::string target = trash + "/" + name;
    struct stat info{};
    for (int i = 2; lstat(target.c_str(), &info) == 0 && i < 10000; ++i) {
      target = trash + "/" + name + " " + std::to_string(i);
    }
    if (::rename(path.c_str(), target.c_str()) == 0) {
      return 0;
    }
    return RemoveTree(path); // another volume: no trash there
  }

  int DeleteOne(const std::string& native, const FILEOP_FLAGS flags)
  {
    const std::size_t slash = native.find_last_of('/');
    const std::string leaf = slash == std::string::npos ? native : native.substr(slash + 1);
    auto remove = [&](const std::string& path) {
      return (flags & FOF_ALLOWUNDO) != 0 ? MoveToTrash(path) : RemoveTree(path);
    };
    if (!HasWildcards(leaf)) {
      return remove(ResolveCase(native));
    }
    const std::string directory = slash == std::string::npos ? "." : (slash == 0 ? "/" : native.substr(0, slash));
    const std::string resolvedDirectory = ResolveCase(directory);
    std::vector<std::string> matches;
    if (DIR* const dir = opendir(resolvedDirectory.c_str())) {
      while (const dirent* const entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name != "." && name != ".." && DosMatch(name, leaf)) {
          matches.push_back(name);
        }
      }
      closedir(dir);
    } else {
      return ERROR_PATH_NOT_FOUND;
    }
    for (const std::string& name : matches) {
      const std::string path = resolvedDirectory + "/" + name;
      struct stat info{};
      if ((flags & FOF_FILESONLY) != 0 && lstat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode)) {
        continue;
      }
      const int result = remove(path);
      if (result != 0) {
        return result;
      }
    }
    return 0;
  }

  int FileOperationImpl(const UINT function, const std::vector<std::string>& from, const std::vector<std::string>& to,
                        const FILEOP_FLAGS flags)
  {
    if (from.empty()) {
      return ERROR_INVALID_PARAMETER;
    }
    for (std::size_t i = 0; i < from.size(); ++i) {
      const std::string source = NativePath(from[i].c_str());
      int result = 0;
      switch (function) {
        case FO_DELETE:
          result = DeleteOne(source, flags);
          break;
        case FO_MOVE:
        case FO_RENAME:
        case FO_COPY: {
          if (to.empty()) {
            return ERROR_INVALID_PARAMETER;
          }
          const std::string resolvedSource = ResolveCase(source);
          std::string target = ResolveCase(NativePath(to[(flags & FOF_MULTIDESTFILES) != 0 && i < to.size() ? i : 0].c_str()));
          struct stat info{};
          if (function != FO_RENAME && stat(target.c_str(), &info) == 0 && S_ISDIR(info.st_mode)) {
            const std::size_t slash = resolvedSource.find_last_of('/');
            target += "/" + (slash == std::string::npos ? resolvedSource : resolvedSource.substr(slash + 1));
          }
          if (function == FO_COPY) {
            result = copyfile(resolvedSource.c_str(), target.c_str(), nullptr, COPYFILE_ALL | COPYFILE_RECURSIVE) == 0
                       ? 0
                       : static_cast<int>(Win32ErrorFromErrno(errno));
          } else if (::rename(resolvedSource.c_str(), target.c_str()) != 0) {
            result = static_cast<int>(Win32ErrorFromErrno(errno));
          }
          break;
        }
        default:
          return ERROR_INVALID_PARAMETER;
      }
      if (result != 0) {
        return result;
      }
    }
    return 0;
  }

  std::vector<std::string> SplitMultiString(const char* list)
  {
    std::vector<std::string> out;
    while (list != nullptr && *list != '\0') {
      out.emplace_back(list);
      list += std::strlen(list) + 1;
    }
    return out;
  }

  std::vector<std::string> SplitMultiString(const wchar_t* list)
  {
    std::vector<std::string> out;
    while (list != nullptr && *list != L'\0') {
      out.push_back(Utf8FromWide(list));
      list += std::wcslen(list) + 1;
    }
    return out;
  }
} // namespace

int SHFileOperationA(LPSHFILEOPSTRUCTA const operation) noexcept
{
  if (operation == nullptr) {
    return ERROR_INVALID_PARAMETER;
  }
  try {
    operation->fAnyOperationsAborted = FALSE;
    return FileOperationImpl(operation->wFunc, SplitMultiString(operation->pFrom), SplitMultiString(operation->pTo),
                             operation->fFlags);
  } catch (...) {
    return ERROR_NOT_ENOUGH_MEMORY;
  }
}

int SHFileOperationW(LPSHFILEOPSTRUCTW const operation) noexcept
{
  if (operation == nullptr) {
    return ERROR_INVALID_PARAMETER;
  }
  try {
    operation->fAnyOperationsAborted = FALSE;
    return FileOperationImpl(operation->wFunc, SplitMultiString(operation->pFrom), SplitMultiString(operation->pTo),
                             operation->fFlags);
  } catch (...) {
    return ERROR_NOT_ENOUGH_MEMORY;
  }
}

// --- File mappings ----------------------------------------------------------------------------

namespace
{
  HANDLE CreateMappingImpl(const HANDLE file, const DWORD protect, const std::uint64_t maximumSize) noexcept
  {
    const DWORD access = protect & 0xFF;
    int fd = -1;
    std::uint64_t size = maximumSize;
    if (file == INVALID_HANDLE_VALUE) {
      // Pagefile-backed: an unlinked temporary file, so every view shares the pages.
      if (size == 0) {
        ::SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
      }
      std::string pattern = TempDirectoryPosix() + "faf-mapping-XXXXXX";
      std::vector<char> name(pattern.begin(), pattern.end());
      name.push_back('\0');
      fd = ::mkstemp(name.data());
      if (fd < 0) {
        SetLastErrorFromErrno(errno);
        return nullptr;
      }
      (void)::unlink(name.data());
      if (::ftruncate(fd, static_cast<off_t>(size)) != 0) {
        SetLastErrorFromErrno(errno);
        ::close(fd);
        return nullptr;
      }
    } else {
      FileRef ref(file);
      if (ref.object == nullptr) {
        return nullptr;
      }
      struct stat info{};
      if (fstat(ref.object->fd, &info) != 0) {
        SetLastErrorFromErrno(errno);
        return nullptr;
      }
      if (size == 0) {
        size = static_cast<std::uint64_t>(info.st_size);
        if (size == 0) {
          ::SetLastError(1006 /* ERROR_FILE_INVALID: empty file */);
          return nullptr;
        }
      } else if (size > static_cast<std::uint64_t>(info.st_size)) {
        const bool writable = access == PAGE_READWRITE || access == PAGE_EXECUTE_READWRITE;
        if (!writable || ::ftruncate(ref.object->fd, static_cast<off_t>(size)) != 0) {
          ::SetLastError(ERROR_ACCESS_DENIED);
          return nullptr;
        }
      }
      fd = ::dup(ref.object->fd);
      if (fd < 0) {
        SetLastErrorFromErrno(errno);
        return nullptr;
      }
    }
    auto* const mapping = new (std::nothrow) MappingObject();
    if (mapping == nullptr) {
      ::close(fd);
      ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return nullptr;
    }
    mapping->fd = fd;
    mapping->size = size;
    mapping->protect = access;
    const HANDLE handle = InsertHandle(mapping);
    if (handle != nullptr) {
      ::SetLastError(ERROR_SUCCESS);
    }
    return handle;
  }
} // namespace

HANDLE CreateFileMappingA(const HANDLE file, LPSECURITY_ATTRIBUTES const attributes, const DWORD protect,
                          const DWORD maximumSizeHigh, const DWORD maximumSizeLow, LPCSTR const name) noexcept
{
  (void)attributes;
  if (name != nullptr) {
    FAF_POSIX_STUB("CreateFileMapping: named mappings are not shared (no cross-process namespace)");
  }
  return CreateMappingImpl(file, protect, (static_cast<std::uint64_t>(maximumSizeHigh) << 32) | maximumSizeLow);
}

HANDLE CreateFileMappingW(const HANDLE file, LPSECURITY_ATTRIBUTES const attributes, const DWORD protect,
                          const DWORD maximumSizeHigh, const DWORD maximumSizeLow, LPCWSTR const name) noexcept
{
  (void)attributes;
  if (name != nullptr) {
    FAF_POSIX_STUB("CreateFileMapping: named mappings are not shared (no cross-process namespace)");
  }
  return CreateMappingImpl(file, protect, (static_cast<std::uint64_t>(maximumSizeHigh) << 32) | maximumSizeLow);
}

LPVOID MapViewOfFile(const HANDLE mapping, const DWORD access, const DWORD offsetHigh, const DWORD offsetLow,
                     const SIZE_T bytes) noexcept
{
  auto* const object = ReferenceHandleAs<MappingObject>(mapping, ObjectKind::FileMapping);
  if (object == nullptr) {
    return nullptr;
  }
  const std::uint64_t offset = (static_cast<std::uint64_t>(offsetHigh) << 32) | offsetLow;
  if (offset > object->size || (offset % 0x10000u) != 0) {
    object->Release();
    ::SetLastError(offset > object->size ? ERROR_ACCESS_DENIED : ERROR_MAPPED_ALIGNMENT);
    return nullptr;
  }
  const std::size_t length = bytes != 0 ? bytes : static_cast<std::size_t>(object->size - offset);
  int prot = PROT_READ;
  int flags = MAP_SHARED;
  if ((access & FILE_MAP_COPY) != 0) {
    prot |= PROT_WRITE;
    flags = MAP_PRIVATE;
  } else if ((access & FILE_MAP_WRITE) != 0) {
    prot |= PROT_WRITE;
  }
  if ((access & FILE_MAP_EXECUTE) != 0) {
    prot |= PROT_EXEC;
  }
  void* const view = ::mmap(nullptr, length, prot, flags, object->fd, static_cast<off_t>(offset));
  object->Release();
  if (view == MAP_FAILED) {
    SetLastErrorFromErrno(errno);
    return nullptr;
  }
  pthread_mutex_lock(&gViewLock);
  try {
    Views()[reinterpret_cast<std::uintptr_t>(view)] = length;
  } catch (...) {
    pthread_mutex_unlock(&gViewLock);
    ::munmap(view, length);
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return nullptr;
  }
  pthread_mutex_unlock(&gViewLock);
  return view;
}

BOOL UnmapViewOfFile(LPCVOID const baseAddress) noexcept
{
  pthread_mutex_lock(&gViewLock);
  auto& views = Views();
  const auto it = views.find(reinterpret_cast<std::uintptr_t>(baseAddress));
  if (it == views.end()) {
    pthread_mutex_unlock(&gViewLock);
    ::SetLastError(ERROR_INVALID_ADDRESS);
    return FALSE;
  }
  const std::size_t length = it->second;
  views.erase(it);
  pthread_mutex_unlock(&gViewLock);
  ::munmap(const_cast<void*>(baseAddress), length);
  return TRUE;
}

BOOL FlushViewOfFile(LPCVOID const baseAddress, const SIZE_T bytes) noexcept
{
  const std::uintptr_t page = static_cast<std::uintptr_t>(getpagesize());
  const auto start = reinterpret_cast<std::uintptr_t>(baseAddress) & ~(page - 1);
  std::size_t length = bytes;
  if (length == 0) {
    pthread_mutex_lock(&gViewLock);
    auto& views = Views();
    auto it = views.upper_bound(reinterpret_cast<std::uintptr_t>(baseAddress));
    if (it != views.begin()) {
      --it;
      length = it->first + it->second - reinterpret_cast<std::uintptr_t>(baseAddress);
    }
    pthread_mutex_unlock(&gViewLock);
  }
  length += reinterpret_cast<std::uintptr_t>(baseAddress) - start;
  if (::msync(reinterpret_cast<void*>(start), length, MS_SYNC) != 0) {
    SetLastErrorFromErrno(errno);
    return FALSE;
  }
  return TRUE;
}

// --- CRT files ------------------------------------------------------------------------------

std::intptr_t _findfirst64(const char* const pattern, struct __finddata64_t* const data) noexcept
{
  if (pattern == nullptr) {
    errno = EINVAL;
    return -1;
  }
  try {
    return CrtFindFirst(NativePath(pattern), data);
  } catch (...) {
    errno = ENOMEM;
    return -1;
  }
}

int _findnext64(const std::intptr_t handle, struct __finddata64_t* const data) noexcept
{
  return CrtFindNext(handle, data);
}

std::intptr_t _wfindfirst64(const wchar_t* const pattern, struct _wfinddata64_t* const data) noexcept
{
  if (pattern == nullptr) {
    errno = EINVAL;
    return -1;
  }
  try {
    return CrtFindFirst(NativePath(pattern), data);
  } catch (...) {
    errno = ENOMEM;
    return -1;
  }
}

int _wfindnext64(const std::intptr_t handle, struct _wfinddata64_t* const data) noexcept
{
  return CrtFindNext(handle, data);
}

int _findclose(const std::intptr_t handle) noexcept
{
  const ObjectKind kind = ObjectKind::Find;
  if (!CloseHandleSlot(reinterpret_cast<HANDLE>(handle), &kind)) {
    errno = ENOENT;
    return -1;
  }
  return 0;
}

errno_t fopen_s(FILE** const stream, const char* const path, const char* const mode) noexcept
{
  if (stream == nullptr) {
    return EINVAL;
  }
  int error = 0;
  *stream = OpenStream(path, mode, &error);
  return *stream != nullptr ? 0 : error;
}

errno_t _wfopen_s(FILE** const stream, const wchar_t* const path, const wchar_t* const mode) noexcept
{
  if (stream == nullptr) {
    return EINVAL;
  }
  *stream = nullptr;
  if (path == nullptr || mode == nullptr) {
    return EINVAL;
  }
  try {
    const std::string utf8Path = Utf8FromWide(path);
    const std::string utf8Mode = Utf8FromWide(mode);
    int error = 0;
    *stream = OpenStream(utf8Path.c_str(), utf8Mode.c_str(), &error);
    return *stream != nullptr ? 0 : error;
  } catch (...) {
    return ENOMEM;
  }
}

FILE* _wfopen(const wchar_t* const path, const wchar_t* const mode) noexcept
{
  FILE* stream = nullptr;
  const errno_t error = _wfopen_s(&stream, path, mode);
  if (error != 0) {
    errno = error;
  }
  return stream;
}

FILE* _fsopen(const char* const path, const char* const mode, const int shareFlag) noexcept
{
  (void)shareFlag;
  int error = 0;
  FILE* const stream = OpenStream(path, mode, &error);
  if (stream == nullptr) {
    errno = error;
  }
  return stream;
}

FILE* _wfsopen(const wchar_t* const path, const wchar_t* const mode, const int shareFlag) noexcept
{
  (void)shareFlag;
  return _wfopen(path, mode);
}

errno_t freopen_s(FILE** const stream, const char* const path, const char* const mode, FILE* const old) noexcept
{
  if (stream == nullptr || mode == nullptr || old == nullptr) {
    return EINVAL;
  }
  try {
    const std::string native = path != nullptr ? OpenPath(path) : std::string();
    *stream = std::freopen(path != nullptr ? native.c_str() : nullptr, PosixMode(mode).c_str(), old);
    return *stream != nullptr ? 0 : errno;
  } catch (...) {
    return ENOMEM;
  }
}

int _unlink(const char* const path) noexcept
{
  try {
    return ::unlink(OpenPath(path).c_str());
  } catch (...) {
    errno = ENOMEM;
    return -1;
  }
}

int _wremove(const wchar_t* const path) noexcept
{
  try {
    return std::remove(ResolvedPath(path).c_str());
  } catch (...) {
    errno = ENOMEM;
    return -1;
  }
}

int _wrename(const wchar_t* const oldPath, const wchar_t* const newPath) noexcept
{
  try {
    // The CRT's rename does not replace an existing target (MoveFile semantics).
    const std::string from = ResolvedPath(oldPath);
    const std::string to = ResolvedPath(newPath);
    struct stat info{};
    if (lstat(to.c_str(), &info) == 0) {
      errno = EACCES;
      return -1;
    }
    return std::rename(from.c_str(), to.c_str());
  } catch (...) {
    errno = ENOMEM;
    return -1;
  }
}

int _wunlink(const wchar_t* const path) noexcept
{
  try {
    return ::unlink(ResolvedPath(path).c_str());
  } catch (...) {
    errno = ENOMEM;
    return -1;
  }
}

long _lseek(const int fd, const long offset, const int origin) noexcept
{
  const off_t result = ::lseek(fd, static_cast<off_t>(offset), origin);
  if (result > 0x7FFFFFFF) {
    errno = EINVAL;
    return -1;
  }
  return static_cast<long>(result);
}

__int64 _lseeki64(const int fd, const __int64 offset, const int origin) noexcept
{
  return static_cast<__int64>(::lseek(fd, static_cast<off_t>(offset), origin));
}

long _tell(const int fd) noexcept
{
  return _lseek(fd, 0, SEEK_CUR);
}

__int64 _telli64(const int fd) noexcept
{
  return _lseeki64(fd, 0, SEEK_CUR);
}

long _filelength(const int fd) noexcept
{
  struct stat info{};
  if (fstat(fd, &info) != 0) {
    return -1;
  }
  if (info.st_size > 0x7FFFFFFF) {
    errno = EINVAL;
    return -1;
  }
  return static_cast<long>(info.st_size);
}

__int64 _filelengthi64(const int fd) noexcept
{
  struct stat info{};
  if (fstat(fd, &info) != 0) {
    return -1;
  }
  return static_cast<__int64>(info.st_size);
}

int _commit(const int fd) noexcept
{
  return ::fsync(fd);
}

int _chsize(const int fd, const long size) noexcept
{
  return ::ftruncate(fd, static_cast<off_t>(size));
}

errno_t _chsize_s(const int fd, const __int64 size) noexcept
{
  return ::ftruncate(fd, static_cast<off_t>(size)) == 0 ? 0 : errno;
}

int _fseeki64(FILE* const stream, const __int64 offset, const int origin) noexcept
{
  return ::fseeko(stream, static_cast<off_t>(offset), origin);
}

__int64 _ftelli64(FILE* const stream) noexcept
{
  return static_cast<__int64>(::ftello(stream));
}

int _mkdir(const char* const path) noexcept
{
  try {
    return ::mkdir(OpenPath(path).c_str(), 0777);
  } catch (...) {
    errno = ENOMEM;
    return -1;
  }
}

int _wmkdir(const wchar_t* const path) noexcept
{
  try {
    return ::mkdir(ResolvedPath(path).c_str(), 0777);
  } catch (...) {
    errno = ENOMEM;
    return -1;
  }
}

int _rmdir(const char* const path) noexcept
{
  try {
    return ::rmdir(OpenPath(path).c_str());
  } catch (...) {
    errno = ENOMEM;
    return -1;
  }
}

int _wrmdir(const wchar_t* const path) noexcept
{
  try {
    return ::rmdir(ResolvedPath(path).c_str());
  } catch (...) {
    errno = ENOMEM;
    return -1;
  }
}

int _chdir(const char* const path) noexcept
{
  try {
    return ::chdir(OpenPath(path).c_str());
  } catch (...) {
    errno = ENOMEM;
    return -1;
  }
}

int _wchdir(const wchar_t* const path) noexcept
{
  try {
    return ::chdir(ResolvedPath(path).c_str());
  } catch (...) {
    errno = ENOMEM;
    return -1;
  }
}

char* _getcwd(char* const buffer, const int maxLength) noexcept
{
  return GetCwdImpl(buffer, maxLength);
}

wchar_t* _wgetcwd(wchar_t* const buffer, const int maxLength) noexcept
{
  return GetCwdImpl(buffer, maxLength);
}

char* _getdcwd(const int drive, char* const buffer, const int maxLength) noexcept
{
  if (drive < 0 || drive > 26) {
    errno = EACCES;
    return nullptr;
  }
  return GetCwdImpl(buffer, maxLength);
}

wchar_t* _wgetdcwd(const int drive, wchar_t* const buffer, const int maxLength) noexcept
{
  if (drive < 0 || drive > 26) {
    errno = EACCES;
    return nullptr;
  }
  return GetCwdImpl(buffer, maxLength);
}

int _getdrive() noexcept
{
  return 3; // C:
}

int _chdrive(const int drive) noexcept
{
  if (drive < 1 || drive > 26) {
    errno = EACCES;
    return -1;
  }
  return 0;
}

char* _fullpath(char* const absolutePath, const char* const relativePath, const std::size_t maxLength) noexcept
{
  return FullPathCrt(absolutePath, relativePath, maxLength);
}

wchar_t* _wfullpath(wchar_t* const absolutePath, const wchar_t* const relativePath, const std::size_t maxLength) noexcept
{
  return FullPathCrt(absolutePath, relativePath, maxLength);
}

int _access(const char* const path, const int mode) noexcept
{
  try {
    return ::access(OpenPath(path).c_str(), AccessMode(mode));
  } catch (...) {
    errno = ENOMEM;
    return -1;
  }
}

int _waccess(const wchar_t* const path, const int mode) noexcept
{
  try {
    return ::access(ResolvedPath(path).c_str(), AccessMode(mode));
  } catch (...) {
    errno = ENOMEM;
    return -1;
  }
}

errno_t _access_s(const char* const path, const int mode) noexcept
{
  if (path == nullptr || (mode & ~6) != 0) {
    return EINVAL;
  }
  return _access(path, mode) == 0 ? 0 : errno;
}

errno_t _waccess_s(const wchar_t* const path, const int mode) noexcept
{
  if (path == nullptr || (mode & ~6) != 0) {
    return EINVAL;
  }
  return _waccess(path, mode) == 0 ? 0 : errno;
}

// --- POSIX extension: host paths for the C++ standard library (compat/Files.h) -------------------

namespace
{
  std::size_t CopyOutHostPath(const std::string& hostPath, char* const buffer, const std::size_t capacity) noexcept
  {
    if (buffer == nullptr || capacity <= hostPath.size()) {
      return hostPath.size() + 1u;
    }
    std::memcpy(buffer, hostPath.c_str(), hostPath.size() + 1u);
    return hostPath.size();
  }
} // namespace

std::size_t faf_posixHostPathA(const char* const windowsPath, char* const buffer, const std::size_t capacity) noexcept
{
  if (windowsPath == nullptr) {
    return 0;
  }
  try {
    return CopyOutHostPath(faf_posix::ResolvedPath(windowsPath), buffer, capacity);
  } catch (...) {
    return 0;
  }
}

std::size_t faf_posixHostPathW(const wchar_t* const windowsPath, char* const buffer, const std::size_t capacity) noexcept
{
  if (windowsPath == nullptr) {
    return 0;
  }
  try {
    return CopyOutHostPath(faf_posix::ResolvedPath(windowsPath), buffer, capacity);
  } catch (...) {
    return 0;
  }
}
