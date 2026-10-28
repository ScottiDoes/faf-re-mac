#pragma once

// Win32 on POSIX: Strings. Included only by ../WinCompat.h (see its rules).
// Implementations: ../WinCompatStrings.cpp.
//
// Code pages: the layer's ANSI and OEM code page is UTF-8 (GetACP() == CP_UTF8, as Windows
// with "Use Unicode UTF-8 for worldwide language support"), so CP_ACP/CP_OEMCP/
// CP_THREAD_ACP convert as CP_UTF8; 1252 and 28591 are converted by table. Wide strings
// are UTF-32 wchar_t (see README.md rule 3); counts are in wchar_t units.
//
// printf family: the MSVC-only functions (_snprintf, sprintf_s, swprintf_s, ...) keep
// their MSVC return and truncation rules, and their format strings are read the MSVC way:
// in wide formats %s/%c are wide and %S/%C narrow; %I64, %I32 and %I size prefixes; in
// narrow formats %S/%C are wide. Standard functions (snprintf, swprintf) are untouched.

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <strings.h>

// Case-insensitive compares. The CRT folds ASCII to lower case before comparing, as
// strcasecmp does, so orderings (sorted maps keyed by these) match Windows.
inline int _stricmp(const char* a, const char* b) noexcept { return ::strcasecmp(a, b); }
inline int _strnicmp(const char* a, const char* b, std::size_t n) noexcept { return ::strncasecmp(a, b, n); }
inline int _wcsicmp(const wchar_t* a, const wchar_t* b) noexcept { return ::wcscasecmp(a, b); }
inline int _wcsnicmp(const wchar_t* a, const wchar_t* b, std::size_t n) noexcept { return ::wcsncasecmp(a, b, n); }
#define stricmp _stricmp
#define strnicmp _strnicmp
#define _strcmpi _stricmp
#define strcmpi _stricmp
int _memicmp(const void* a, const void* b, std::size_t count) noexcept;
#define memicmp _memicmp

inline char* _strlwr(char* s) noexcept
{
  for (char* p = s; *p != '\0'; ++p) {
    if (*p >= 'A' && *p <= 'Z') {
      *p = static_cast<char>(*p - 'A' + 'a');
    }
  }
  return s;
}
inline char* _strupr(char* s) noexcept
{
  for (char* p = s; *p != '\0'; ++p) {
    if (*p >= 'a' && *p <= 'z') {
      *p = static_cast<char>(*p - 'a' + 'A');
    }
  }
  return s;
}
wchar_t* _wcslwr(wchar_t* s) noexcept;
wchar_t* _wcsupr(wchar_t* s) noexcept;
inline char* _strdup(const char* s) noexcept { return ::strdup(s); }
inline wchar_t* _wcsdup(const wchar_t* s) noexcept { return ::wcsdup(s); }
char* _strrev(char* s) noexcept;

// --- Code pages ----------------------------------------------------------------------------
#define CP_ACP 0
#define CP_OEMCP 1
#define CP_MACCP 2
#define CP_THREAD_ACP 3
#define CP_SYMBOL 42
#define CP_UTF7 65000
#define CP_UTF8 65001
#define MB_PRECOMPOSED 0x00000001
#define MB_COMPOSITE 0x00000002
#define MB_USEGLYPHCHARS 0x00000004
#define MB_ERR_INVALID_CHARS 0x00000008
#define WC_COMPOSITECHECK 0x00000200
#define WC_DISCARDNS 0x00000010
#define WC_SEPCHARS 0x00000020
#define WC_DEFAULTCHAR 0x00000040
#define WC_ERR_INVALID_CHARS 0x00000080
#define WC_NO_BEST_FIT_CHARS 0x00000400
#define MAX_DEFAULTCHAR 2
#define MAX_LEADBYTES 12
typedef struct _cpinfo {
  UINT MaxCharSize;
  BYTE DefaultChar[MAX_DEFAULTCHAR];
  BYTE LeadByte[MAX_LEADBYTES];
} CPINFO, *LPCPINFO;
int MultiByteToWideChar(UINT codePage, DWORD flags, LPCSTR source, int sourceLength, LPWSTR destination,
                        int destinationLength) noexcept;
int WideCharToMultiByte(UINT codePage, DWORD flags, LPCWSTR source, int sourceLength, LPSTR destination,
                        int destinationLength, LPCSTR defaultChar, LPBOOL usedDefaultChar) noexcept;
UINT GetACP() noexcept;
UINT GetOEMCP() noexcept;
BOOL GetCPInfo(UINT codePage, LPCPINFO info) noexcept;
BOOL IsValidCodePage(UINT codePage) noexcept;
BOOL IsDBCSLeadByte(BYTE testChar) noexcept;
BOOL IsDBCSLeadByteEx(UINT codePage, BYTE testChar) noexcept;

// --- Locale-taking CRT variants ---------------------------------------------------------------
// No CRT locale objects exist on POSIX: a null _locale_t (the only kind the engine passes)
// means the current locale, as in the CRT.
typedef struct faf_posixLocale* _locale_t;
inline wint_t _towlower_l(wint_t c, _locale_t) noexcept { return ::towlower(c); }
inline wint_t _towupper_l(wint_t c, _locale_t) noexcept { return ::towupper(c); }
inline int _tolower_l(int c, _locale_t) noexcept { return ::tolower(c); }
inline int _toupper_l(int c, _locale_t) noexcept { return ::toupper(c); }
inline int _stricmp_l(const char* a, const char* b, _locale_t) noexcept { return ::strcasecmp(a, b); }
inline int _strnicmp_l(const char* a, const char* b, std::size_t n, _locale_t) noexcept { return ::strncasecmp(a, b, n); }
inline int _wcsicmp_l(const wchar_t* a, const wchar_t* b, _locale_t) noexcept { return ::wcscasecmp(a, b); }
inline long _wcstol_l(const wchar_t* s, wchar_t** end, int base, _locale_t) noexcept { return ::wcstol(s, end, base); }

// --- Secure CRT ------------------------------------------------------------------------------
// A violated precondition (null pointer, buffer too small without _TRUNCATE) calls the
// handler installed with _set_invalid_parameter_handler, then fails with the documented
// errno and an empty destination; with no handler installed it is reported on stderr
// instead of terminating the process as the release CRT would.
#define _TRUNCATE ((std::size_t)-1)
typedef void(__cdecl* _invalid_parameter_handler)(const wchar_t* expression, const wchar_t* function,
                                                   const wchar_t* file, unsigned int line, std::uintptr_t reserved);
_invalid_parameter_handler _set_invalid_parameter_handler(_invalid_parameter_handler handler) noexcept;
_invalid_parameter_handler _get_invalid_parameter_handler() noexcept;
void _invalid_parameter_noinfo() noexcept;
void _invalid_parameter_noinfo_noreturn() noexcept;

errno_t strcpy_s(char* destination, std::size_t size, const char* source) noexcept;
errno_t strncpy_s(char* destination, std::size_t size, const char* source, std::size_t count) noexcept;
errno_t strcat_s(char* destination, std::size_t size, const char* source) noexcept;
errno_t strncat_s(char* destination, std::size_t size, const char* source, std::size_t count) noexcept;
errno_t wcscpy_s(wchar_t* destination, std::size_t size, const wchar_t* source) noexcept;
errno_t wcsncpy_s(wchar_t* destination, std::size_t size, const wchar_t* source, std::size_t count) noexcept;
errno_t wcscat_s(wchar_t* destination, std::size_t size, const wchar_t* source) noexcept;
errno_t wcsncat_s(wchar_t* destination, std::size_t size, const wchar_t* source, std::size_t count) noexcept;
errno_t _strlwr_s(char* text, std::size_t size) noexcept;
errno_t _strupr_s(char* text, std::size_t size) noexcept;
errno_t _wcslwr_s(wchar_t* text, std::size_t size) noexcept;
errno_t _wcsupr_s(wchar_t* text, std::size_t size) noexcept;
char* strtok_s(char* text, const char* delimiters, char** context) noexcept;
wchar_t* wcstok_s(wchar_t* text, const wchar_t* delimiters, wchar_t** context) noexcept;
errno_t strerror_s(char* buffer, std::size_t size, int error) noexcept;
errno_t mbstowcs_s(std::size_t* converted, wchar_t* destination, std::size_t size, const char* source,
                   std::size_t count) noexcept;
errno_t wcstombs_s(std::size_t* converted, char* destination, std::size_t size, const wchar_t* source,
                   std::size_t count) noexcept;

int sprintf_s(char* buffer, std::size_t size, const char* format, ...) noexcept;
int vsprintf_s(char* buffer, std::size_t size, const char* format, va_list arguments) noexcept;
int _snprintf_s(char* buffer, std::size_t size, std::size_t count, const char* format, ...) noexcept;
int _vsnprintf_s(char* buffer, std::size_t size, std::size_t count, const char* format, va_list arguments) noexcept;
int vsnprintf_s(char* buffer, std::size_t size, std::size_t count, const char* format, va_list arguments) noexcept;
int swprintf_s(wchar_t* buffer, std::size_t size, const wchar_t* format, ...) noexcept;
int vswprintf_s(wchar_t* buffer, std::size_t size, const wchar_t* format, va_list arguments) noexcept;
int _snwprintf_s(wchar_t* buffer, std::size_t size, std::size_t count, const wchar_t* format, ...) noexcept;
int _vsnwprintf_s(wchar_t* buffer, std::size_t size, std::size_t count, const wchar_t* format,
                  va_list arguments) noexcept;

// Secure scanf (MSVC semantics). %c, %s and %[ (and MSVC's %C/%S) take an extra `unsigned`
// buffer size, in elements, after their pointer. A token that does not fit (with its
// terminator, for %s/%[) leaves an empty string, sets errno to ENOMEM and stops the scan;
// the result is the number of fields assigned before it. Size prefixes are MSVC's: `l` on an
// integer conversion stores a 32-bit Windows `long`; I64, I32 and I are accepted. In
// sscanf_s %S/%C/%ls fill wide buffers; in swscanf_s %s/%c are wide and %S/%C/%hs narrow.
// Narrow <-> wide is the "C" locale's byte <-> code unit mapping. Numeric conversions use
// the C library's scanner. No fscanf_s: nothing in the engine calls it.
int sscanf_s(const char* buffer, const char* format, ...) noexcept;
int vsscanf_s(const char* buffer, const char* format, va_list arguments) noexcept;
int swscanf_s(const wchar_t* buffer, const wchar_t* format, ...) noexcept;
int vswscanf_s(const wchar_t* buffer, const wchar_t* format, va_list arguments) noexcept;

// MSVC's non-secure counted forms: -1 (and no terminator) when the output does not fit.
int _snprintf(char* buffer, std::size_t count, const char* format, ...) noexcept;
int _vsnprintf(char* buffer, std::size_t count, const char* format, va_list arguments) noexcept;
int _snwprintf(wchar_t* buffer, std::size_t count, const wchar_t* format, ...) noexcept;
int _vsnwprintf(wchar_t* buffer, std::size_t count, const wchar_t* format, va_list arguments) noexcept;
int _scprintf(const char* format, ...) noexcept;
int _vscprintf(const char* format, va_list arguments) noexcept;
int _scwprintf(const wchar_t* format, ...) noexcept;
int _vscwprintf(const wchar_t* format, va_list arguments) noexcept;

// Array forms (the CRT's C++ template overloads).
template <std::size_t N>
inline errno_t strcpy_s(char (&destination)[N], const char* source) noexcept
{
  return strcpy_s(destination, N, source);
}
template <std::size_t N>
inline errno_t strncpy_s(char (&destination)[N], const char* source, std::size_t count) noexcept
{
  return strncpy_s(destination, N, source, count);
}
template <std::size_t N>
inline errno_t strcat_s(char (&destination)[N], const char* source) noexcept
{
  return strcat_s(destination, N, source);
}
template <std::size_t N>
inline errno_t strncat_s(char (&destination)[N], const char* source, std::size_t count) noexcept
{
  return strncat_s(destination, N, source, count);
}
template <std::size_t N>
inline errno_t wcscpy_s(wchar_t (&destination)[N], const wchar_t* source) noexcept
{
  return wcscpy_s(destination, N, source);
}
template <std::size_t N>
inline errno_t wcsncpy_s(wchar_t (&destination)[N], const wchar_t* source, std::size_t count) noexcept
{
  return wcsncpy_s(destination, N, source, count);
}
template <std::size_t N>
inline errno_t wcscat_s(wchar_t (&destination)[N], const wchar_t* source) noexcept
{
  return wcscat_s(destination, N, source);
}
template <std::size_t N>
inline int sprintf_s(char (&buffer)[N], const char* format, ...) noexcept
{
  va_list arguments;
  va_start(arguments, format);
  const int result = vsprintf_s(buffer, N, format, arguments);
  va_end(arguments);
  return result;
}
template <std::size_t N>
inline int vsprintf_s(char (&buffer)[N], const char* format, va_list arguments) noexcept
{
  return vsprintf_s(buffer, N, format, arguments);
}
template <std::size_t N>
inline int _snprintf_s(char (&buffer)[N], std::size_t count, const char* format, ...) noexcept
{
  va_list arguments;
  va_start(arguments, format);
  const int result = _vsnprintf_s(buffer, N, count, format, arguments);
  va_end(arguments);
  return result;
}
template <std::size_t N>
inline int _vsnprintf_s(char (&buffer)[N], std::size_t count, const char* format, va_list arguments) noexcept
{
  return _vsnprintf_s(buffer, N, count, format, arguments);
}
template <std::size_t N>
inline int swprintf_s(wchar_t (&buffer)[N], const wchar_t* format, ...) noexcept
{
  va_list arguments;
  va_start(arguments, format);
  const int result = vswprintf_s(buffer, N, format, arguments);
  va_end(arguments);
  return result;
}
template <std::size_t N>
inline int _snwprintf_s(wchar_t (&buffer)[N], std::size_t count, const wchar_t* format, ...) noexcept
{
  va_list arguments;
  va_start(arguments, format);
  const int result = _vsnwprintf_s(buffer, N, count, format, arguments);
  va_end(arguments);
  return result;
}

// --- Number conversions ------------------------------------------------------------------------
char* _itoa(int value, char* buffer, int radix) noexcept;
char* _ltoa(long value, char* buffer, int radix) noexcept;
char* _ultoa(unsigned long value, char* buffer, int radix) noexcept;
char* _i64toa(long long value, char* buffer, int radix) noexcept;
char* _ui64toa(unsigned long long value, char* buffer, int radix) noexcept;
wchar_t* _itow(int value, wchar_t* buffer, int radix) noexcept;
errno_t _itoa_s(int value, char* buffer, std::size_t size, int radix) noexcept;
errno_t _i64toa_s(long long value, char* buffer, std::size_t size, int radix) noexcept;
errno_t _ui64toa_s(unsigned long long value, char* buffer, std::size_t size, int radix) noexcept;
inline int _wtoi(const wchar_t* s) noexcept { return static_cast<int>(::wcstol(s, nullptr, 10)); }
inline long _wtol(const wchar_t* s) noexcept { return ::wcstol(s, nullptr, 10); }
inline double _wtof(const wchar_t* s) noexcept { return ::wcstod(s, nullptr); }
inline long long _wtoi64(const wchar_t* s) noexcept { return ::wcstoll(s, nullptr, 10); }
inline long long _atoi64(const char* s) noexcept { return ::strtoll(s, nullptr, 10); }
inline long long _strtoi64(const char* s, char** end, int base) noexcept { return ::strtoll(s, end, base); }
inline unsigned long long _strtoui64(const char* s, char** end, int base) noexcept { return ::strtoull(s, end, base); }
inline long long _wcstoi64(const wchar_t* s, wchar_t** end, int base) noexcept { return ::wcstoll(s, end, base); }
inline unsigned long long _wcstoui64(const wchar_t* s, wchar_t** end, int base) noexcept
{
  return ::wcstoull(s, end, base);
}
