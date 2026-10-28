#pragma once

// Win32 on POSIX for the native macOS build (FAF_POSIX), force-included into every
// C++ translation unit (macos/CMakeLists.txt), as <windows.h> reaches every Windows
// translation unit through platform/Platform.h.
//
// Rules (see README.md next to this file):
// - Win32 types keep their Windows sizes. Windows is LLP64: DWORD, LONG and ULONG are
//   32-bit here even though `long` is 64-bit on macOS. Engine code that uses a bare
//   `long` where the Windows build has 32 bits must be fixed at the use site, not here.
// - Only what the engine uses: add a declaration when a translation unit needs it,
//   implement it in the matching WinCompat*.cpp, and keep Windows semantics (return
//   values, error codes) wherever callers look at them.
// - Areas live in their own headers (compat/*.h), included below in a fixed order.

#if !defined(FAF_POSIX)
#error "WinCompat.h is for the POSIX build only"
#endif

#if defined(__cplusplus)
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#else
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#endif

#include <pthread.h>
#include <unistd.h>

// --- Calling conventions and MSVC keywords ------------------------------------------
#define WINAPI
#define APIENTRY
#define CALLBACK
#define WINAPIV
#define STDMETHODCALLTYPE
#define __stdcall
#define __cdecl
#define __fastcall
#define __thiscall
#define _cdecl
#define _stdcall
#define __forceinline inline __attribute__((always_inline))
#define __unaligned
#define __w64
#define _In_
#define _In_opt_
#define _Out_
#define _Out_opt_
#define _Inout_
#define _Inout_opt_
#define _In_z_
#define _In_reads_(n)
#define _Out_writes_(n)
#define _Printf_format_string_
#define IN
#define OUT
#define OPTIONAL
#define CONST const
#define VOID void
#define FAR
#define NEAR

#if !defined(__int8)
#define __int8 char
#endif
#if !defined(__int16)
#define __int16 short
#endif
#if !defined(__int32)
#define __int32 int
#endif
#if !defined(__int64)
#define __int64 long long
#endif

// --- Integer types at their Windows (LLP64) sizes -------------------------------------
typedef std::uint8_t BYTE;
typedef std::uint8_t UCHAR;
typedef std::uint8_t BOOLEAN;
typedef char CHAR;
typedef std::int16_t SHORT;
typedef std::uint16_t WORD;
typedef std::uint16_t USHORT;
typedef std::int32_t INT;
typedef std::uint32_t UINT;
typedef std::int32_t BOOL;
typedef std::int32_t LONG;
typedef std::uint32_t ULONG;
typedef std::uint32_t DWORD;
typedef std::int32_t INT32;
typedef std::uint32_t UINT32;
typedef std::uint32_t DWORD32;
typedef std::int64_t INT64;
typedef std::uint64_t UINT64;
typedef std::int64_t LONGLONG;
typedef std::uint64_t ULONGLONG;
typedef std::uint64_t DWORD64;
typedef std::uint64_t DWORDLONG;
typedef std::intptr_t INT_PTR;
typedef std::uintptr_t UINT_PTR;
typedef std::intptr_t LONG_PTR;
typedef std::uintptr_t ULONG_PTR;
typedef std::uintptr_t DWORD_PTR;
typedef std::size_t SIZE_T;
typedef std::intptr_t SSIZE_T;
typedef float FLOAT;
typedef std::int32_t HRESULT;
typedef DWORD COLORREF;
typedef DWORD LCID;
typedef WORD LANGID;
typedef WORD ATOM;
typedef int errno_t;
typedef std::int64_t __time64_t;

// Windows wchar_t is 16 bits; macOS wchar_t is 32. WCHAR follows the platform
// wchar_t so wide literals and std::wstring keep working; anything that persists
// or transmits wide text must convert explicitly (see README.md).
typedef wchar_t WCHAR;
#if defined(UNICODE)
typedef WCHAR TCHAR;
#else
typedef char TCHAR;
#endif

typedef void* PVOID;
typedef void* LPVOID;
typedef const void* LPCVOID;
typedef BYTE* PBYTE;
typedef BYTE* LPBYTE;
typedef BOOL* PBOOL;
typedef BOOL* LPBOOL;
typedef WORD* PWORD;
typedef WORD* LPWORD;
typedef DWORD* PDWORD;
typedef DWORD* LPDWORD;
typedef LONG* PLONG;
typedef LONG* LPLONG;
typedef ULONG* PULONG;
typedef UINT* PUINT;
typedef CHAR* PCHAR;
typedef CHAR* LPSTR;
typedef CHAR* PSTR;
typedef const CHAR* LPCSTR;
typedef const CHAR* PCSTR;
typedef WCHAR* LPWSTR;
typedef WCHAR* PWSTR;
typedef const WCHAR* LPCWSTR;
typedef const WCHAR* PCWSTR;
typedef TCHAR* LPTSTR;
typedef const TCHAR* LPCTSTR;
typedef ULONG_PTR* PULONG_PTR;
typedef SIZE_T* PSIZE_T;

typedef UINT_PTR WPARAM;
typedef LONG_PTR LPARAM;
typedef LONG_PTR LRESULT;

// Opaque handles: distinct pointer types, as STRICT <windows.h> declares them.
#define FAF_DECLARE_HANDLE(name) struct name##__ { int unused; }; typedef struct name##__* name
typedef void* HANDLE;
typedef HANDLE* PHANDLE;
typedef HANDLE* LPHANDLE;
FAF_DECLARE_HANDLE(HWND);
FAF_DECLARE_HANDLE(HINSTANCE);
FAF_DECLARE_HANDLE(HDC);
FAF_DECLARE_HANDLE(HICON);
FAF_DECLARE_HANDLE(HMENU);
FAF_DECLARE_HANDLE(HKEY);
FAF_DECLARE_HANDLE(HMONITOR);
FAF_DECLARE_HANDLE(HGLRC);
FAF_DECLARE_HANDLE(HBITMAP);
FAF_DECLARE_HANDLE(HBRUSH);
FAF_DECLARE_HANDLE(HFONT);
FAF_DECLARE_HANDLE(HPEN);
FAF_DECLARE_HANDLE(HRGN);
FAF_DECLARE_HANDLE(HGDIOBJ_);
typedef HICON HCURSOR;
typedef HINSTANCE HMODULE;
typedef void* HGDIOBJ;
typedef void* HGLOBAL;
typedef void* HLOCAL;
typedef void* FARPROC;
typedef HKEY* PHKEY;

#define INVALID_HANDLE_VALUE (reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(-1)))

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
#define INFINITE 0xFFFFFFFFu
#define WAIT_OBJECT_0 0x00000000u
#define WAIT_ABANDONED 0x00000080u
#define WAIT_TIMEOUT 0x00000102u
#define WAIT_FAILED 0xFFFFFFFFu
#define MAXIMUM_WAIT_OBJECTS 64

#define S_OK ((HRESULT)0)
#define S_FALSE ((HRESULT)1)
#define E_FAIL ((HRESULT)0x80004005L)
#define E_NOTIMPL ((HRESULT)0x80004001L)
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#define E_INVALIDARG ((HRESULT)0x80070057L)
#define E_POINTER ((HRESULT)0x80004003L)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)

#define LOWORD(l) ((WORD)(((DWORD_PTR)(l)) & 0xffff))
#define HIWORD(l) ((WORD)((((DWORD_PTR)(l)) >> 16) & 0xffff))
#define LOBYTE(w) ((BYTE)(((DWORD_PTR)(w)) & 0xff))
#define HIBYTE(w) ((BYTE)((((DWORD_PTR)(w)) >> 8) & 0xff))
#define MAKEWORD(a, b) ((WORD)(((BYTE)(((DWORD_PTR)(a)) & 0xff)) | ((WORD)((BYTE)(((DWORD_PTR)(b)) & 0xff))) << 8))
#define MAKELONG(a, b) ((LONG)(((WORD)(((DWORD_PTR)(a)) & 0xffff)) | ((DWORD)((WORD)(((DWORD_PTR)(b)) & 0xffff))) << 16))
#define RGB(r, g, b) ((COLORREF)(((BYTE)(r) | ((WORD)((BYTE)(g)) << 8)) | (((DWORD)(BYTE)(b)) << 16)))
#define UNREFERENCED_PARAMETER(P) (void)(P)
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#define ZeroMemory(dst, n) std::memset((dst), 0, (n))
#define CopyMemory(dst, src, n) std::memcpy((dst), (src), (n))
#define MoveMemory(dst, src, n) std::memmove((dst), (src), (n))
#define FillMemory(dst, n, v) std::memset((dst), (v), (n))
#ifndef TEXT
#if defined(UNICODE)
#define TEXT(s) L##s
#else
#define TEXT(s) s
#endif
#endif
#define _T(s) TEXT(s)

typedef union _LARGE_INTEGER {
  struct {
    DWORD LowPart;
    LONG HighPart;
  };
  struct {
    DWORD LowPart;
    LONG HighPart;
  } u;
  LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

typedef union _ULARGE_INTEGER {
  struct {
    DWORD LowPart;
    DWORD HighPart;
  };
  struct {
    DWORD LowPart;
    DWORD HighPart;
  } u;
  ULONGLONG QuadPart;
} ULARGE_INTEGER, *PULARGE_INTEGER;

typedef struct _FILETIME {
  DWORD dwLowDateTime;
  DWORD dwHighDateTime;
} FILETIME, *PFILETIME, *LPFILETIME;

typedef struct _SYSTEMTIME {
  WORD wYear;
  WORD wMonth;
  WORD wDayOfWeek;
  WORD wDay;
  WORD wHour;
  WORD wMinute;
  WORD wSecond;
  WORD wMilliseconds;
} SYSTEMTIME, *PSYSTEMTIME, *LPSYSTEMTIME;

typedef struct tagPOINT {
  LONG x;
  LONG y;
} POINT, *PPOINT, *LPPOINT;

typedef struct tagSIZE {
  LONG cx;
  LONG cy;
} SIZE, *PSIZE, *LPSIZE;

typedef struct tagRECT {
  LONG left;
  LONG top;
  LONG right;
  LONG bottom;
} RECT, *PRECT, *LPRECT;
typedef const RECT* LPCRECT;

typedef struct _GUID {
  std::uint32_t Data1;
  std::uint16_t Data2;
  std::uint16_t Data3;
  std::uint8_t Data4[8];
} GUID;
typedef GUID IID;
typedef GUID CLSID;
#if defined(__cplusplus)
#define REFGUID const GUID&
#define REFIID const IID&
#define REFCLSID const CLSID&
#endif

// --- Areas ------------------------------------------------------------------------------
// One header per area; each declares what the engine calls, implemented in
// WinCompat<Area>.cpp. Fixed order; an area may use only those above it.
#include "compat/Errors.h"
#include "compat/Sync.h"
#include "compat/Threads.h"
#include "compat/Time.h"
#include "compat/Files.h"
#include "compat/Strings.h"
#include "compat/Memory.h"
#include "compat/Modules.h"
#include "compat/Sockets.h"
#include "compat/Window.h"
#include "compat/Gdi.h"
#include "compat/Misc.h"
#include "compat/Math.h"
