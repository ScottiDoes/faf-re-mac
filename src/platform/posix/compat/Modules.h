#pragma once

// Win32 on POSIX: Modules. Included only by ../WinCompat.h (see its rules).
// Implementations: ../WinCompatModules.cpp.
//
// An HMODULE is the Mach-O header address of a loaded image (as a Windows HMODULE is the
// image base): GetModuleHandle(nullptr) is the executable, GetModuleHandleEx(FROM_ADDRESS)
// finds the image holding an address, GetProcAddress looks a symbol up with dlsym. Modules
// are found by file name with or without extension, ignoring case. LoadLibrary loads
// dylibs/bundles with dlopen; a Windows DLL name ("*.dll") does not exist here and fails
// with ERROR_MOD_NOT_FOUND, as a missing DLL does on Windows. Module paths are returned in
// Windows form ("C:\...", see Files.h).

#define DLL_PROCESS_DETACH 0
#define DLL_PROCESS_ATTACH 1
#define DLL_THREAD_ATTACH 2
#define DLL_THREAD_DETACH 3
#ifndef NTAPI
#define NTAPI
#endif
typedef void(NTAPI* PIMAGE_TLS_CALLBACK)(PVOID dllHandle, DWORD reason, PVOID reserved);

#define GET_MODULE_HANDLE_EX_FLAG_PIN 0x00000001
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x00000002
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x00000004
#define LOAD_LIBRARY_AS_DATAFILE 0x00000002
#define LOAD_WITH_ALTERED_SEARCH_PATH 0x00000008
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800

HMODULE GetModuleHandleA(LPCSTR moduleName) noexcept;
HMODULE GetModuleHandleW(LPCWSTR moduleName) noexcept;
BOOL GetModuleHandleExA(DWORD flags, LPCSTR moduleName, HMODULE* module) noexcept;
BOOL GetModuleHandleExW(DWORD flags, LPCWSTR moduleName, HMODULE* module) noexcept;
DWORD GetModuleFileNameA(HMODULE module, LPSTR fileName, DWORD size) noexcept;
DWORD GetModuleFileNameW(HMODULE module, LPWSTR fileName, DWORD size) noexcept;
HMODULE LoadLibraryA(LPCSTR fileName) noexcept;
HMODULE LoadLibraryW(LPCWSTR fileName) noexcept;
HMODULE LoadLibraryExA(LPCSTR fileName, HANDLE file, DWORD flags) noexcept;
HMODULE LoadLibraryExW(LPCWSTR fileName, HANDLE file, DWORD flags) noexcept;
BOOL FreeLibrary(HMODULE module) noexcept;
FARPROC GetProcAddress(HMODULE module, LPCSTR procName) noexcept;
BOOL DisableThreadLibraryCalls(HMODULE module) noexcept;

// --- Tool help snapshots (<tlhelp32.h>) -----------------------------------------------------------
// Modules: every image dyld has loaded. Threads: every thread of this process the layer
// knows (all threads that called into it or were started by it), with their Win32 ids.
#define TH32CS_SNAPHEAPLIST 0x00000001
#define TH32CS_SNAPPROCESS 0x00000002
#define TH32CS_SNAPTHREAD 0x00000004
#define TH32CS_SNAPMODULE 0x00000008
#define TH32CS_SNAPMODULE32 0x00000010
#define TH32CS_SNAPALL (TH32CS_SNAPHEAPLIST | TH32CS_SNAPPROCESS | TH32CS_SNAPTHREAD | TH32CS_SNAPMODULE)
#define MAX_MODULE_NAME32 255
typedef struct tagTHREADENTRY32 {
  DWORD dwSize;
  DWORD cntUsage;
  DWORD th32ThreadID;
  DWORD th32OwnerProcessID;
  LONG tpBasePri;
  LONG tpDeltaPri;
  DWORD dwFlags;
} THREADENTRY32, *PTHREADENTRY32, *LPTHREADENTRY32;
typedef struct tagMODULEENTRY32W {
  DWORD dwSize;
  DWORD th32ModuleID;
  DWORD th32ProcessID;
  DWORD GlblcntUsage;
  DWORD ProccntUsage;
  BYTE* modBaseAddr;
  DWORD modBaseSize;
  HMODULE hModule;
  WCHAR szModule[MAX_MODULE_NAME32 + 1];
  WCHAR szExePath[MAX_PATH];
} MODULEENTRY32W, *PMODULEENTRY32W, *LPMODULEENTRY32W;
typedef struct tagMODULEENTRY32 {
  DWORD dwSize;
  DWORD th32ModuleID;
  DWORD th32ProcessID;
  DWORD GlblcntUsage;
  DWORD ProccntUsage;
  BYTE* modBaseAddr;
  DWORD modBaseSize;
  HMODULE hModule;
  char szModule[MAX_MODULE_NAME32 + 1];
  char szExePath[MAX_PATH];
} MODULEENTRY32, *PMODULEENTRY32, *LPMODULEENTRY32;
HANDLE CreateToolhelp32Snapshot(DWORD flags, DWORD processId) noexcept;
BOOL Thread32First(HANDLE snapshot, LPTHREADENTRY32 entry) noexcept;
BOOL Thread32Next(HANDLE snapshot, LPTHREADENTRY32 entry) noexcept;
BOOL Module32First(HANDLE snapshot, LPMODULEENTRY32 entry) noexcept;
BOOL Module32Next(HANDLE snapshot, LPMODULEENTRY32 entry) noexcept;
BOOL Module32FirstW(HANDLE snapshot, LPMODULEENTRY32W entry) noexcept;
BOOL Module32NextW(HANDLE snapshot, LPMODULEENTRY32W entry) noexcept;
