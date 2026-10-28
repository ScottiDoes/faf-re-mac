// Win32 on POSIX: Misc (declarations in compat/Misc.h).

#include "WinCompatInternal.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cxxabi.h>
#include <new>
#include <string>
#include <typeinfo>
#include <vector>

#include <crt_externs.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <pwd.h>
#include <signal.h>
#include <spawn.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

using namespace faf_posix;

// --- CRT process globals ----------------------------------------------------------------------

int __argc = 0;
char** __argv = nullptr;
wchar_t** __wargv = nullptr;

namespace
{
  pthread_mutex_t gArgumentsLock = PTHREAD_MUTEX_INITIALIZER;

  void BuildWideArguments() noexcept
  {
    if (__argv == nullptr || __wargv != nullptr) {
      return;
    }
    auto** const wide = static_cast<wchar_t**>(std::calloc(static_cast<std::size_t>(__argc) + 1, sizeof(wchar_t*)));
    if (wide == nullptr) {
      return;
    }
    // Allocation-free on operator new (this can run inside the engine allocator's start-up,
    // through GetCommandLineW).
    for (int i = 0; i < __argc; ++i) {
      const char* const text = __argv[i] != nullptr ? __argv[i] : "";
      const std::size_t length = DecodeUtf8Raw(text, nullptr, 0);
      wide[i] = static_cast<wchar_t*>(std::calloc(length + 1, sizeof(wchar_t)));
      if (wide[i] != nullptr) {
        (void)DecodeUtf8Raw(text, wide[i], length + 1);
      }
    }
    __wargv = wide;
  }

  // Darwin passes (argc, argv, envp) to image initializers.
  __attribute__((constructor)) void InitProcessArguments(int argc, char** argv, char**)
  {
    faf_posixInitArguments(argc, argv);
  }
} // namespace

extern "C" void faf_posixInitArguments(const int argc, char** const argv) noexcept
{
  pthread_mutex_lock(&gArgumentsLock);
  if (__argv == nullptr) {
    __argc = argc;
    __argv = argv;
    if (__argv == nullptr) {
      __argc = *_NSGetArgc();
      __argv = *_NSGetArgv();
    }
    BuildWideArguments();
  }
  pthread_mutex_unlock(&gArgumentsLock);
}

namespace
{
  void EnsureArguments() noexcept
  {
    if (__argv == nullptr) {
      faf_posixInitArguments(*_NSGetArgc(), *_NSGetArgv());
    }
  }

  // --- Environment -----------------------------------------------------------------------------
  pthread_mutex_t gEnvironmentLock = PTHREAD_MUTEX_INITIALIZER;

  std::string HomeDirectory()
  {
    if (const char* const home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
      return home;
    }
    if (const passwd* const user = getpwuid(getuid()); user != nullptr && user->pw_dir != nullptr) {
      return user->pw_dir;
    }
    return "/";
  }

  std::string UserName()
  {
    if (const char* const name = std::getenv("USER"); name != nullptr && name[0] != '\0') {
      return name;
    }
    if (const passwd* const user = getpwuid(getuid()); user != nullptr && user->pw_name != nullptr) {
      return user->pw_name;
    }
    return "user";
  }

  std::string HostName()
  {
    char buffer[256] = {};
    if (gethostname(buffer, sizeof(buffer) - 1) != 0) {
      return "localhost";
    }
    std::string name = buffer;
    const std::size_t dot = name.find('.');
    if (dot != std::string::npos) {
      name.erase(dot);
    }
    for (char& c : name) {
      if (c >= 'a' && c <= 'z') {
        c = static_cast<char>(c - 'a' + 'A');
      }
    }
    return name;
  }

  std::string TempDirectory()
  {
    const char* const tmp = std::getenv("TMPDIR");
    std::string directory = (tmp != nullptr && tmp[0] != '\0') ? tmp : "/tmp";
    while (directory.size() > 1 && directory.back() == '/') {
      directory.pop_back();
    }
    return directory;
  }

  // The Windows profile variables a POSIX environment does not have.
  bool DerivedVariable(const char* const name, std::string* const value)
  {
    if (::strcasecmp(name, "USERPROFILE") == 0) {
      *value = WindowsPath(HomeDirectory());
    } else if (::strcasecmp(name, "APPDATA") == 0 || ::strcasecmp(name, "LOCALAPPDATA") == 0) {
      *value = WindowsPath(HomeDirectory() + "/Library/Application Support");
    } else if (::strcasecmp(name, "PROGRAMDATA") == 0 || ::strcasecmp(name, "ALLUSERSPROFILE") == 0) {
      *value = WindowsPath("/Users/Shared");
    } else if (::strcasecmp(name, "TEMP") == 0 || ::strcasecmp(name, "TMP") == 0) {
      *value = WindowsPath(TempDirectory());
    } else if (::strcasecmp(name, "USERNAME") == 0) {
      *value = UserName();
    } else if (::strcasecmp(name, "COMPUTERNAME") == 0) {
      *value = HostName();
    } else if (::strcasecmp(name, "HOMEDRIVE") == 0 || ::strcasecmp(name, "SYSTEMDRIVE") == 0) {
      *value = "C:";
    } else if (::strcasecmp(name, "HOMEPATH") == 0) {
      *value = WindowsPath(HomeDirectory()).substr(2);
    } else {
      return false;
    }
    return true;
  }

  // Case-insensitive lookup in environ, then the derived Windows variables.
  bool LookupVariable(const char* const name, std::string* const value)
  {
    if (name == nullptr || name[0] == '\0') {
      return false;
    }
    pthread_mutex_lock(&gEnvironmentLock);
    bool found = false;
    if (const char* const exact = std::getenv(name)) {
      *value = exact;
      found = true;
    } else {
      const std::size_t length = std::strlen(name);
      for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
        if (::strncasecmp(*entry, name, length) == 0 && (*entry)[length] == '=') {
          *value = *entry + length + 1;
          found = true;
          break;
        }
      }
    }
    pthread_mutex_unlock(&gEnvironmentLock);
    return found || DerivedVariable(name, value);
  }

  std::string ExpandVariables(const std::string& source)
  {
    std::string out;
    std::size_t i = 0;
    while (i < source.size()) {
      if (source[i] == '%') {
        const std::size_t end = source.find('%', i + 1);
        if (end != std::string::npos && end > i + 1) {
          std::string value;
          if (LookupVariable(source.substr(i + 1, end - i - 1).c_str(), &value)) {
            out += value;
            i = end + 1;
            continue;
          }
        }
      }
      out.push_back(source[i++]);
    }
    return out;
  }

  // --- Command lines ---------------------------------------------------------------------------
  // Windows (CommandLineToArgvW / msvcrt) splitting rules.
  template <class Char>
  std::vector<std::basic_string<Char>> SplitCommandLine(const Char* const line)
  {
    std::vector<std::basic_string<Char>> arguments;
    if (line == nullptr) {
      return arguments;
    }
    const Char* p = line;
    // Program name: up to the closing quote, or to whitespace; no escapes.
    std::basic_string<Char> program;
    if (*p == '"') {
      ++p;
      while (*p != 0 && *p != '"') {
        program.push_back(*p++);
      }
      if (*p == '"') {
        ++p;
      }
    } else {
      while (*p != 0 && *p != ' ' && *p != '\t') {
        program.push_back(*p++);
      }
    }
    arguments.push_back(program);
    for (;;) {
      while (*p == ' ' || *p == '\t') {
        ++p;
      }
      if (*p == 0) {
        break;
      }
      std::basic_string<Char> argument;
      bool quoted = false;
      while (*p != 0 && (quoted || (*p != ' ' && *p != '\t'))) {
        if (*p == '\\') {
          std::size_t backslashes = 0;
          while (*p == '\\') {
            ++backslashes;
            ++p;
          }
          if (*p == '"') {
            argument.append(backslashes / 2, static_cast<Char>('\\'));
            if ((backslashes & 1) != 0) {
              argument.push_back('"');
              ++p;
            }
          } else {
            argument.append(backslashes, static_cast<Char>('\\'));
          }
          continue;
        }
        if (*p == '"') {
          if (quoted && p[1] == '"') {
            argument.push_back('"');
            p += 2;
            continue;
          }
          quoted = !quoted;
          ++p;
          continue;
        }
        argument.push_back(*p++);
      }
      arguments.push_back(argument);
    }
    return arguments;
  }

  // --- Processes ---------------------------------------------------------------------------------
  struct ProcessObject final : KernelObject
  {
    ProcessObject() noexcept : KernelObject(ObjectKind::Process) {}
    bool IsSignaled(const ThreadObject*) const noexcept override { return exited; }
    pid_t pid = 0;
    bool exited = false;        // wait lock
    DWORD exitCode = STILL_ACTIVE;
    bool terminated = false;
    DWORD terminateCode = 0;
  };

  void* ProcessWaiter(void* const argument)
  {
    auto* const process = static_cast<ProcessObject*>(argument);
    int status = 0;
    pid_t result = 0;
    do {
      result = waitpid(process->pid, &status, 0);
    } while (result < 0 && errno == EINTR);
    {
      WaitGuard guard;
      if (process->terminated) {
        process->exitCode = process->terminateCode;
      } else if (result > 0 && WIFEXITED(status)) {
        process->exitCode = static_cast<DWORD>(WEXITSTATUS(status));
      } else if (result > 0 && WIFSIGNALED(status)) {
        process->exitCode = 0xC0000000u | static_cast<DWORD>(WTERMSIG(status));
      } else {
        process->exitCode = 1;
      }
      process->exited = true;
      NotifyWaiters(process);
    }
    process->Release();
    return nullptr;
  }

  std::vector<std::string> EnvironmentFromBlock(const void* const block, const bool wide)
  {
    std::vector<std::string> entries;
    if (wide) {
      for (const wchar_t* p = static_cast<const wchar_t*>(block); *p != L'\0'; p += std::wcslen(p) + 1) {
        entries.push_back(Utf8FromWide(p));
      }
    } else {
      for (const char* p = static_cast<const char*>(block); *p != '\0'; p += std::strlen(p) + 1) {
        entries.emplace_back(p);
      }
    }
    return entries;
  }

  BOOL SpawnProcess(const std::string& applicationName, const std::string& commandLine, const DWORD creationFlags,
                    const void* const environment, const std::string* const currentDirectory,
                    const DWORD startupFlags, LPPROCESS_INFORMATION const information)
  {
    std::vector<std::string> arguments = SplitCommandLine(commandLine.c_str());
    if (arguments.empty() || (arguments.size() == 1 && arguments[0].empty())) {
      if (applicationName.empty()) {
        ::SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
      }
      arguments.assign(1, applicationName);
    }
    const std::string programSource = applicationName.empty() ? arguments[0] : applicationName;
    const std::string program = ResolvedPath(programSource.c_str());
    if ((startupFlags & STARTF_USESTDHANDLES) != 0) {
      FAF_POSIX_STUB("CreateProcess: STARTF_USESTDHANDLES redirection is not forwarded");
    }
    if ((creationFlags & CREATE_SUSPENDED) != 0) {
      FAF_POSIX_STUB("CreateProcess: CREATE_SUSPENDED is not supported (started running)");
    }

    std::vector<char*> argv;
    for (std::string& argument : arguments) {
      argv.push_back(argument.data());
    }
    argv.push_back(nullptr);
    std::vector<std::string> environmentEntries;
    std::vector<char*> envp;
    if (environment != nullptr) {
      environmentEntries = EnvironmentFromBlock(environment, (creationFlags & CREATE_UNICODE_ENVIRONMENT) != 0);
      for (std::string& entry : environmentEntries) {
        envp.push_back(entry.data());
      }
      envp.push_back(nullptr);
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    std::string directory;
    if (currentDirectory != nullptr && !currentDirectory->empty()) {
      directory = ResolvedPath(currentDirectory->c_str());
      posix_spawn_file_actions_addchdir_np(&actions, directory.c_str());
    }
    pid_t pid = 0;
    const bool searchPath = program.find('/') == std::string::npos;
    const int rc = searchPath ? posix_spawnp(&pid, program.c_str(), &actions, nullptr, argv.data(),
                                             environment != nullptr ? envp.data() : environ)
                              : posix_spawn(&pid, program.c_str(), &actions, nullptr, argv.data(),
                                            environment != nullptr ? envp.data() : environ);
    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0) {
      ::SetLastError(rc == ENOENT ? ERROR_FILE_NOT_FOUND : Win32ErrorFromErrno(rc));
      return FALSE;
    }

    auto* const process = new (std::nothrow) ProcessObject();
    if (process == nullptr) {
      ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return FALSE;
    }
    process->pid = pid;
    process->AddRef(); // the waiter thread's reference
    pthread_t waiter{};
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&waiter, &attributes, &ProcessWaiter, process) != 0) {
      process->Release();
    }
    pthread_attr_destroy(&attributes);
    process->AddRef(); // hThread refers to the process too
    const HANDLE processHandle = InsertHandle(process);
    const HANDLE threadHandle = InsertHandle(process);
    if (information != nullptr) {
      information->hProcess = processHandle;
      information->hThread = threadHandle;
      information->dwProcessId = static_cast<DWORD>(pid);
      information->dwThreadId = 0;
    } else {
      (void)CloseHandleSlot(processHandle);
      (void)CloseHandleSlot(threadHandle);
    }
    return TRUE;
  }

  // --- Known folders -----------------------------------------------------------------------------
  bool FolderPath(const int folder, std::string* const path)
  {
    const std::string home = HomeDirectory();
    switch (folder & ~CSIDL_FLAG_MASK) {
      case CSIDL_PERSONAL:
        *path = home + "/Documents";
        return true;
      case CSIDL_APPDATA:
      case CSIDL_LOCAL_APPDATA:
        *path = home + "/Library/Application Support";
        return true;
      case CSIDL_COMMON_APPDATA:
      case CSIDL_COMMON_DOCUMENTS:
        *path = "/Users/Shared";
        return true;
      case CSIDL_PROFILE:
        *path = home;
        return true;
      case CSIDL_DESKTOP:
      case CSIDL_DESKTOPDIRECTORY:
        *path = home + "/Desktop";
        return true;
      case CSIDL_MYPICTURES:
        *path = home + "/Pictures";
        return true;
      case CSIDL_MYMUSIC:
        *path = home + "/Music";
        return true;
      case CSIDL_MYVIDEO:
        *path = home + "/Movies";
        return true;
      case CSIDL_INTERNET_CACHE:
        *path = home + "/Library/Caches";
        return true;
      case CSIDL_FONTS:
        *path = "/System/Library/Fonts";
        return true;
      case CSIDL_PROGRAM_FILES:
        *path = "/Applications";
        return true;
      default:
        return false;
    }
  }

  HRESULT FolderPathImpl(const int folder, std::string* const windowsPath)
  {
    std::string path;
    if (!FolderPath(folder, &path)) {
      return E_INVALIDARG;
    }
    struct stat info{};
    if (stat(path.c_str(), &info) != 0) {
      if ((folder & CSIDL_FLAG_CREATE) != 0) {
        const std::string windows = WindowsPath(path);
        if (SHCreateDirectoryExA(nullptr, windows.c_str(), nullptr) != ERROR_SUCCESS) {
          return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
        }
      } else if ((folder & CSIDL_FLAG_DONT_VERIFY) == 0) {
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
      }
    }
    *windowsPath = WindowsPath(path);
    return S_OK;
  }

  bool OpenWithShell(const std::string& target)
  {
    std::string argument = target;
    if (target.find("://") == std::string::npos && target.rfind("mailto:", 0) != 0) {
      argument = ResolvedPath(target.c_str());
    }
    char program[] = "/usr/bin/open";
    std::vector<char*> argv{program, argument.data(), nullptr};
    pid_t pid = 0;
    if (posix_spawn(&pid, program, nullptr, nullptr, argv.data(), environ) != 0) {
      return false;
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
  }

  // --- Floating point ----------------------------------------------------------------------------
  thread_local unsigned int tPrecisionShadow = _PC_53;
  thread_local unsigned int tExceptionMaskShadow = _MCW_EM;

  std::uint64_t ReadFpcr() noexcept
  {
    std::uint64_t value = 0;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(value));
    return value;
  }

  void WriteFpcr(const std::uint64_t value) noexcept
  {
    __asm__ __volatile__("msr fpcr, %0" : : "r"(value));
  }

  std::uint64_t ReadFpsr() noexcept
  {
    std::uint64_t value = 0;
    __asm__ __volatile__("mrs %0, fpsr" : "=r"(value));
    return value;
  }

  void WriteFpsr(const std::uint64_t value) noexcept
  {
    __asm__ __volatile__("msr fpsr, %0" : : "r"(value));
  }

  unsigned int CurrentControlWord() noexcept
  {
    const std::uint64_t fpcr = ReadFpcr();
    static constexpr unsigned int kRound[4] = {_RC_NEAR, _RC_UP, _RC_DOWN, _RC_CHOP};
    unsigned int word = kRound[(fpcr >> 22) & 3u] | tPrecisionShadow | tExceptionMaskShadow;
    if ((fpcr & (1ull << 24)) != 0) {
      word |= _DN_FLUSH;
    }
    return word;
  }

  unsigned int StatusWord(const std::uint64_t fpsr) noexcept
  {
    unsigned int status = 0;
    if ((fpsr & (1u << 0)) != 0) {
      status |= _SW_INVALID;
    }
    if ((fpsr & (1u << 1)) != 0) {
      status |= _SW_ZERODIVIDE;
    }
    if ((fpsr & (1u << 2)) != 0) {
      status |= _SW_OVERFLOW;
    }
    if ((fpsr & (1u << 3)) != 0) {
      status |= _SW_UNDERFLOW;
    }
    if ((fpsr & (1u << 4)) != 0) {
      status |= _SW_INEXACT;
    }
    if ((fpsr & (1u << 7)) != 0) {
      status |= _SW_DENORMAL;
    }
    return status;
  }

  // --- CRT hooks ---------------------------------------------------------------------------------
  std::atomic<_purecall_handler> gPurecallHandler{nullptr};
  std::atomic<_CRT_REPORT_HOOK> gReportHook{nullptr};
  std::atomic<int> gCrtDbgFlag{_CRTDBG_ALLOC_MEM_DF};
  std::atomic<unsigned int> gAbortBehavior{_WRITE_ABORT_MSG | _CALL_REPORTFAULT};
  std::atomic<int> gErrorMode{_OUT_TO_DEFAULT};
  std::atomic<UINT> gSystemErrorMode{0};
  std::atomic<LPTOP_LEVEL_EXCEPTION_FILTER> gUnhandledFilter{nullptr};
  std::atomic<DWORD> gSymOptions{SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS};

  bool DebuggerAttached() noexcept
  {
    kinfo_proc info{};
    std::size_t size = sizeof(info);
    int name[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
    if (sysctl(name, 4, &info, &size, nullptr, 0) != 0) {
      return false;
    }
    return (info.kp_proc.p_flag & P_TRACED) != 0;
  }

  bool DebugOutputEnabled() noexcept
  {
    static const bool forced = [] {
      const char* const value = std::getenv("FAF_DEBUG_OUTPUT");
      return value != nullptr && value[0] == '1';
    }();
    return forced || DebuggerAttached();
  }

  std::string Demangle(const char* const name)
  {
    if (name == nullptr) {
      return std::string();
    }
    int status = 0;
    char* const demangled = abi::__cxa_demangle(name, nullptr, nullptr, &status);
    if (demangled == nullptr || status != 0) {
      std::free(demangled);
      return name;
    }
    std::string out = demangled;
    std::free(demangled);
    return out;
  }

  template <class Char>
  void FillSystemInfo(LPSYSTEM_INFO const info) noexcept
  {
    std::memset(info, 0, sizeof(*info));
    info->wProcessorArchitecture = PROCESSOR_ARCHITECTURE_ARM64;
    info->dwPageSize = static_cast<DWORD>(getpagesize());
    info->lpMinimumApplicationAddress = reinterpret_cast<LPVOID>(0x10000);
    info->lpMaximumApplicationAddress = reinterpret_cast<LPVOID>(0x00007FFFFFFEFFFFull);
    DWORD_PTR processMask = 0;
    DWORD_PTR systemMask = 0;
    (void)GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask);
    info->dwActiveProcessorMask = systemMask;
    info->dwNumberOfProcessors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    info->dwProcessorType = 0;
    info->dwAllocationGranularity = 0x10000;
    info->wProcessorLevel = 0;
    info->wProcessorRevision = 0;
  }

  template <class Info>
  BOOL FillVersion(Info* const info) noexcept
  {
    if (info == nullptr || info->dwOSVersionInfoSize < sizeof(OSVERSIONINFOA)) {
      ::SetLastError(ERROR_INSUFFICIENT_BUFFER);
      return FALSE;
    }
    info->dwMajorVersion = 10;
    info->dwMinorVersion = 0;
    info->dwBuildNumber = 19045;
    info->dwPlatformId = VER_PLATFORM_WIN32_NT;
    info->szCSDVersion[0] = 0;
    return TRUE;
  }
} // namespace

// --- Process ------------------------------------------------------------------------------------

void GetStartupInfoA(LPSTARTUPINFOA const info) noexcept
{
  if (info == nullptr) {
    return;
  }
  std::memset(info, 0, sizeof(*info));
  info->cb = sizeof(*info);
  info->wShowWindow = SW_SHOWDEFAULT;
}

void GetStartupInfoW(LPSTARTUPINFOW const info) noexcept
{
  if (info == nullptr) {
    return;
  }
  std::memset(info, 0, sizeof(*info));
  info->cb = sizeof(*info);
  info->wShowWindow = SW_SHOWDEFAULT;
}

BOOL CreateProcessA(LPCSTR const applicationName, LPSTR const commandLine, LPSECURITY_ATTRIBUTES const processAttributes,
                    LPSECURITY_ATTRIBUTES const threadAttributes, const BOOL inheritHandles, const DWORD creationFlags,
                    LPVOID const environment, LPCSTR const currentDirectory, LPSTARTUPINFOA const startupInfo,
                    LPPROCESS_INFORMATION const processInformation) noexcept
{
  (void)processAttributes;
  (void)threadAttributes;
  (void)inheritHandles;
  try {
    const std::string directory = currentDirectory != nullptr ? currentDirectory : "";
    return SpawnProcess(applicationName != nullptr ? applicationName : "", commandLine != nullptr ? commandLine : "",
                        creationFlags, environment, currentDirectory != nullptr ? &directory : nullptr,
                        startupInfo != nullptr ? startupInfo->dwFlags : 0, processInformation);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

BOOL CreateProcessW(LPCWSTR const applicationName, LPWSTR const commandLine, LPSECURITY_ATTRIBUTES const processAttributes,
                    LPSECURITY_ATTRIBUTES const threadAttributes, const BOOL inheritHandles, const DWORD creationFlags,
                    LPVOID const environment, LPCWSTR const currentDirectory, LPSTARTUPINFOW const startupInfo,
                    LPPROCESS_INFORMATION const processInformation) noexcept
{
  (void)processAttributes;
  (void)threadAttributes;
  (void)inheritHandles;
  try {
    const std::string directory = currentDirectory != nullptr ? Utf8FromWide(currentDirectory) : "";
    return SpawnProcess(applicationName != nullptr ? Utf8FromWide(applicationName) : "",
                        commandLine != nullptr ? Utf8FromWide(commandLine) : "", creationFlags, environment,
                        currentDirectory != nullptr ? &directory : nullptr,
                        startupInfo != nullptr ? startupInfo->dwFlags : 0, processInformation);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

BOOL GetExitCodeProcess(const HANDLE process, LPDWORD const exitCode) noexcept
{
  if (process == CurrentProcessPseudoHandle()) {
    if (exitCode != nullptr) {
      *exitCode = STILL_ACTIVE;
    }
    return TRUE;
  }
  auto* const object = ReferenceHandleAs<ProcessObject>(process, ObjectKind::Process);
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

BOOL TerminateProcess(const HANDLE process, const UINT exitCode) noexcept
{
  if (process == CurrentProcessPseudoHandle()) {
    std::fflush(nullptr);
    _exit(static_cast<int>(exitCode));
  }
  auto* const object = ReferenceHandleAs<ProcessObject>(process, ObjectKind::Process);
  if (object == nullptr) {
    return FALSE;
  }
  BOOL result = TRUE;
  {
    WaitGuard guard;
    if (object->exited) {
      result = FALSE;
    } else {
      object->terminated = true;
      object->terminateCode = exitCode;
    }
  }
  if (result != FALSE && kill(object->pid, SIGKILL) != 0) {
    result = FALSE;
  }
  object->Release();
  if (result == FALSE) {
    ::SetLastError(ERROR_ACCESS_DENIED);
  }
  return result;
}

void ExitProcess(const UINT exitCode) noexcept
{
  std::exit(static_cast<int>(exitCode));
}

// --- Environment and command line --------------------------------------------------------------

DWORD GetEnvironmentVariableA(LPCSTR const name, LPSTR const buffer, const DWORD size) noexcept
{
  // A variable that is in the environment is copied out without any allocation: the engine
  // allocator reads FAF_SMALLPOOL / FAF_HIGHMEM through here while it starts.
  if (name != nullptr && name[0] != '\0') {
    pthread_mutex_lock(&gEnvironmentLock);
    const char* value = std::getenv(name);
    if (value == nullptr) {
      const std::size_t length = std::strlen(name);
      for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
        if (::strncasecmp(*entry, name, length) == 0 && (*entry)[length] == '=') {
          value = *entry + length + 1;
          break;
        }
      }
    }
    if (value != nullptr) {
      const std::size_t length = std::strlen(value);
      DWORD result = 0;
      if (buffer == nullptr || length + 1 > size) {
        result = static_cast<DWORD>(length + 1);
      } else {
        std::memcpy(buffer, value, length + 1);
        result = static_cast<DWORD>(length);
      }
      pthread_mutex_unlock(&gEnvironmentLock);
      return result;
    }
    pthread_mutex_unlock(&gEnvironmentLock);
  }
  try {
    std::string value;
    if (!LookupVariable(name, &value)) {
      ::SetLastError(ERROR_ENVVAR_NOT_FOUND);
      return 0;
    }
    return CopyOutString(value, buffer, size);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
}

DWORD GetEnvironmentVariableW(LPCWSTR const name, LPWSTR const buffer, const DWORD size) noexcept
{
  try {
    std::string value;
    if (name == nullptr || !LookupVariable(Utf8FromWide(name).c_str(), &value)) {
      ::SetLastError(ERROR_ENVVAR_NOT_FOUND);
      return 0;
    }
    return CopyOutString(WideFromUtf8(value.c_str()), buffer, size);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
}

BOOL SetEnvironmentVariableA(LPCSTR const name, LPCSTR const value) noexcept
{
  if (name == nullptr || name[0] == '\0' || std::strchr(name, '=') != nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  pthread_mutex_lock(&gEnvironmentLock);
  const int rc = value != nullptr ? setenv(name, value, 1) : unsetenv(name);
  pthread_mutex_unlock(&gEnvironmentLock);
  if (rc != 0) {
    SetLastErrorFromErrno(errno);
    return FALSE;
  }
  return TRUE;
}

BOOL SetEnvironmentVariableW(LPCWSTR const name, LPCWSTR const value) noexcept
{
  try {
    const std::string utf8Name = name != nullptr ? Utf8FromWide(name) : std::string();
    const std::string utf8Value = value != nullptr ? Utf8FromWide(value) : std::string();
    return SetEnvironmentVariableA(name != nullptr ? utf8Name.c_str() : nullptr,
                                   value != nullptr ? utf8Value.c_str() : nullptr);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

LPSTR GetEnvironmentStringsA() noexcept
{
  pthread_mutex_lock(&gEnvironmentLock);
  std::size_t total = 1;
  for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
    total += std::strlen(*entry) + 1;
  }
  auto* const block = static_cast<char*>(std::malloc(total + 1));
  if (block != nullptr) {
    char* out = block;
    for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
      const std::size_t length = std::strlen(*entry) + 1;
      std::memcpy(out, *entry, length);
      out += length;
    }
    *out++ = '\0';
    *out = '\0';
  }
  pthread_mutex_unlock(&gEnvironmentLock);
  return block;
}

LPWSTR GetEnvironmentStringsW() noexcept
{
  try {
    std::wstring block;
    pthread_mutex_lock(&gEnvironmentLock);
    try {
      for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
        block += WideFromUtf8(*entry);
        block.push_back(L'\0');
      }
    } catch (...) {
      pthread_mutex_unlock(&gEnvironmentLock);
      throw;
    }
    pthread_mutex_unlock(&gEnvironmentLock);
    block.push_back(L'\0');
    auto* const out = static_cast<wchar_t*>(std::malloc((block.size() + 1) * sizeof(wchar_t)));
    if (out != nullptr) {
      std::wmemcpy(out, block.data(), block.size());
      out[block.size()] = L'\0';
    }
    return out;
  } catch (...) {
    return nullptr;
  }
}

BOOL FreeEnvironmentStringsA(LPSTR const block) noexcept
{
  std::free(block);
  return TRUE;
}

BOOL FreeEnvironmentStringsW(LPWSTR const block) noexcept
{
  std::free(block);
  return TRUE;
}

DWORD ExpandEnvironmentStringsA(LPCSTR const source, LPSTR const destination, const DWORD size) noexcept
{
  if (source == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  try {
    const std::string expanded = ExpandVariables(source);
    const DWORD needed = static_cast<DWORD>(expanded.size() + 1);
    if (destination != nullptr && size >= needed) {
      std::memcpy(destination, expanded.c_str(), needed);
    }
    return needed; // including the terminator, success or not
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
}

DWORD ExpandEnvironmentStringsW(LPCWSTR const source, LPWSTR const destination, const DWORD size) noexcept
{
  if (source == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  try {
    const std::wstring expanded = WideFromUtf8(ExpandVariables(Utf8FromWide(source)).c_str());
    const DWORD needed = static_cast<DWORD>(expanded.size() + 1);
    if (destination != nullptr && size >= needed) {
      std::wmemcpy(destination, expanded.c_str(), needed);
    }
    return needed;
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
}

namespace
{
  // Writes `argument` with Windows quoting to `out` (when not null); returns its length.
  // Allocation-free: the engine allocator reads the command line while it starts.
  std::size_t QuoteArgumentRaw(const char* const argument, char* const out) noexcept
  {
    const bool needsQuotes = argument[0] == '\0' || std::strpbrk(argument, " \t\n\v\"") != nullptr;
    std::size_t n = 0;
    auto put = [&](const char c) {
      if (out != nullptr) {
        out[n] = c;
      }
      ++n;
    };
    if (!needsQuotes) {
      for (const char* p = argument; *p != '\0'; ++p) {
        put(*p);
      }
      return n;
    }
    put('"');
    std::size_t backslashes = 0;
    for (const char* p = argument; *p != '\0'; ++p) {
      if (*p == '\\') {
        ++backslashes;
        continue;
      }
      const std::size_t escapes = *p == '"' ? backslashes * 2 + 1 : backslashes;
      for (std::size_t i = 0; i < escapes; ++i) {
        put('\\');
      }
      backslashes = 0;
      put(*p);
    }
    for (std::size_t i = 0; i < backslashes * 2; ++i) {
      put('\\');
    }
    put('"');
    return n;
  }

  char* BuildCommandLine() noexcept
  {
    EnsureArguments();
    std::size_t total = 1;
    for (int i = 0; i < __argc; ++i) {
      total += QuoteArgumentRaw(__argv[i] != nullptr ? __argv[i] : "", nullptr) + 1;
    }
    char* const line = static_cast<char*>(std::malloc(total));
    if (line == nullptr) {
      return nullptr;
    }
    std::size_t n = 0;
    for (int i = 0; i < __argc; ++i) {
      if (i != 0) {
        line[n++] = ' ';
      }
      n += QuoteArgumentRaw(__argv[i] != nullptr ? __argv[i] : "", line + n);
    }
    line[n] = '\0';
    return line;
  }
} // namespace

LPSTR GetCommandLineA() noexcept
{
  static char* const line = BuildCommandLine();
  static char empty[1] = {'\0'};
  return line != nullptr ? line : empty;
}

LPWSTR GetCommandLineW() noexcept
{
  static wchar_t* const line = [] {
    const char* const narrow = GetCommandLineA();
    const std::size_t length = DecodeUtf8Raw(narrow, nullptr, 0);
    auto* const wide = static_cast<wchar_t*>(std::malloc((length + 1) * sizeof(wchar_t)));
    if (wide != nullptr) {
      (void)DecodeUtf8Raw(narrow, wide, length + 1);
    }
    return wide;
  }();
  static wchar_t empty[1] = {L'\0'};
  return line != nullptr ? line : empty;
}

LPWSTR* CommandLineToArgvW(LPCWSTR const commandLine, int* const argumentCount) noexcept
{
  if (argumentCount == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return nullptr;
  }
  try {
    std::vector<std::wstring> arguments;
    if (commandLine == nullptr || commandLine[0] == L'\0') {
      EnsureArguments();
      arguments.push_back(__argc > 0 && __argv[0] != nullptr ? WideFromUtf8(__argv[0]) : std::wstring());
    } else {
      arguments = SplitCommandLine(commandLine);
    }
    std::size_t characters = 0;
    for (const std::wstring& argument : arguments) {
      characters += argument.size() + 1;
    }
    const std::size_t pointerBytes = (arguments.size() + 1) * sizeof(LPWSTR);
    auto* const block = static_cast<unsigned char*>(::LocalAlloc(LMEM_FIXED, pointerBytes + characters * sizeof(wchar_t)));
    if (block == nullptr) {
      ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return nullptr;
    }
    auto** const argv = reinterpret_cast<LPWSTR*>(block);
    auto* text = reinterpret_cast<wchar_t*>(block + pointerBytes);
    for (std::size_t i = 0; i < arguments.size(); ++i) {
      argv[i] = text;
      std::wmemcpy(text, arguments[i].c_str(), arguments[i].size() + 1);
      text += arguments[i].size() + 1;
    }
    argv[arguments.size()] = nullptr;
    *argumentCount = static_cast<int>(arguments.size());
    return argv;
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return nullptr;
  }
}

errno_t getenv_s(std::size_t* const requiredSize, char* const buffer, const std::size_t size, const char* const name) noexcept
{
  if (requiredSize == nullptr || name == nullptr || (buffer == nullptr && size != 0)) {
    return EINVAL;
  }
  try {
    std::string value;
    if (!LookupVariable(name, &value)) {
      *requiredSize = 0;
      if (buffer != nullptr && size != 0) {
        buffer[0] = '\0';
      }
      return 0;
    }
    *requiredSize = value.size() + 1;
    if (buffer == nullptr && size == 0) {
      return 0;
    }
    if (value.size() + 1 > size) {
      if (size != 0) {
        buffer[0] = '\0';
      }
      return ERANGE;
    }
    std::memcpy(buffer, value.c_str(), value.size() + 1);
    return 0;
  } catch (...) {
    return ENOMEM;
  }
}

errno_t _dupenv_s(char** const buffer, std::size_t* const length, const char* const name) noexcept
{
  if (buffer == nullptr || name == nullptr) {
    return EINVAL;
  }
  *buffer = nullptr;
  if (length != nullptr) {
    *length = 0;
  }
  try {
    std::string value;
    if (!LookupVariable(name, &value)) {
      return 0;
    }
    *buffer = static_cast<char*>(std::malloc(value.size() + 1));
    if (*buffer == nullptr) {
      return ENOMEM;
    }
    std::memcpy(*buffer, value.c_str(), value.size() + 1);
    if (length != nullptr) {
      *length = value.size() + 1;
    }
    return 0;
  } catch (...) {
    return ENOMEM;
  }
}

errno_t _wdupenv_s(wchar_t** const buffer, std::size_t* const length, const wchar_t* const name) noexcept
{
  if (buffer == nullptr || name == nullptr) {
    return EINVAL;
  }
  *buffer = nullptr;
  if (length != nullptr) {
    *length = 0;
  }
  try {
    std::string value;
    if (!LookupVariable(Utf8FromWide(name).c_str(), &value)) {
      return 0;
    }
    const std::wstring wide = WideFromUtf8(value.c_str());
    *buffer = static_cast<wchar_t*>(std::malloc((wide.size() + 1) * sizeof(wchar_t)));
    if (*buffer == nullptr) {
      return ENOMEM;
    }
    std::wmemcpy(*buffer, wide.c_str(), wide.size() + 1);
    if (length != nullptr) {
      *length = wide.size() + 1;
    }
    return 0;
  } catch (...) {
    return ENOMEM;
  }
}

wchar_t* _wgetenv(const wchar_t* const name) noexcept
{
  // The CRT returns a pointer into its wide environment copy; keep one string per thread.
  static thread_local std::wstring value;
  try {
    std::string narrow;
    if (name == nullptr || !LookupVariable(Utf8FromWide(name).c_str(), &narrow)) {
      return nullptr;
    }
    value = WideFromUtf8(narrow.c_str());
    return value.data();
  } catch (...) {
    return nullptr;
  }
}

int _putenv(const char* const assignment) noexcept
{
  if (assignment == nullptr) {
    errno = EINVAL;
    return -1;
  }
  const char* const equals = std::strchr(assignment, '=');
  if (equals == nullptr || equals == assignment) {
    errno = EINVAL;
    return -1;
  }
  try {
    const std::string name(assignment, equals);
    // "NAME=" removes the variable, as in the CRT.
    return SetEnvironmentVariableA(name.c_str(), equals[1] != '\0' ? equals + 1 : nullptr) != FALSE ? 0 : -1;
  } catch (...) {
    errno = ENOMEM;
    return -1;
  }
}

errno_t _putenv_s(const char* const name, const char* const value) noexcept
{
  if (name == nullptr || value == nullptr) {
    return EINVAL;
  }
  return SetEnvironmentVariableA(name, value[0] != '\0' ? value : nullptr) != FALSE ? 0 : EINVAL;
}

// --- Debugging ------------------------------------------------------------------------------------

void OutputDebugStringA(LPCSTR const text) noexcept
{
  if (text != nullptr && DebugOutputEnabled()) {
    std::fputs(text, stderr);
  }
}

void OutputDebugStringW(LPCWSTR const text) noexcept
{
  if (text == nullptr || !DebugOutputEnabled()) {
    return;
  }
  try {
    std::fputs(Utf8FromWide(text).c_str(), stderr);
  } catch (...) {
  }
}

BOOL IsDebuggerPresent() noexcept
{
  return DebuggerAttached() ? TRUE : FALSE;
}

void DebugBreak() noexcept
{
  __builtin_debugtrap();
}

// --- System information ---------------------------------------------------------------------------

void GetSystemInfo(LPSYSTEM_INFO const info) noexcept
{
  if (info != nullptr) {
    FillSystemInfo<char>(info);
  }
}

void GetNativeSystemInfo(LPSYSTEM_INFO const info) noexcept
{
  GetSystemInfo(info);
}

BOOL GetNumaHighestNodeNumber(PULONG const highestNode) noexcept
{
  if (highestNode == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  *highestNode = 0;
  return TRUE;
}

BOOL GetNumaProcessorNode(const UCHAR processor, PUCHAR const nodeNumber) noexcept
{
  if (nodeNumber == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (processor >= GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)) {
    *nodeNumber = 0xFF;
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  *nodeNumber = 0;
  return TRUE;
}

BOOL GetNumaNodeProcessorMask(const UCHAR node, PULONGLONG const processorMask) noexcept
{
  if (processorMask == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (node != 0) {
    *processorMask = 0;
    return TRUE;
  }
  DWORD_PTR processMask = 0;
  DWORD_PTR systemMask = 0;
  (void)GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask);
  *processorMask = systemMask;
  return TRUE;
}

BOOL GetVersionExA(LPOSVERSIONINFOA const info) noexcept
{
  return FillVersion(info);
}

BOOL GetVersionExW(LPOSVERSIONINFOW const info) noexcept
{
  if (info == nullptr || info->dwOSVersionInfoSize < sizeof(OSVERSIONINFOW)) {
    ::SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  info->dwMajorVersion = 10;
  info->dwMinorVersion = 0;
  info->dwBuildNumber = 19045;
  info->dwPlatformId = VER_PLATFORM_WIN32_NT;
  info->szCSDVersion[0] = L'\0';
  if (info->dwOSVersionInfoSize >= sizeof(OSVERSIONINFOEXW)) {
    auto* const ex = reinterpret_cast<LPOSVERSIONINFOEXW>(info);
    ex->wServicePackMajor = 0;
    ex->wServicePackMinor = 0;
    ex->wSuiteMask = 0;
    ex->wProductType = VER_NT_WORKSTATION;
    ex->wReserved = 0;
  }
  return TRUE;
}

DWORD GetVersion() noexcept
{
  return (19045u << 16) | (0u << 8) | 10u;
}

BOOL GetComputerNameA(LPSTR const buffer, LPDWORD const size) noexcept
{
  if (size == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  try {
    const std::string name = HostName();
    if (buffer == nullptr || name.size() + 1 > *size) {
      *size = static_cast<DWORD>(name.size() + 1);
      ::SetLastError(ERROR_BUFFER_OVERFLOW);
      return FALSE;
    }
    std::memcpy(buffer, name.c_str(), name.size() + 1);
    *size = static_cast<DWORD>(name.size());
    return TRUE;
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

BOOL GetComputerNameW(LPWSTR const buffer, LPDWORD const size) noexcept
{
  if (size == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  try {
    const std::wstring name = WideFromUtf8(HostName().c_str());
    if (buffer == nullptr || name.size() + 1 > *size) {
      *size = static_cast<DWORD>(name.size() + 1);
      ::SetLastError(ERROR_BUFFER_OVERFLOW);
      return FALSE;
    }
    std::wmemcpy(buffer, name.c_str(), name.size() + 1);
    *size = static_cast<DWORD>(name.size());
    return TRUE;
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

BOOL GetUserNameA(LPSTR const buffer, LPDWORD const size) noexcept
{
  if (size == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  try {
    const std::string name = UserName();
    if (buffer == nullptr || name.size() + 1 > *size) {
      *size = static_cast<DWORD>(name.size() + 1);
      ::SetLastError(ERROR_INSUFFICIENT_BUFFER);
      return FALSE;
    }
    std::memcpy(buffer, name.c_str(), name.size() + 1);
    *size = static_cast<DWORD>(name.size() + 1); // GetUserName counts the terminator
    return TRUE;
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

BOOL GetUserNameW(LPWSTR const buffer, LPDWORD const size) noexcept
{
  if (size == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  try {
    const std::wstring name = WideFromUtf8(UserName().c_str());
    if (buffer == nullptr || name.size() + 1 > *size) {
      *size = static_cast<DWORD>(name.size() + 1);
      ::SetLastError(ERROR_INSUFFICIENT_BUFFER);
      return FALSE;
    }
    std::wmemcpy(buffer, name.c_str(), name.size() + 1);
    *size = static_cast<DWORD>(name.size() + 1);
    return TRUE;
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

// --- Console --------------------------------------------------------------------------------------

BOOL GetConsoleMode(const HANDLE console, LPDWORD const mode) noexcept
{
  if (mode == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  // Only a terminal is a console.
  int fd = -1;
  if (console == GetStdHandle(STD_INPUT_HANDLE)) {
    fd = 0;
  } else if (console == GetStdHandle(STD_OUTPUT_HANDLE)) {
    fd = 1;
  } else if (console == GetStdHandle(STD_ERROR_HANDLE)) {
    fd = 2;
  }
  if (fd < 0 || isatty(fd) == 0) {
    ::SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  *mode = fd == 0 ? 0x0007u /* processed, line, echo input */ : 0x0003u /* processed, wrap output */;
  return TRUE;
}

BOOL SetConsoleMode(const HANDLE console, const DWORD mode) noexcept
{
  (void)mode;
  DWORD current = 0;
  return GetConsoleMode(console, &current);
}

UINT GetConsoleCP() noexcept
{
  return CP_UTF8;
}

UINT GetConsoleOutputCP() noexcept
{
  return CP_UTF8;
}

BOOL SetConsoleOutputCP(const UINT codePage) noexcept
{
  return codePage == CP_UTF8 ? TRUE : FALSE;
}

BOOL SetConsoleCtrlHandler(const PHANDLER_ROUTINE handler, const BOOL add) noexcept
{
  (void)handler;
  (void)add;
  FAF_POSIX_STUB("SetConsoleCtrlHandler: Ctrl+C is delivered as SIGINT, not to the handler");
  return TRUE;
}

BOOL AllocConsole() noexcept
{
  return isatty(1) != 0 ? FALSE : TRUE; // the process already has whatever terminal it has
}

BOOL FreeConsole() noexcept
{
  return TRUE;
}

BOOL AttachConsole(const DWORD processId) noexcept
{
  (void)processId;
  ::SetLastError(ERROR_INVALID_HANDLE);
  return FALSE;
}

HWND GetConsoleWindow() noexcept
{
  return nullptr;
}

BOOL SetConsoleTitleA(LPCSTR const title) noexcept
{
  (void)title;
  return TRUE;
}

BOOL SetConsoleTitleW(LPCWSTR const title) noexcept
{
  (void)title;
  return TRUE;
}

// --- Error mode -----------------------------------------------------------------------------------

UINT SetErrorMode(const UINT mode) noexcept
{
  return gSystemErrorMode.exchange(mode);
}

UINT GetErrorMode() noexcept
{
  return gSystemErrorMode.load();
}

// --- Registry ---------------------------------------------------------------------------------------

LSTATUS RegOpenKeyExA(const HKEY key, LPCSTR const subKey, const DWORD options, const REGSAM access,
                      PHKEY const result) noexcept
{
  (void)key;
  (void)subKey;
  (void)options;
  (void)access;
  FAF_POSIX_STUB("RegOpenKeyEx: no registry on POSIX (every key is missing)");
  if (result != nullptr) {
    *result = nullptr;
  }
  return ERROR_FILE_NOT_FOUND;
}

LSTATUS RegOpenKeyExW(const HKEY key, LPCWSTR const subKey, const DWORD options, const REGSAM access,
                      PHKEY const result) noexcept
{
  (void)subKey;
  return RegOpenKeyExA(key, nullptr, options, access, result);
}

LSTATUS RegCreateKeyExA(const HKEY key, LPCSTR const subKey, const DWORD reserved, LPSTR const keyClass,
                        const DWORD options, const REGSAM access, LPSECURITY_ATTRIBUTES const attributes,
                        PHKEY const result, LPDWORD const disposition) noexcept
{
  (void)key;
  (void)subKey;
  (void)reserved;
  (void)keyClass;
  (void)options;
  (void)access;
  (void)attributes;
  (void)disposition;
  FAF_POSIX_STUB("RegCreateKeyEx: no registry on POSIX");
  if (result != nullptr) {
    *result = nullptr;
  }
  return ERROR_ACCESS_DENIED;
}

LSTATUS RegCreateKeyExW(const HKEY key, LPCWSTR const subKey, const DWORD reserved, LPWSTR const keyClass,
                        const DWORD options, const REGSAM access, LPSECURITY_ATTRIBUTES const attributes,
                        PHKEY const result, LPDWORD const disposition) noexcept
{
  (void)subKey;
  (void)keyClass;
  return RegCreateKeyExA(key, nullptr, reserved, nullptr, options, access, attributes, result, disposition);
}

LSTATUS RegQueryValueExA(const HKEY key, LPCSTR const valueName, LPDWORD const reserved, LPDWORD const type,
                         LPBYTE const data, LPDWORD const dataSize) noexcept
{
  (void)key;
  (void)valueName;
  (void)reserved;
  (void)type;
  (void)data;
  (void)dataSize;
  return ERROR_INVALID_HANDLE; // no key can have been opened
}

LSTATUS RegQueryValueExW(const HKEY key, LPCWSTR const valueName, LPDWORD const reserved, LPDWORD const type,
                         LPBYTE const data, LPDWORD const dataSize) noexcept
{
  (void)valueName;
  return RegQueryValueExA(key, nullptr, reserved, type, data, dataSize);
}

LSTATUS RegSetValueExA(const HKEY key, LPCSTR const valueName, const DWORD reserved, const DWORD type,
                       const BYTE* const data, const DWORD dataSize) noexcept
{
  (void)key;
  (void)valueName;
  (void)reserved;
  (void)type;
  (void)data;
  (void)dataSize;
  return ERROR_INVALID_HANDLE;
}

LSTATUS RegSetValueExW(const HKEY key, LPCWSTR const valueName, const DWORD reserved, const DWORD type,
                       const BYTE* const data, const DWORD dataSize) noexcept
{
  (void)valueName;
  return RegSetValueExA(key, nullptr, reserved, type, data, dataSize);
}

LSTATUS RegDeleteValueA(const HKEY key, LPCSTR const valueName) noexcept
{
  (void)key;
  (void)valueName;
  return ERROR_INVALID_HANDLE;
}

LSTATUS RegDeleteValueW(const HKEY key, LPCWSTR const valueName) noexcept
{
  (void)key;
  (void)valueName;
  return ERROR_INVALID_HANDLE;
}

LSTATUS RegCloseKey(const HKEY key) noexcept
{
  return key == nullptr ? ERROR_INVALID_HANDLE : ERROR_SUCCESS;
}

// --- Shell ------------------------------------------------------------------------------------------

HRESULT SHGetFolderPathA(const HWND window, const int folder, const HANDLE token, const DWORD flags,
                         LPSTR const path) noexcept
{
  (void)window;
  (void)token;
  (void)flags;
  if (path == nullptr) {
    return E_INVALIDARG;
  }
  try {
    std::string windows;
    const HRESULT result = FolderPathImpl(folder, &windows);
    if (FAILED(result)) {
      path[0] = '\0';
      return result;
    }
    if (windows.size() + 1 > MAX_PATH) {
      path[0] = '\0';
      return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
    }
    std::memcpy(path, windows.c_str(), windows.size() + 1);
    return S_OK;
  } catch (...) {
    return E_OUTOFMEMORY;
  }
}

HRESULT SHGetFolderPathW(const HWND window, const int folder, const HANDLE token, const DWORD flags,
                         LPWSTR const path) noexcept
{
  (void)window;
  (void)token;
  (void)flags;
  if (path == nullptr) {
    return E_INVALIDARG;
  }
  try {
    std::string windows;
    const HRESULT result = FolderPathImpl(folder, &windows);
    if (FAILED(result)) {
      path[0] = L'\0';
      return result;
    }
    const std::wstring wide = WideFromUtf8(windows.c_str());
    if (wide.size() + 1 > MAX_PATH) {
      path[0] = L'\0';
      return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
    }
    std::wmemcpy(path, wide.c_str(), wide.size() + 1);
    return S_OK;
  } catch (...) {
    return E_OUTOFMEMORY;
  }
}

BOOL SHGetSpecialFolderPathA(const HWND window, LPSTR const path, const int folder, const BOOL create) noexcept
{
  return SUCCEEDED(SHGetFolderPathA(window, folder | (create != FALSE ? CSIDL_FLAG_CREATE : 0), nullptr, 0, path))
           ? TRUE
           : FALSE;
}

BOOL SHGetSpecialFolderPathW(const HWND window, LPWSTR const path, const int folder, const BOOL create) noexcept
{
  return SUCCEEDED(SHGetFolderPathW(window, folder | (create != FALSE ? CSIDL_FLAG_CREATE : 0), nullptr, 0, path))
           ? TRUE
           : FALSE;
}

HINSTANCE ShellExecuteA(const HWND window, LPCSTR const operation, LPCSTR const file, LPCSTR const parameters,
                        LPCSTR const directory, const INT showCommand) noexcept
{
  (void)window;
  (void)directory;
  (void)showCommand;
  if (file == nullptr) {
    return reinterpret_cast<HINSTANCE>(static_cast<std::intptr_t>(SE_ERR_FNF));
  }
  if (operation != nullptr && ::strcasecmp(operation, "open") != 0 && ::strcasecmp(operation, "explore") != 0) {
    FAF_POSIX_STUB("ShellExecute: only the \"open\" verb is supported");
    return reinterpret_cast<HINSTANCE>(static_cast<std::intptr_t>(SE_ERR_NOASSOC));
  }
  if (parameters != nullptr && parameters[0] != '\0') {
    FAF_POSIX_STUB("ShellExecute: parameters are not passed to the opened document");
  }
  try {
    return reinterpret_cast<HINSTANCE>(static_cast<std::intptr_t>(OpenWithShell(file) ? 42 : SE_ERR_FNF));
  } catch (...) {
    return reinterpret_cast<HINSTANCE>(static_cast<std::intptr_t>(8 /* SE_ERR_OOM */));
  }
}

HINSTANCE ShellExecuteW(const HWND window, LPCWSTR const operation, LPCWSTR const file, LPCWSTR const parameters,
                        LPCWSTR const directory, const INT showCommand) noexcept
{
  try {
    const std::string op = operation != nullptr ? Utf8FromWide(operation) : std::string();
    const std::string target = file != nullptr ? Utf8FromWide(file) : std::string();
    const std::string params = parameters != nullptr ? Utf8FromWide(parameters) : std::string();
    return ShellExecuteA(window, operation != nullptr ? op.c_str() : nullptr, file != nullptr ? target.c_str() : nullptr,
                         params.c_str(), nullptr, showCommand);
  } catch (...) {
    return reinterpret_cast<HINSTANCE>(static_cast<std::intptr_t>(8));
  }
}

BOOL ShellExecuteExW(SHELLEXECUTEINFOW* const info) noexcept
{
  if (info == nullptr || info->cbSize < sizeof(SHELLEXECUTEINFOW)) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  info->hProcess = nullptr;
  const HINSTANCE result =
    ShellExecuteW(info->hwnd, info->lpVerb, info->lpFile, info->lpParameters, info->lpDirectory, info->nShow);
  info->hInstApp = result;
  if (reinterpret_cast<std::intptr_t>(result) <= 32) {
    ::SetLastError(ERROR_FILE_NOT_FOUND);
    return FALSE;
  }
  return TRUE;
}

// --- Privileges ---------------------------------------------------------------------------------------

BOOL OpenProcessToken(const HANDLE process, const DWORD access, PHANDLE const token) noexcept
{
  (void)process;
  (void)access;
  FAF_POSIX_STUB("OpenProcessToken: no Windows security tokens on POSIX");
  if (token != nullptr) {
    *token = nullptr;
  }
  ::SetLastError(ERROR_ACCESS_DENIED);
  return FALSE;
}

BOOL LookupPrivilegeValueA(LPCSTR const systemName, LPCSTR const name, PLUID const luid) noexcept
{
  (void)systemName;
  (void)name;
  if (luid != nullptr) {
    luid->LowPart = 0;
    luid->HighPart = 0;
  }
  ::SetLastError(ERROR_NOT_FOUND);
  return FALSE;
}

BOOL LookupPrivilegeValueW(LPCWSTR const systemName, LPCWSTR const name, PLUID const luid) noexcept
{
  (void)systemName;
  (void)name;
  return LookupPrivilegeValueA(nullptr, nullptr, luid);
}

BOOL AdjustTokenPrivileges(const HANDLE token, const BOOL disableAll, PTOKEN_PRIVILEGES const newState,
                           const DWORD bufferLength, PTOKEN_PRIVILEGES const previousState,
                           PDWORD const returnLength) noexcept
{
  (void)token;
  (void)disableAll;
  (void)newState;
  (void)bufferLength;
  (void)previousState;
  (void)returnLength;
  ::SetLastError(ERROR_INVALID_HANDLE);
  return FALSE;
}

// --- Structured exceptions and contexts ---------------------------------------------------------------

LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(const LPTOP_LEVEL_EXCEPTION_FILTER filter) noexcept
{
  FAF_POSIX_STUB("SetUnhandledExceptionFilter: recorded, never called (signals are not translated)");
  return gUnhandledFilter.exchange(filter);
}

LONG UnhandledExceptionFilter(struct _EXCEPTION_POINTERS* const exceptionInfo) noexcept
{
  (void)exceptionInfo;
  return EXCEPTION_CONTINUE_SEARCH;
}

PVOID AddVectoredExceptionHandler(const ULONG first, const PVECTORED_EXCEPTION_HANDLER handler) noexcept
{
  (void)first;
  FAF_POSIX_STUB("AddVectoredExceptionHandler: recorded, never called (signals are not translated)");
  return reinterpret_cast<PVOID>(handler);
}

ULONG RemoveVectoredExceptionHandler(const PVOID handle) noexcept
{
  return handle != nullptr ? 1u : 0u;
}

PVOID AddVectoredContinueHandler(const ULONG first, const PVECTORED_EXCEPTION_HANDLER handler) noexcept
{
  return AddVectoredExceptionHandler(first, handler);
}

ULONG RemoveVectoredContinueHandler(const PVOID handle) noexcept
{
  return RemoveVectoredExceptionHandler(handle);
}

void RaiseException(const DWORD code, const DWORD flags, const DWORD argumentCount,
                    const ULONG_PTR* const arguments) noexcept
{
  (void)flags;
  constexpr DWORD kSetThreadNameException = 0x406D1388;
  if (code == kSetThreadNameException) {
    // THREADNAME_INFO: { DWORD type (0x1000); LPCSTR name; DWORD threadId; DWORD flags }.
    if (argumentCount >= 3 && arguments != nullptr) {
      const auto* const name = reinterpret_cast<const char*>(arguments[1]);
      const DWORD threadId = static_cast<DWORD>(arguments[2]);
      if (name != nullptr && (threadId == 0xFFFFFFFFu || threadId == GetCurrentThreadId())) {
        char shortName[64];
        std::strncpy(shortName, name, sizeof(shortName) - 1);
        shortName[sizeof(shortName) - 1] = '\0';
        (void)pthread_setname_np(shortName);
      }
    }
    return;
  }
  std::fprintf(stderr, "[faf-posix] unhandled exception 0x%08X raised; aborting\n", code);
  std::abort();
}

void RtlCaptureContext(PCONTEXT const context) noexcept
{
  if (context != nullptr) {
    std::memset(context, 0, sizeof(*context));
    context->Rip = reinterpret_cast<DWORD64>(__builtin_return_address(0));
    context->Rsp = reinterpret_cast<DWORD64>(__builtin_frame_address(0));
    context->Rbp = context->Rsp;
  }
}

WORD RtlCaptureStackBackTrace(const DWORD framesToSkip, const DWORD framesToCapture, PVOID* const backTrace,
                              PDWORD const backTraceHash) noexcept
{
  if (backTrace == nullptr || framesToCapture == 0) {
    return 0;
  }
  void* frames[128];
  const int captured = backtrace(frames, 128);
  // Frame 0 is this function, as on Windows (the caller asks to skip it explicitly or not).
  const DWORD available = captured > 1 ? static_cast<DWORD>(captured - 1) : 0;
  DWORD count = 0;
  for (DWORD i = framesToSkip; i < available && count < framesToCapture && count < 0xFFFF; ++i) {
    backTrace[count++] = frames[i + 1];
  }
  if (backTraceHash != nullptr) {
    DWORD hash = 0;
    for (DWORD i = 0; i < count; ++i) {
      hash += static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(backTrace[i]));
    }
    *backTraceHash = hash;
  }
  return static_cast<WORD>(count);
}

BOOL GetThreadContext(const HANDLE thread, LPCONTEXT const context) noexcept
{
  (void)thread;
  FAF_POSIX_STUB("GetThreadContext: no x86 thread context on POSIX");
  if (context != nullptr) {
    const DWORD flags = context->ContextFlags;
    std::memset(context, 0, sizeof(*context));
    context->ContextFlags = flags;
  }
  ::SetLastError(ERROR_NOT_SUPPORTED);
  return FALSE;
}

BOOL SetThreadContext(const HANDLE thread, const CONTEXT* const context) noexcept
{
  (void)thread;
  (void)context;
  FAF_POSIX_STUB("SetThreadContext: no x86 thread context on POSIX");
  ::SetLastError(ERROR_NOT_SUPPORTED);
  return FALSE;
}

PVOID EncodePointer(const PVOID pointer) noexcept
{
  return pointer;
}

PVOID DecodePointer(const PVOID pointer) noexcept
{
  return pointer;
}

// --- dbghelp ----------------------------------------------------------------------------------------

BOOL SymInitialize(const HANDLE process, PCSTR const searchPath, const BOOL invadeProcess) noexcept
{
  (void)searchPath;
  (void)invadeProcess;
  return process == CurrentProcessPseudoHandle() ? TRUE : FALSE;
}

BOOL SymInitializeW(const HANDLE process, PCWSTR const searchPath, const BOOL invadeProcess) noexcept
{
  (void)searchPath;
  return SymInitialize(process, nullptr, invadeProcess);
}

BOOL SymCleanup(const HANDLE process) noexcept
{
  (void)process;
  return TRUE;
}

DWORD SymSetOptions(const DWORD options) noexcept
{
  gSymOptions.store(options);
  return options;
}

DWORD SymGetOptions() noexcept
{
  return gSymOptions.load();
}

BOOL SymFromAddr(const HANDLE process, const DWORD64 address, PDWORD64 const displacement,
                 PSYMBOL_INFO const symbol) noexcept
{
  (void)process;
  Dl_info info{};
  if (symbol == nullptr || dladdr(reinterpret_cast<const void*>(address), &info) == 0 || info.dli_sname == nullptr) {
    ::SetLastError(ERROR_MOD_NOT_FOUND);
    return FALSE;
  }
  try {
    const std::string name =
      (gSymOptions.load() & SYMOPT_UNDNAME) != 0 ? Demangle(info.dli_sname) : std::string(info.dli_sname);
    const std::size_t room = symbol->MaxNameLen > 0 ? symbol->MaxNameLen - 1 : 0;
    const std::size_t length = std::min(name.size(), room);
    std::memcpy(symbol->Name, name.data(), length);
    symbol->Name[length] = '\0';
    symbol->NameLen = static_cast<ULONG>(length);
    symbol->ModBase = reinterpret_cast<ULONG64>(info.dli_fbase);
    symbol->Address = reinterpret_cast<ULONG64>(info.dli_saddr);
    symbol->Size = 0;
    symbol->Flags = 0;
    if (displacement != nullptr) {
      *displacement = address - reinterpret_cast<DWORD64>(info.dli_saddr);
    }
    return TRUE;
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

BOOL SymGetSymFromAddr64(const HANDLE process, const DWORD64 address, PDWORD64 const displacement,
                         PIMAGEHLP_SYMBOL64 const symbol) noexcept
{
  (void)process;
  Dl_info info{};
  if (symbol == nullptr || dladdr(reinterpret_cast<const void*>(address), &info) == 0 || info.dli_sname == nullptr) {
    ::SetLastError(ERROR_MOD_NOT_FOUND);
    return FALSE;
  }
  try {
    const std::string name = Demangle(info.dli_sname);
    const std::size_t room = symbol->MaxNameLength > 0 ? symbol->MaxNameLength - 1 : 0;
    const std::size_t length = std::min(name.size(), room);
    std::memcpy(symbol->Name, name.data(), length);
    symbol->Name[length] = '\0';
    symbol->Address = reinterpret_cast<DWORD64>(info.dli_saddr);
    if (displacement != nullptr) {
      *displacement = address - reinterpret_cast<DWORD64>(info.dli_saddr);
    }
    return TRUE;
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

BOOL SymGetSymFromAddr64(const HANDLE process, const DWORD64 address, DWORD_PTR* const displacement,
                         PIMAGEHLP_SYMBOL64 const symbol) noexcept
{
  DWORD64 wide = 0;
  const BOOL result = SymGetSymFromAddr64(process, address, &wide, symbol);
  if (result != FALSE && displacement != nullptr) {
    *displacement = static_cast<DWORD_PTR>(wide);
  }
  return result;
}

BOOL SymGetLineFromAddr64(const HANDLE process, const DWORD64 address, PDWORD const displacement,
                          PIMAGEHLP_LINE64 const line) noexcept
{
  (void)process;
  (void)address;
  (void)displacement;
  (void)line;
  ::SetLastError(ERROR_NOT_SUPPORTED); // no line tables without dSYM parsing
  return FALSE;
}

DWORD64 SymGetModuleBase64(const HANDLE process, const DWORD64 address) noexcept
{
  (void)process;
  Dl_info info{};
  if (dladdr(reinterpret_cast<const void*>(address), &info) == 0) {
    return 0;
  }
  return reinterpret_cast<DWORD64>(info.dli_fbase);
}

PVOID SymFunctionTableAccess64(const HANDLE process, const DWORD64 base) noexcept
{
  (void)process;
  (void)base;
  return nullptr;
}

BOOL StackWalk64(const DWORD machineType, const HANDLE process, const HANDLE thread, LPSTACKFRAME64 const stackFrame,
                 PVOID const contextRecord, const PREAD_PROCESS_MEMORY_ROUTINE64 readMemory,
                 const PFUNCTION_TABLE_ACCESS_ROUTINE64 functionTableAccess,
                 const PGET_MODULE_BASE_ROUTINE64 getModuleBase,
                 const PTRANSLATE_ADDRESS_ROUTINE64 translateAddress) noexcept
{
  (void)machineType;
  (void)process;
  (void)thread;
  (void)stackFrame;
  (void)contextRecord;
  (void)readMemory;
  (void)functionTableAccess;
  (void)getModuleBase;
  (void)translateAddress;
  FAF_POSIX_STUB("StackWalk64: no x86 stack walking on POSIX (use RtlCaptureStackBackTrace)");
  return FALSE;
}

DWORD UnDecorateSymbolName(PCSTR const name, PSTR const outputString, const DWORD maxStringLength,
                           const DWORD flags) noexcept
{
  (void)flags;
  if (name == nullptr || outputString == nullptr || maxStringLength == 0) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  try {
    const std::string text = Demangle(name);
    const std::size_t length = std::min<std::size_t>(text.size(), maxStringLength - 1);
    std::memcpy(outputString, text.data(), length);
    outputString[length] = '\0';
    return static_cast<DWORD>(length);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
}

BOOL MiniDumpWriteDump(const HANDLE process, const DWORD processId, const HANDLE file, const MINIDUMP_TYPE dumpType,
                       PMINIDUMP_EXCEPTION_INFORMATION const exceptionParam, PVOID const userStreamParam,
                       PVOID const callbackParam) noexcept
{
  (void)process;
  (void)processId;
  (void)file;
  (void)dumpType;
  (void)exceptionParam;
  (void)userStreamParam;
  (void)callbackParam;
  FAF_POSIX_STUB("MiniDumpWriteDump: no minidumps on POSIX");
  ::SetLastError(ERROR_NOT_SUPPORTED);
  return FALSE;
}

// --- CRT floating point -------------------------------------------------------------------------------

unsigned int _controlfp(const unsigned int newValue, const unsigned int mask) noexcept
{
  const unsigned int effective = mask & ~static_cast<unsigned int>(_MCW_IC);
  if ((effective & (_MCW_RC | _MCW_DN)) != 0) {
    std::uint64_t fpcr = ReadFpcr();
    if ((effective & _MCW_RC) != 0) {
      std::uint64_t mode = 0;
      switch (newValue & _MCW_RC) {
        case _RC_UP:
          mode = 1;
          break;
        case _RC_DOWN:
          mode = 2;
          break;
        case _RC_CHOP:
          mode = 3;
          break;
        default:
          mode = 0;
          break;
      }
      fpcr = (fpcr & ~(3ull << 22)) | (mode << 22);
    }
    if ((effective & _MCW_DN) != 0) {
      fpcr = (newValue & _MCW_DN) == _DN_FLUSH ? (fpcr | (1ull << 24)) : (fpcr & ~(1ull << 24));
    }
    WriteFpcr(fpcr);
  }
  if ((effective & _MCW_PC) != 0) {
    tPrecisionShadow = newValue & _MCW_PC; // recorded only (no x87 on ARM)
  }
  if ((effective & _MCW_EM) != 0) {
    tExceptionMaskShadow = newValue & _MCW_EM; // ARM FP traps stay off: every exception masked
  }
  return CurrentControlWord();
}

errno_t _controlfp_s(unsigned int* const current, const unsigned int newValue, const unsigned int mask) noexcept
{
  const unsigned int word = mask != 0 ? _controlfp(newValue, mask) : CurrentControlWord();
  if (current != nullptr) {
    *current = word;
  }
  return 0;
}

unsigned int _control87(const unsigned int newValue, const unsigned int mask) noexcept
{
  return _controlfp(newValue, mask);
}

unsigned int _clearfp() noexcept
{
  const std::uint64_t fpsr = ReadFpsr();
  WriteFpsr(fpsr & ~std::uint64_t{0x9F});
  return StatusWord(fpsr);
}

unsigned int _statusfp() noexcept
{
  return StatusWord(ReadFpsr());
}

void _fpreset() noexcept
{
  WriteFpcr(ReadFpcr() & ~((3ull << 22) | (1ull << 24)));
  WriteFpsr(ReadFpsr() & ~std::uint64_t{0x9F});
  tPrecisionShadow = _PC_53;
  tExceptionMaskShadow = _MCW_EM;
}

// --- CRT debug and error hooks ---------------------------------------------------------------------

_CRT_REPORT_HOOK _CrtSetReportHook(const _CRT_REPORT_HOOK hook) noexcept
{
  return gReportHook.exchange(hook);
}

int _CrtSetDbgFlag(const int flags) noexcept
{
  if (flags == _CRTDBG_REPORT_FLAG) {
    return gCrtDbgFlag.load();
  }
  return gCrtDbgFlag.exchange(flags);
}

unsigned int _set_abort_behavior(const unsigned int flags, const unsigned int mask) noexcept
{
  unsigned int previous = gAbortBehavior.load();
  unsigned int next = 0;
  do {
    next = (previous & ~mask) | (flags & mask);
  } while (!gAbortBehavior.compare_exchange_weak(previous, next));
  return previous;
}

_purecall_handler _set_purecall_handler(const _purecall_handler handler) noexcept
{
  return gPurecallHandler.exchange(handler);
}

_purecall_handler _get_purecall_handler() noexcept
{
  return gPurecallHandler.load();
}

extern "C" int __cdecl _purecall()
{
  if (const _purecall_handler handler = gPurecallHandler.load()) {
    handler();
  }
  std::fprintf(stderr, "[faf-posix] pure virtual function call\n");
  std::abort();
}

int _callnewh(const std::size_t size) noexcept
{
  (void)size;
  const std::new_handler handler = std::get_new_handler();
  if (handler == nullptr) {
    return 0;
  }
  try {
    handler();
  } catch (...) {
    return 0;
  }
  return 1;
}

int _set_error_mode(const int mode) noexcept
{
  if (mode == _REPORT_ERRMODE) {
    return gErrorMode.load();
  }
  if (mode < _OUT_TO_DEFAULT || mode > _OUT_TO_MSGBOX) {
    errno = EINVAL;
    return -1;
  }
  return gErrorMode.exchange(mode);
}

// --- C++ runtime type information ---------------------------------------------------------------

void* faf_posixRTtypeid(void* const object)
{
  if (object == nullptr) {
    throw std::bad_typeid();
  }
  // Itanium C++ ABI 2.5.2: every virtual table in a group (the primary one and each
  // secondary one a base subobject points at) holds the offset to top at [-2] and the
  // complete object's typeinfo pointer at [-1].
  const void* const* const vtable = *static_cast<const void* const* const*>(object);
  return const_cast<void*>(vtable[-1]);
}
