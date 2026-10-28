#pragma once

// Win32 on POSIX: Misc. Included only by ../WinCompat.h (see its rules).
// Implementations: ../WinCompatMisc.cpp.
//
// Process, environment and command line; system and OS version information; the CRT's
// process globals (__argc/__argv), floating-point control and debug hooks; and the
// diagnostics surface (structured exceptions, CONTEXT, dbghelp, registry, shell) that a
// headless replay never needs: those report FAF_POSIX_STUB once and fail the Windows way.

#include <cstdio>

typedef std::uint64_t ULONG64;
typedef std::int64_t LONG64;
typedef DWORD64* PDWORD64;
typedef UCHAR* PUCHAR;
typedef ULONGLONG* PULONGLONG;
typedef CHAR* LPCH;

// --- Process -------------------------------------------------------------------------------------
typedef struct _STARTUPINFOA {
  DWORD cb;
  LPSTR lpReserved;
  LPSTR lpDesktop;
  LPSTR lpTitle;
  DWORD dwX;
  DWORD dwY;
  DWORD dwXSize;
  DWORD dwYSize;
  DWORD dwXCountChars;
  DWORD dwYCountChars;
  DWORD dwFillAttribute;
  DWORD dwFlags;
  WORD wShowWindow;
  WORD cbReserved2;
  LPBYTE lpReserved2;
  HANDLE hStdInput;
  HANDLE hStdOutput;
  HANDLE hStdError;
} STARTUPINFOA, *LPSTARTUPINFOA;
typedef struct _STARTUPINFOW {
  DWORD cb;
  LPWSTR lpReserved;
  LPWSTR lpDesktop;
  LPWSTR lpTitle;
  DWORD dwX;
  DWORD dwY;
  DWORD dwXSize;
  DWORD dwYSize;
  DWORD dwXCountChars;
  DWORD dwYCountChars;
  DWORD dwFillAttribute;
  DWORD dwFlags;
  WORD wShowWindow;
  WORD cbReserved2;
  LPBYTE lpReserved2;
  HANDLE hStdInput;
  HANDLE hStdOutput;
  HANDLE hStdError;
} STARTUPINFOW, *LPSTARTUPINFOW;
typedef struct _PROCESS_INFORMATION {
  HANDLE hProcess;
  HANDLE hThread;
  DWORD dwProcessId;
  DWORD dwThreadId;
} PROCESS_INFORMATION, *PPROCESS_INFORMATION, *LPPROCESS_INFORMATION;
#define STARTF_USESHOWWINDOW 0x00000001
#define STARTF_USESIZE 0x00000002
#define STARTF_USESTDHANDLES 0x00000100
#define DEBUG_PROCESS 0x00000001
#define CREATE_NEW_CONSOLE 0x00000010
#define CREATE_NEW_PROCESS_GROUP 0x00000200
#define CREATE_UNICODE_ENVIRONMENT 0x00000400
#define CREATE_NO_WINDOW 0x08000000
#define DETACHED_PROCESS 0x00000008
#define CREATE_DEFAULT_ERROR_MODE 0x04000000

void GetStartupInfoA(LPSTARTUPINFOA info) noexcept;
void GetStartupInfoW(LPSTARTUPINFOW info) noexcept;
// CreateProcess spawns the program with posix_spawn; the command line is split by the
// Windows (CommandLineToArgvW) rules and the program path converted like any path. The
// process handle is waitable (signalled on exit) and GetExitCodeProcess reports the code.
BOOL CreateProcessA(LPCSTR applicationName, LPSTR commandLine, LPSECURITY_ATTRIBUTES processAttributes,
                    LPSECURITY_ATTRIBUTES threadAttributes, BOOL inheritHandles, DWORD creationFlags, LPVOID environment,
                    LPCSTR currentDirectory, LPSTARTUPINFOA startupInfo, LPPROCESS_INFORMATION processInformation) noexcept;
BOOL CreateProcessW(LPCWSTR applicationName, LPWSTR commandLine, LPSECURITY_ATTRIBUTES processAttributes,
                    LPSECURITY_ATTRIBUTES threadAttributes, BOOL inheritHandles, DWORD creationFlags, LPVOID environment,
                    LPCWSTR currentDirectory, LPSTARTUPINFOW startupInfo,
                    LPPROCESS_INFORMATION processInformation) noexcept;
BOOL GetExitCodeProcess(HANDLE process, LPDWORD exitCode) noexcept;
BOOL TerminateProcess(HANDLE process, UINT exitCode) noexcept;
[[noreturn]] void ExitProcess(UINT exitCode) noexcept;

// --- Environment and command line ----------------------------------------------------------------
// Variable names match case-insensitively (Windows semantics). The Windows profile
// variables (USERPROFILE, APPDATA, LOCALAPPDATA, PROGRAMDATA, TEMP, TMP, USERNAME,
// COMPUTERNAME, HOMEDRIVE, HOMEPATH) are derived from the POSIX environment when not set.
DWORD GetEnvironmentVariableA(LPCSTR name, LPSTR buffer, DWORD size) noexcept;
DWORD GetEnvironmentVariableW(LPCWSTR name, LPWSTR buffer, DWORD size) noexcept;
BOOL SetEnvironmentVariableA(LPCSTR name, LPCSTR value) noexcept;
BOOL SetEnvironmentVariableW(LPCWSTR name, LPCWSTR value) noexcept;
LPSTR GetEnvironmentStringsA() noexcept;
LPWSTR GetEnvironmentStringsW() noexcept;
BOOL FreeEnvironmentStringsA(LPSTR block) noexcept;
BOOL FreeEnvironmentStringsW(LPWSTR block) noexcept;
DWORD ExpandEnvironmentStringsA(LPCSTR source, LPSTR destination, DWORD size) noexcept;
DWORD ExpandEnvironmentStringsW(LPCWSTR source, LPWSTR destination, DWORD size) noexcept;
// The command line is rebuilt from argv with Windows quoting.
LPSTR GetCommandLineA() noexcept;
LPWSTR GetCommandLineW() noexcept;
LPWSTR* CommandLineToArgvW(LPCWSTR commandLine, int* argumentCount) noexcept;

// CRT environment and process globals. __argc/__argv/__wargv are filled from the process
// arguments by a static initializer (or earlier through faf_posixInitArguments).
extern int __argc;
extern char** __argv;
extern wchar_t** __wargv;
extern "C" void faf_posixInitArguments(int argc, char** argv) noexcept;
errno_t getenv_s(std::size_t* requiredSize, char* buffer, std::size_t size, const char* name) noexcept;
errno_t _dupenv_s(char** buffer, std::size_t* length, const char* name) noexcept;
errno_t _wdupenv_s(wchar_t** buffer, std::size_t* length, const wchar_t* name) noexcept;
wchar_t* _wgetenv(const wchar_t* name) noexcept;
int _putenv(const char* assignment) noexcept;
errno_t _putenv_s(const char* name, const char* value) noexcept;
inline FILE* _popen(const char* command, const char* mode) noexcept { return ::popen(command, mode); }
inline int _pclose(FILE* stream) noexcept { return ::pclose(stream); }
inline int _getpid() noexcept { return static_cast<int>(::getpid()); }

// --- Debugging --------------------------------------------------------------------------------------
// OutputDebugString goes to stderr when a debugger is attached or FAF_DEBUG_OUTPUT=1 (on
// Windows nothing sees it without a debugger).
void OutputDebugStringA(LPCSTR text) noexcept;
void OutputDebugStringW(LPCWSTR text) noexcept;
BOOL IsDebuggerPresent() noexcept;
void DebugBreak() noexcept;

// --- System information --------------------------------------------------------------------------
#define PROCESSOR_ARCHITECTURE_INTEL 0
#define PROCESSOR_ARCHITECTURE_ARM 5
#define PROCESSOR_ARCHITECTURE_IA64 6
#define PROCESSOR_ARCHITECTURE_AMD64 9
#define PROCESSOR_ARCHITECTURE_ARM64 12
#define PROCESSOR_ARCHITECTURE_UNKNOWN 0xffff
typedef struct _SYSTEM_INFO {
  union {
    DWORD dwOemId;
    struct {
      WORD wProcessorArchitecture;
      WORD wReserved;
    };
  };
  DWORD dwPageSize;
  LPVOID lpMinimumApplicationAddress;
  LPVOID lpMaximumApplicationAddress;
  DWORD_PTR dwActiveProcessorMask;
  DWORD dwNumberOfProcessors;
  DWORD dwProcessorType;
  DWORD dwAllocationGranularity;
  WORD wProcessorLevel;
  WORD wProcessorRevision;
} SYSTEM_INFO, *LPSYSTEM_INFO;
// dwPageSize is the host page size (16 KB on Apple Silicon): VirtualAlloc/VirtualProtect
// work in host pages.
void GetSystemInfo(LPSYSTEM_INFO info) noexcept;
void GetNativeSystemInfo(LPSYSTEM_INFO info) noexcept;
BOOL GetNumaHighestNodeNumber(PULONG highestNode) noexcept;
BOOL GetNumaProcessorNode(UCHAR processor, PUCHAR nodeNumber) noexcept;
BOOL GetNumaNodeProcessorMask(UCHAR node, PULONGLONG processorMask) noexcept;

// The OS version reported is Windows 10 (10.0.19045, NT platform).
#define VER_PLATFORM_WIN32s 0
#define VER_PLATFORM_WIN32_WINDOWS 1
#define VER_PLATFORM_WIN32_NT 2
#define VER_NT_WORKSTATION 0x0000001
typedef struct _OSVERSIONINFOA {
  DWORD dwOSVersionInfoSize;
  DWORD dwMajorVersion;
  DWORD dwMinorVersion;
  DWORD dwBuildNumber;
  DWORD dwPlatformId;
  CHAR szCSDVersion[128];
} OSVERSIONINFOA, *POSVERSIONINFOA, *LPOSVERSIONINFOA;
typedef struct _OSVERSIONINFOW {
  DWORD dwOSVersionInfoSize;
  DWORD dwMajorVersion;
  DWORD dwMinorVersion;
  DWORD dwBuildNumber;
  DWORD dwPlatformId;
  WCHAR szCSDVersion[128];
} OSVERSIONINFOW, *POSVERSIONINFOW, *LPOSVERSIONINFOW, RTL_OSVERSIONINFOW, *PRTL_OSVERSIONINFOW;
typedef struct _OSVERSIONINFOEXA {
  DWORD dwOSVersionInfoSize;
  DWORD dwMajorVersion;
  DWORD dwMinorVersion;
  DWORD dwBuildNumber;
  DWORD dwPlatformId;
  CHAR szCSDVersion[128];
  WORD wServicePackMajor;
  WORD wServicePackMinor;
  WORD wSuiteMask;
  BYTE wProductType;
  BYTE wReserved;
} OSVERSIONINFOEXA, *POSVERSIONINFOEXA, *LPOSVERSIONINFOEXA;
typedef struct _OSVERSIONINFOEXW {
  DWORD dwOSVersionInfoSize;
  DWORD dwMajorVersion;
  DWORD dwMinorVersion;
  DWORD dwBuildNumber;
  DWORD dwPlatformId;
  WCHAR szCSDVersion[128];
  WORD wServicePackMajor;
  WORD wServicePackMinor;
  WORD wSuiteMask;
  BYTE wProductType;
  BYTE wReserved;
} OSVERSIONINFOEXW, *POSVERSIONINFOEXW, *LPOSVERSIONINFOEXW, RTL_OSVERSIONINFOEXW, *PRTL_OSVERSIONINFOEXW;
BOOL GetVersionExA(LPOSVERSIONINFOA info) noexcept;
BOOL GetVersionExW(LPOSVERSIONINFOW info) noexcept;
DWORD GetVersion() noexcept;
BOOL GetComputerNameA(LPSTR buffer, LPDWORD size) noexcept;
BOOL GetComputerNameW(LPWSTR buffer, LPDWORD size) noexcept;
BOOL GetUserNameA(LPSTR buffer, LPDWORD size) noexcept;
BOOL GetUserNameW(LPWSTR buffer, LPDWORD size) noexcept;

// --- Console --------------------------------------------------------------------------------------
typedef BOOL(WINAPI* PHANDLER_ROUTINE)(DWORD controlType);
#define CTRL_C_EVENT 0
#define CTRL_BREAK_EVENT 1
#define CTRL_CLOSE_EVENT 2
BOOL GetConsoleMode(HANDLE console, LPDWORD mode) noexcept;
BOOL SetConsoleMode(HANDLE console, DWORD mode) noexcept;
UINT GetConsoleCP() noexcept;
UINT GetConsoleOutputCP() noexcept;
BOOL SetConsoleOutputCP(UINT codePage) noexcept;
BOOL SetConsoleCtrlHandler(PHANDLER_ROUTINE handler, BOOL add) noexcept;
BOOL AllocConsole() noexcept;
BOOL FreeConsole() noexcept;
BOOL AttachConsole(DWORD processId) noexcept;
HWND GetConsoleWindow() noexcept;
BOOL SetConsoleTitleA(LPCSTR title) noexcept;
BOOL SetConsoleTitleW(LPCWSTR title) noexcept;

// --- Error mode ------------------------------------------------------------------------------------
#define SEM_FAILCRITICALERRORS 0x0001
#define SEM_NOGPFAULTERRORBOX 0x0002
#define SEM_NOALIGNMENTFAULTEXCEPT 0x0004
#define SEM_NOOPENFILEERRORBOX 0x8000
UINT SetErrorMode(UINT mode) noexcept;
UINT GetErrorMode() noexcept;

// --- Registry ---------------------------------------------------------------------------------------
// No registry on POSIX: keys do not exist (ERROR_FILE_NOT_FOUND) and cannot be created.
typedef LONG LSTATUS;
typedef DWORD REGSAM;
#define HKEY_CLASSES_ROOT (reinterpret_cast<HKEY>(static_cast<ULONG_PTR>(static_cast<LONG>(0x80000000))))
#define HKEY_CURRENT_USER (reinterpret_cast<HKEY>(static_cast<ULONG_PTR>(static_cast<LONG>(0x80000001))))
#define HKEY_LOCAL_MACHINE (reinterpret_cast<HKEY>(static_cast<ULONG_PTR>(static_cast<LONG>(0x80000002))))
#define HKEY_USERS (reinterpret_cast<HKEY>(static_cast<ULONG_PTR>(static_cast<LONG>(0x80000003))))
#define KEY_QUERY_VALUE 0x0001
#define KEY_SET_VALUE 0x0002
#define KEY_CREATE_SUB_KEY 0x0004
#define KEY_ENUMERATE_SUB_KEYS 0x0008
#define KEY_NOTIFY 0x0010
#define KEY_WOW64_64KEY 0x0100
#define KEY_WOW64_32KEY 0x0200
#define KEY_READ 0x20019
#define KEY_WRITE 0x20006
#define KEY_ALL_ACCESS 0xF003F
#define REG_NONE 0
#define REG_SZ 1
#define REG_EXPAND_SZ 2
#define REG_BINARY 3
#define REG_DWORD 4
#define REG_MULTI_SZ 7
#define REG_QWORD 11
#define REG_OPTION_NON_VOLATILE 0x00000000
#define REG_CREATED_NEW_KEY 0x00000001
#define REG_OPENED_EXISTING_KEY 0x00000002
LSTATUS RegOpenKeyExA(HKEY key, LPCSTR subKey, DWORD options, REGSAM access, PHKEY result) noexcept;
LSTATUS RegOpenKeyExW(HKEY key, LPCWSTR subKey, DWORD options, REGSAM access, PHKEY result) noexcept;
LSTATUS RegCreateKeyExA(HKEY key, LPCSTR subKey, DWORD reserved, LPSTR keyClass, DWORD options, REGSAM access,
                        LPSECURITY_ATTRIBUTES attributes, PHKEY result, LPDWORD disposition) noexcept;
LSTATUS RegCreateKeyExW(HKEY key, LPCWSTR subKey, DWORD reserved, LPWSTR keyClass, DWORD options, REGSAM access,
                        LPSECURITY_ATTRIBUTES attributes, PHKEY result, LPDWORD disposition) noexcept;
LSTATUS RegQueryValueExA(HKEY key, LPCSTR valueName, LPDWORD reserved, LPDWORD type, LPBYTE data,
                         LPDWORD dataSize) noexcept;
LSTATUS RegQueryValueExW(HKEY key, LPCWSTR valueName, LPDWORD reserved, LPDWORD type, LPBYTE data,
                         LPDWORD dataSize) noexcept;
LSTATUS RegSetValueExA(HKEY key, LPCSTR valueName, DWORD reserved, DWORD type, const BYTE* data,
                       DWORD dataSize) noexcept;
LSTATUS RegSetValueExW(HKEY key, LPCWSTR valueName, DWORD reserved, DWORD type, const BYTE* data,
                       DWORD dataSize) noexcept;
LSTATUS RegDeleteValueA(HKEY key, LPCSTR valueName) noexcept;
LSTATUS RegDeleteValueW(HKEY key, LPCWSTR valueName) noexcept;
LSTATUS RegCloseKey(HKEY key) noexcept;

// --- Shell -----------------------------------------------------------------------------------------
// Known folders map to the macOS user folders: CSIDL_PERSONAL ~/Documents, CSIDL_APPDATA and
// CSIDL_LOCAL_APPDATA ~/Library/Application Support, CSIDL_COMMON_APPDATA /Users/Shared,
// CSIDL_PROFILE ~, CSIDL_DESKTOP(DIRECTORY) ~/Desktop, ... (returned in Windows form).
#define CSIDL_DESKTOP 0x0000
#define CSIDL_PROGRAMS 0x0002
#define CSIDL_PERSONAL 0x0005
#define CSIDL_MYDOCUMENTS CSIDL_PERSONAL
#define CSIDL_FAVORITES 0x0006
#define CSIDL_STARTUP 0x0007
#define CSIDL_MYMUSIC 0x000d
#define CSIDL_MYVIDEO 0x000e
#define CSIDL_DESKTOPDIRECTORY 0x0010
#define CSIDL_FONTS 0x0014
#define CSIDL_APPDATA 0x001a
#define CSIDL_LOCAL_APPDATA 0x001c
#define CSIDL_INTERNET_CACHE 0x0020
#define CSIDL_COMMON_APPDATA 0x0023
#define CSIDL_WINDOWS 0x0024
#define CSIDL_SYSTEM 0x0025
#define CSIDL_PROGRAM_FILES 0x0026
#define CSIDL_MYPICTURES 0x0027
#define CSIDL_PROFILE 0x0028
#define CSIDL_COMMON_DOCUMENTS 0x002e
#define CSIDL_FLAG_CREATE 0x8000
#define CSIDL_FLAG_DONT_VERIFY 0x4000
#define CSIDL_FLAG_MASK 0xFF00
#define SHGFP_TYPE_CURRENT 0
#define SHGFP_TYPE_DEFAULT 1
HRESULT SHGetFolderPathA(HWND window, int folder, HANDLE token, DWORD flags, LPSTR path) noexcept;
HRESULT SHGetFolderPathW(HWND window, int folder, HANDLE token, DWORD flags, LPWSTR path) noexcept;
BOOL SHGetSpecialFolderPathA(HWND window, LPSTR path, int folder, BOOL create) noexcept;
BOOL SHGetSpecialFolderPathW(HWND window, LPWSTR path, int folder, BOOL create) noexcept;

#ifndef SW_HIDE
#define SW_HIDE 0
#endif
#ifndef SW_SHOWNORMAL
#define SW_SHOWNORMAL 1
#endif
#ifndef SW_NORMAL
#define SW_NORMAL 1
#endif
#ifndef SW_SHOWMINIMIZED
#define SW_SHOWMINIMIZED 2
#endif
#ifndef SW_SHOWMAXIMIZED
#define SW_SHOWMAXIMIZED 3
#endif
#ifndef SW_SHOW
#define SW_SHOW 5
#endif
#ifndef SW_SHOWDEFAULT
#define SW_SHOWDEFAULT 10
#endif
#define SEE_MASK_DEFAULT 0x00000000
#define SEE_MASK_NOCLOSEPROCESS 0x00000040
#define SEE_MASK_FLAG_NO_UI 0x00000400
#define SEE_MASK_NOASYNC 0x00000100
#define SE_ERR_FNF 2
#define SE_ERR_NOASSOC 31
typedef struct _SHELLEXECUTEINFOW {
  DWORD cbSize;
  ULONG fMask;
  HWND hwnd;
  LPCWSTR lpVerb;
  LPCWSTR lpFile;
  LPCWSTR lpParameters;
  LPCWSTR lpDirectory;
  int nShow;
  HINSTANCE hInstApp;
  void* lpIDList;
  LPCWSTR lpClass;
  HKEY hkeyClass;
  DWORD dwHotKey;
  union {
    HANDLE hIcon;
    HANDLE hMonitor;
  };
  HANDLE hProcess;
} SHELLEXECUTEINFOW, *LPSHELLEXECUTEINFOW;
// ShellExecute "open"s a file, folder or URL with /usr/bin/open (UI paths only); returns
// a value > 32 on success, as Windows does.
HINSTANCE ShellExecuteA(HWND window, LPCSTR operation, LPCSTR file, LPCSTR parameters, LPCSTR directory,
                        INT showCommand) noexcept;
HINSTANCE ShellExecuteW(HWND window, LPCWSTR operation, LPCWSTR file, LPCWSTR parameters, LPCWSTR directory,
                        INT showCommand) noexcept;
BOOL ShellExecuteExW(SHELLEXECUTEINFOW* info) noexcept;

// --- Privileges --------------------------------------------------------------------------------------
typedef struct _LUID {
  DWORD LowPart;
  LONG HighPart;
} LUID, *PLUID;
typedef struct _LUID_AND_ATTRIBUTES {
  LUID Luid;
  DWORD Attributes;
} LUID_AND_ATTRIBUTES, *PLUID_AND_ATTRIBUTES;
typedef struct _TOKEN_PRIVILEGES {
  DWORD PrivilegeCount;
  LUID_AND_ATTRIBUTES Privileges[1];
} TOKEN_PRIVILEGES, *PTOKEN_PRIVILEGES;
#define TOKEN_QUERY 0x0008
#define TOKEN_ADJUST_PRIVILEGES 0x0020
#define SE_PRIVILEGE_ENABLED 0x00000002L
BOOL OpenProcessToken(HANDLE process, DWORD access, PHANDLE token) noexcept;
BOOL LookupPrivilegeValueA(LPCSTR systemName, LPCSTR name, PLUID luid) noexcept;
BOOL LookupPrivilegeValueW(LPCWSTR systemName, LPCWSTR name, PLUID luid) noexcept;
BOOL AdjustTokenPrivileges(HANDLE token, BOOL disableAll, PTOKEN_PRIVILEGES newState, DWORD bufferLength,
                           PTOKEN_PRIVILEGES previousState, PDWORD returnLength) noexcept;

// --- Structured exceptions and thread contexts (diagnostics) ------------------------------------------
#define STATUS_ACCESS_VIOLATION ((DWORD)0xC0000005L)
#define STATUS_IN_PAGE_ERROR ((DWORD)0xC0000006L)
#define STATUS_INVALID_HANDLE ((DWORD)0xC0000008L)
#define STATUS_NO_MEMORY ((DWORD)0xC0000017L)
#define STATUS_ILLEGAL_INSTRUCTION ((DWORD)0xC000001DL)
#define STATUS_NONCONTINUABLE_EXCEPTION ((DWORD)0xC0000025L)
#define STATUS_INVALID_DISPOSITION ((DWORD)0xC0000026L)
#define STATUS_ARRAY_BOUNDS_EXCEEDED ((DWORD)0xC000008CL)
#define STATUS_FLOAT_DENORMAL_OPERAND ((DWORD)0xC000008DL)
#define STATUS_FLOAT_DIVIDE_BY_ZERO ((DWORD)0xC000008EL)
#define STATUS_FLOAT_INEXACT_RESULT ((DWORD)0xC000008FL)
#define STATUS_FLOAT_INVALID_OPERATION ((DWORD)0xC0000090L)
#define STATUS_FLOAT_OVERFLOW ((DWORD)0xC0000091L)
#define STATUS_FLOAT_STACK_CHECK ((DWORD)0xC0000092L)
#define STATUS_FLOAT_UNDERFLOW ((DWORD)0xC0000093L)
#define STATUS_INTEGER_DIVIDE_BY_ZERO ((DWORD)0xC0000094L)
#define STATUS_INTEGER_OVERFLOW ((DWORD)0xC0000095L)
#define STATUS_PRIVILEGED_INSTRUCTION ((DWORD)0xC0000096L)
#define STATUS_STACK_OVERFLOW ((DWORD)0xC00000FDL)
#define STATUS_GUARD_PAGE_VIOLATION ((DWORD)0x80000001L)
#define STATUS_DATATYPE_MISALIGNMENT ((DWORD)0x80000002L)
#define STATUS_BREAKPOINT ((DWORD)0x80000003L)
#define STATUS_SINGLE_STEP ((DWORD)0x80000004L)
#define STATUS_CONTROL_C_EXIT ((DWORD)0xC000013AL)
#define EXCEPTION_ACCESS_VIOLATION STATUS_ACCESS_VIOLATION
#define EXCEPTION_DATATYPE_MISALIGNMENT STATUS_DATATYPE_MISALIGNMENT
#define EXCEPTION_BREAKPOINT STATUS_BREAKPOINT
#define EXCEPTION_SINGLE_STEP STATUS_SINGLE_STEP
#define EXCEPTION_ARRAY_BOUNDS_EXCEEDED STATUS_ARRAY_BOUNDS_EXCEEDED
#define EXCEPTION_FLT_DENORMAL_OPERAND STATUS_FLOAT_DENORMAL_OPERAND
#define EXCEPTION_FLT_DIVIDE_BY_ZERO STATUS_FLOAT_DIVIDE_BY_ZERO
#define EXCEPTION_FLT_INEXACT_RESULT STATUS_FLOAT_INEXACT_RESULT
#define EXCEPTION_FLT_INVALID_OPERATION STATUS_FLOAT_INVALID_OPERATION
#define EXCEPTION_FLT_OVERFLOW STATUS_FLOAT_OVERFLOW
#define EXCEPTION_FLT_STACK_CHECK STATUS_FLOAT_STACK_CHECK
#define EXCEPTION_FLT_UNDERFLOW STATUS_FLOAT_UNDERFLOW
#define EXCEPTION_INT_DIVIDE_BY_ZERO STATUS_INTEGER_DIVIDE_BY_ZERO
#define EXCEPTION_INT_OVERFLOW STATUS_INTEGER_OVERFLOW
#define EXCEPTION_PRIV_INSTRUCTION STATUS_PRIVILEGED_INSTRUCTION
#define EXCEPTION_IN_PAGE_ERROR STATUS_IN_PAGE_ERROR
#define EXCEPTION_ILLEGAL_INSTRUCTION STATUS_ILLEGAL_INSTRUCTION
#define EXCEPTION_NONCONTINUABLE_EXCEPTION STATUS_NONCONTINUABLE_EXCEPTION
#define EXCEPTION_STACK_OVERFLOW STATUS_STACK_OVERFLOW
#define EXCEPTION_INVALID_DISPOSITION STATUS_INVALID_DISPOSITION
#define EXCEPTION_GUARD_PAGE STATUS_GUARD_PAGE_VIOLATION
#define EXCEPTION_INVALID_HANDLE STATUS_INVALID_HANDLE
#define EXCEPTION_EXECUTE_HANDLER 1
#define EXCEPTION_CONTINUE_SEARCH 0
#define EXCEPTION_CONTINUE_EXECUTION (-1)
#define EXCEPTION_NONCONTINUABLE 0x1
#define EXCEPTION_MAXIMUM_PARAMETERS 15

typedef struct _EXCEPTION_RECORD {
  DWORD ExceptionCode;
  DWORD ExceptionFlags;
  struct _EXCEPTION_RECORD* ExceptionRecord;
  PVOID ExceptionAddress;
  DWORD NumberParameters;
  ULONG_PTR ExceptionInformation[EXCEPTION_MAXIMUM_PARAMETERS];
} EXCEPTION_RECORD, *PEXCEPTION_RECORD;

// CONTEXT carries both the x86 and the x64 register names the engine's diagnostics read;
// GetThreadContext/RtlCaptureContext do not fill it on POSIX (no x86 registers exist).
#define CONTEXT_i386 0x00010000L
#define CONTEXT_AMD64 0x00100000L
#define CONTEXT_CONTROL (CONTEXT_AMD64 | 0x00000001L)
#define CONTEXT_INTEGER (CONTEXT_AMD64 | 0x00000002L)
#define CONTEXT_SEGMENTS (CONTEXT_AMD64 | 0x00000004L)
#define CONTEXT_FLOATING_POINT (CONTEXT_AMD64 | 0x00000008L)
#define CONTEXT_DEBUG_REGISTERS (CONTEXT_AMD64 | 0x00000010L)
#define CONTEXT_FULL (CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_FLOATING_POINT)
#define CONTEXT_ALL (CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_SEGMENTS | CONTEXT_FLOATING_POINT | CONTEXT_DEBUG_REGISTERS)
typedef struct _CONTEXT {
  DWORD ContextFlags;
  DWORD MxCsr;
  WORD SegCs;
  WORD SegDs;
  WORD SegEs;
  WORD SegFs;
  WORD SegGs;
  WORD SegSs;
  DWORD EFlags;
  DWORD64 Dr0;
  DWORD64 Dr1;
  DWORD64 Dr2;
  DWORD64 Dr3;
  DWORD64 Dr6;
  DWORD64 Dr7;
  DWORD64 Rax;
  DWORD64 Rcx;
  DWORD64 Rdx;
  DWORD64 Rbx;
  DWORD64 Rsp;
  DWORD64 Rbp;
  DWORD64 Rsi;
  DWORD64 Rdi;
  DWORD64 R8;
  DWORD64 R9;
  DWORD64 R10;
  DWORD64 R11;
  DWORD64 R12;
  DWORD64 R13;
  DWORD64 R14;
  DWORD64 R15;
  DWORD64 Rip;
  // x86 names (32-bit views kept separately so both spellings compile).
  DWORD Eax;
  DWORD Ecx;
  DWORD Edx;
  DWORD Ebx;
  DWORD Esp;
  DWORD Ebp;
  DWORD Esi;
  DWORD Edi;
  DWORD Eip;
  DWORD SegCsX86;
} CONTEXT, *PCONTEXT, *LPCONTEXT;

typedef struct _EXCEPTION_POINTERS {
  PEXCEPTION_RECORD ExceptionRecord;
  PCONTEXT ContextRecord;
} EXCEPTION_POINTERS, *PEXCEPTION_POINTERS, *LPEXCEPTION_POINTERS;
typedef LONG(WINAPI* PTOP_LEVEL_EXCEPTION_FILTER)(struct _EXCEPTION_POINTERS* exceptionInfo);
typedef PTOP_LEVEL_EXCEPTION_FILTER LPTOP_LEVEL_EXCEPTION_FILTER;
typedef LONG(NTAPI* PVECTORED_EXCEPTION_HANDLER)(struct _EXCEPTION_POINTERS* exceptionInfo);

// Handlers are recorded but never called: POSIX signals are not translated into structured
// exceptions (crash reporting is a diagnostics path).
LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER filter) noexcept;
LONG UnhandledExceptionFilter(struct _EXCEPTION_POINTERS* exceptionInfo) noexcept;
PVOID AddVectoredExceptionHandler(ULONG first, PVECTORED_EXCEPTION_HANDLER handler) noexcept;
ULONG RemoveVectoredExceptionHandler(PVOID handle) noexcept;
PVOID AddVectoredContinueHandler(ULONG first, PVECTORED_EXCEPTION_HANDLER handler) noexcept;
ULONG RemoveVectoredContinueHandler(PVOID handle) noexcept;
// RaiseException: the debugger thread-naming exception (0x406D1388) is ignored; any other
// code is unhandled and aborts the process, as an unhandled exception does on Windows.
void RaiseException(DWORD code, DWORD flags, DWORD argumentCount, const ULONG_PTR* arguments) noexcept;
void RtlCaptureContext(PCONTEXT context) noexcept;
// Real: the return addresses of the calling thread (execinfo backtrace).
WORD RtlCaptureStackBackTrace(DWORD framesToSkip, DWORD framesToCapture, PVOID* backTrace, PDWORD backTraceHash) noexcept;
BOOL GetThreadContext(HANDLE thread, LPCONTEXT context) noexcept;
BOOL SetThreadContext(HANDLE thread, const CONTEXT* context) noexcept;
PVOID EncodePointer(PVOID pointer) noexcept;
PVOID DecodePointer(PVOID pointer) noexcept;

// --- dbghelp (diagnostics) -------------------------------------------------------------------------
// Symbols come from dladdr (exported/dynamic symbol names, demangled); there are no line
// numbers, stack walking over a CONTEXT, or minidumps.
#define IMAGE_FILE_MACHINE_I386 0x014c
#define IMAGE_FILE_MACHINE_AMD64 0x8664
#define IMAGE_FILE_MACHINE_ARM64 0xAA64
#define SYMOPT_CASE_INSENSITIVE 0x00000001
#define SYMOPT_UNDNAME 0x00000002
#define SYMOPT_DEFERRED_LOADS 0x00000004
#define SYMOPT_NO_CPP 0x00000008
#define SYMOPT_LOAD_LINES 0x00000010
#define SYMOPT_FAIL_CRITICAL_ERRORS 0x00000200
#define SYMOPT_NO_PROMPTS 0x00080000
#define MAX_SYM_NAME 2000
#define UNDNAME_COMPLETE 0x0000
#define UNDNAME_NAME_ONLY 0x1000
typedef struct _SYMBOL_INFO {
  ULONG SizeOfStruct;
  ULONG TypeIndex;
  ULONG64 Reserved[2];
  ULONG Index;
  ULONG Size;
  ULONG64 ModBase;
  ULONG Flags;
  ULONG64 Value;
  ULONG64 Address;
  ULONG Register;
  ULONG Scope;
  ULONG Tag;
  ULONG NameLen;
  ULONG MaxNameLen;
  CHAR Name[1];
} SYMBOL_INFO, *PSYMBOL_INFO;
typedef struct _IMAGEHLP_SYMBOL64 {
  DWORD SizeOfStruct;
  DWORD64 Address;
  DWORD Size;
  DWORD Flags;
  DWORD MaxNameLength;
  CHAR Name[1];
} IMAGEHLP_SYMBOL64, *PIMAGEHLP_SYMBOL64;
typedef struct _IMAGEHLP_LINE64 {
  DWORD SizeOfStruct;
  PVOID Key;
  DWORD LineNumber;
  PCHAR FileName;
  DWORD64 Address;
} IMAGEHLP_LINE64, *PIMAGEHLP_LINE64;
typedef enum { AddrMode1616, AddrMode1632, AddrModeReal, AddrModeFlat } ADDRESS_MODE;
typedef struct _tagADDRESS64 {
  DWORD64 Offset;
  WORD Segment;
  ADDRESS_MODE Mode;
} ADDRESS64, *LPADDRESS64;
typedef struct _KDHELP64 {
  DWORD64 Thread;
  DWORD ThCallbackStack;
  DWORD ThCallbackBStore;
  DWORD NextCallback;
  DWORD FramePointer;
  DWORD64 KiCallUserMode;
  DWORD64 KeUserCallbackDispatcher;
  DWORD64 SystemRangeStart;
  DWORD64 KiUserExceptionDispatcher;
  DWORD64 StackBase;
  DWORD64 StackLimit;
  DWORD64 Reserved[5];
} KDHELP64, *PKDHELP64;
typedef struct _tagSTACKFRAME64 {
  ADDRESS64 AddrPC;
  ADDRESS64 AddrReturn;
  ADDRESS64 AddrFrame;
  ADDRESS64 AddrStack;
  ADDRESS64 AddrBStore;
  PVOID FuncTableEntry;
  DWORD64 Params[4];
  BOOL Far;
  BOOL Virtual;
  DWORD64 Reserved[3];
  KDHELP64 KdHelp;
} STACKFRAME64, *LPSTACKFRAME64;
typedef BOOL(CALLBACK* PREAD_PROCESS_MEMORY_ROUTINE64)(HANDLE process, DWORD64 base, PVOID buffer, DWORD size,
                                                       LPDWORD read);
typedef PVOID(CALLBACK* PFUNCTION_TABLE_ACCESS_ROUTINE64)(HANDLE process, DWORD64 base);
typedef DWORD64(CALLBACK* PGET_MODULE_BASE_ROUTINE64)(HANDLE process, DWORD64 address);
typedef DWORD64(CALLBACK* PTRANSLATE_ADDRESS_ROUTINE64)(HANDLE process, HANDLE thread, LPADDRESS64 address);
BOOL SymInitialize(HANDLE process, PCSTR searchPath, BOOL invadeProcess) noexcept;
BOOL SymInitializeW(HANDLE process, PCWSTR searchPath, BOOL invadeProcess) noexcept;
BOOL SymCleanup(HANDLE process) noexcept;
DWORD SymSetOptions(DWORD options) noexcept;
DWORD SymGetOptions() noexcept;
BOOL SymFromAddr(HANDLE process, DWORD64 address, PDWORD64 displacement, PSYMBOL_INFO symbol) noexcept;
BOOL SymGetSymFromAddr64(HANDLE process, DWORD64 address, PDWORD64 displacement, PIMAGEHLP_SYMBOL64 symbol) noexcept;
BOOL SymGetLineFromAddr64(HANDLE process, DWORD64 address, PDWORD displacement, PIMAGEHLP_LINE64 line) noexcept;
DWORD64 SymGetModuleBase64(HANDLE process, DWORD64 address) noexcept;
PVOID SymFunctionTableAccess64(HANDLE process, DWORD64 base) noexcept;
BOOL StackWalk64(DWORD machineType, HANDLE process, HANDLE thread, LPSTACKFRAME64 stackFrame, PVOID contextRecord,
                 PREAD_PROCESS_MEMORY_ROUTINE64 readMemory, PFUNCTION_TABLE_ACCESS_ROUTINE64 functionTableAccess,
                 PGET_MODULE_BASE_ROUTINE64 getModuleBase, PTRANSLATE_ADDRESS_ROUTINE64 translateAddress) noexcept;
DWORD UnDecorateSymbolName(PCSTR name, PSTR outputString, DWORD maxStringLength, DWORD flags) noexcept;
// The x64 dbghelp.h maps the unsuffixed names onto the 64-bit API (_IMAGEHLP64); so does
// this 64-bit build. The DWORD_PTR displacement form is what x64 callers pass.
BOOL SymGetSymFromAddr64(HANDLE process, DWORD64 address, DWORD_PTR* displacement, PIMAGEHLP_SYMBOL64 symbol) noexcept;
#define StackWalk StackWalk64
#define SymFunctionTableAccess SymFunctionTableAccess64
#define SymGetModuleBase SymGetModuleBase64
#define SymGetSymFromAddr SymGetSymFromAddr64
#define SymGetLineFromAddr SymGetLineFromAddr64
#define IMAGEHLP_SYMBOL IMAGEHLP_SYMBOL64
#define PIMAGEHLP_SYMBOL PIMAGEHLP_SYMBOL64
#define IMAGEHLP_LINE IMAGEHLP_LINE64
#define PIMAGEHLP_LINE PIMAGEHLP_LINE64
#define STACKFRAME STACKFRAME64
#define LPSTACKFRAME LPSTACKFRAME64
#define ADDRESS ADDRESS64
#define LPADDRESS LPADDRESS64

typedef enum _MINIDUMP_TYPE {
  MiniDumpNormal = 0x00000000,
  MiniDumpWithDataSegs = 0x00000001,
  MiniDumpWithFullMemory = 0x00000002,
  MiniDumpWithHandleData = 0x00000004,
  MiniDumpFilterMemory = 0x00000008,
  MiniDumpScanMemory = 0x00000010,
  MiniDumpWithUnloadedModules = 0x00000020,
  MiniDumpWithIndirectlyReferencedMemory = 0x00000040,
  MiniDumpFilterModulePaths = 0x00000080,
  MiniDumpWithProcessThreadData = 0x00000100,
  MiniDumpWithPrivateReadWriteMemory = 0x00000200,
  MiniDumpWithoutOptionalData = 0x00000400,
  MiniDumpWithFullMemoryInfo = 0x00000800,
  MiniDumpWithThreadInfo = 0x00001000,
  MiniDumpWithCodeSegs = 0x00002000
} MINIDUMP_TYPE;
typedef struct _MINIDUMP_EXCEPTION_INFORMATION {
  DWORD ThreadId;
  PEXCEPTION_POINTERS ExceptionPointers;
  BOOL ClientPointers;
} MINIDUMP_EXCEPTION_INFORMATION, *PMINIDUMP_EXCEPTION_INFORMATION;
BOOL MiniDumpWriteDump(HANDLE process, DWORD processId, HANDLE file, MINIDUMP_TYPE dumpType,
                       PMINIDUMP_EXCEPTION_INFORMATION exceptionParam, PVOID userStreamParam,
                       PVOID callbackParam) noexcept;

// --- CRT floating point (<float.h>) ----------------------------------------------------------------
// _controlfp maps the rounding (_MCW_RC) and denormal (_MCW_DN) fields onto FPCR. The x87
// precision field (_MCW_PC) has no ARM counterpart and is ignored, as the x64 CRT ignores
// it. FAF_POSIX_TODO(determinism): the Windows x86 build runs sim math with _PC_24; the
// x87 reproduction lives in legacy/math/X87Math (separate track).
#define _MCW_EM 0x0008001f
#define _EM_INEXACT 0x00000001
#define _EM_UNDERFLOW 0x00000002
#define _EM_OVERFLOW 0x00000004
#define _EM_ZERODIVIDE 0x00000008
#define _EM_INVALID 0x00000010
#define _EM_DENORMAL 0x00080000
#define _MCW_RC 0x00000300
#define _RC_NEAR 0x00000000
#define _RC_DOWN 0x00000100
#define _RC_UP 0x00000200
#define _RC_CHOP 0x00000300
#define _MCW_PC 0x00030000
#define _PC_64 0x00000000
#define _PC_53 0x00010000
#define _PC_24 0x00020000
#define _MCW_IC 0x00040000
#define _IC_AFFINE 0x00040000
#define _IC_PROJECTIVE 0x00000000
#define _MCW_DN 0x03000000
#define _DN_SAVE 0x00000000
#define _DN_FLUSH 0x01000000
#define _CW_DEFAULT (_RC_NEAR + _PC_53 + _EM_INVALID + _EM_ZERODIVIDE + _EM_OVERFLOW + _EM_UNDERFLOW + _EM_INEXACT + _EM_DENORMAL)
#define _SW_INEXACT 0x00000001
#define _SW_UNDERFLOW 0x00000002
#define _SW_OVERFLOW 0x00000004
#define _SW_ZERODIVIDE 0x00000008
#define _SW_INVALID 0x00000010
#define _SW_DENORMAL 0x00080000
unsigned int _controlfp(unsigned int newValue, unsigned int mask) noexcept;
errno_t _controlfp_s(unsigned int* current, unsigned int newValue, unsigned int mask) noexcept;
unsigned int _control87(unsigned int newValue, unsigned int mask) noexcept;
unsigned int _clearfp() noexcept;
unsigned int _statusfp() noexcept;
void _fpreset() noexcept;
// Exact bit-level operations (no libm involved; <cmath> is deliberately not pulled into
// every translation unit, as its float overloads would change which function unqualified
// math calls pick).
inline int _isnan(double x) noexcept { return __builtin_isnan(x) ? 1 : 0; }
inline int _finite(double x) noexcept { return __builtin_isfinite(x) ? 1 : 0; }
inline int _finitef(float x) noexcept { return __builtin_isfinite(x) ? 1 : 0; }
inline double _copysign(double x, double y) noexcept { return __builtin_copysign(x, y); }
inline float _copysignf(float x, float y) noexcept { return __builtin_copysignf(x, y); }
inline double _chgsign(double x) noexcept { return -x; }

// --- CRT debug and error hooks (<crtdbg.h>, <stdlib.h>) -------------------------------------------
#define _CRT_WARN 0
#define _CRT_ERROR 1
#define _CRT_ASSERT 2
#define _CRTDBG_ALLOC_MEM_DF 0x01
#define _CRTDBG_DELAY_FREE_MEM_DF 0x02
#define _CRTDBG_CHECK_ALWAYS_DF 0x04
#define _CRTDBG_RESERVED_DF 0x08
#define _CRTDBG_CHECK_CRT_DF 0x10
#define _CRTDBG_LEAK_CHECK_DF 0x20
#define _CRTDBG_CHECK_EVERY_16_DF 0x00100000
#define _CRTDBG_CHECK_EVERY_128_DF 0x00800000
#define _CRTDBG_CHECK_EVERY_1024_DF 0x04000000
#define _CRTDBG_CHECK_DEFAULT_DF 0
#define _CRTDBG_REPORT_FLAG -1
typedef int(__cdecl* _CRT_REPORT_HOOK)(int reportType, char* message, int* returnValue);
_CRT_REPORT_HOOK _CrtSetReportHook(_CRT_REPORT_HOOK hook) noexcept;
int _CrtSetDbgFlag(int flags) noexcept;
#define _WRITE_ABORT_MSG 0x1
#define _CALL_REPORTFAULT 0x2
unsigned int _set_abort_behavior(unsigned int flags, unsigned int mask) noexcept;
typedef void(__cdecl* _purecall_handler)();
_purecall_handler _set_purecall_handler(_purecall_handler handler) noexcept;
_purecall_handler _get_purecall_handler() noexcept;
// The MSVC pure-call entry: runs the installed handler, then aborts.
extern "C" int __cdecl _purecall();
// The new-handler hook operator new consults (std::get_new_handler); 1 = retry.
int _callnewh(std::size_t size) noexcept;
#define _OUT_TO_DEFAULT 0
#define _OUT_TO_STDERR 1
#define _OUT_TO_MSGBOX 2
#define _REPORT_ERRMODE 3
int _set_error_mode(int mode) noexcept;

// --- C++ runtime type information ---------------------------------------------------------------
// vcruntime's polymorphic typeid entry (`__RTtypeid`) for the Itanium C++ ABI: the
// std::type_info of the most-derived object that `object` (any base subobject of a
// polymorphic class) belongs to, read from the typeinfo slot at vtable[-1]. Throws
// std::bad_typeid for null, as vcruntime does. Returns `const std::type_info*` as void*,
// vcruntime's signature. gpg/core/reflection/Reflection.cpp calls it.
void* faf_posixRTtypeid(void* object);
