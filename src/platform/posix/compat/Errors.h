#pragma once

// Win32 on POSIX: Errors. Included only by ../WinCompat.h (see its rules).
// Implementations: ../WinCompatErrors.cpp.
//
// GetLastError/SetLastError keep a per-thread Windows error code (DWORD), set by every
// compat function the way kernel32 sets it, so callers that test the code (e.g.
// ERROR_ALREADY_EXISTS after CreateDirectory, ERROR_NO_MORE_FILES after FindNextFile)
// behave as on Windows. errno values from POSIX calls are translated, never passed on.

#include <atomic>
#include <cerrno>
#include <cstdarg>

// --- Stub reporting ----------------------------------------------------------------------
// A compat entry point that only a UI, diagnostics or rendering path reaches and that has
// no POSIX equivalent yet: logs once per call site (stderr), then does whatever the
// function documents as its failure result. README.md rule 6.
extern "C" void faf_posixReportStub(const char* what, const char* file, int line) noexcept;
#define FAF_POSIX_STUB(what)                                                                \
  do {                                                                                      \
    static std::atomic<bool> faf_posixStubReported_{false};                                 \
    if (!faf_posixStubReported_.exchange(true, std::memory_order_relaxed)) {                \
      faf_posixReportStub((what), __FILE__, __LINE__);                                      \
    }                                                                                       \
  } while (0)

// --- Last error --------------------------------------------------------------------------
DWORD GetLastError() noexcept;
void SetLastError(DWORD code) noexcept;
void SetLastErrorEx(DWORD code, DWORD type) noexcept;

// --- Win32 error codes (winerror.h values) ------------------------------------------------
// The neutral (non-A/W) names of Win32 functions are not defined as macros anywhere in
// this layer: the engine calls the A/W names, and macros such as CreateEvent or CopyFile
// would rename engine methods of the same name.
#define ERROR_SUCCESS 0L
#define NO_ERROR 0L
#define ERROR_INVALID_FUNCTION 1L
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_PATH_NOT_FOUND 3L
#define ERROR_TOO_MANY_OPEN_FILES 4L
#define ERROR_ACCESS_DENIED 5L
#define ERROR_INVALID_HANDLE 6L
#define ERROR_ARENA_TRASHED 7L
#define ERROR_NOT_ENOUGH_MEMORY 8L
#define ERROR_INVALID_BLOCK 9L
#define ERROR_BAD_ENVIRONMENT 10L
#define ERROR_BAD_FORMAT 11L
#define ERROR_INVALID_ACCESS 12L
#define ERROR_INVALID_DATA 13L
#define ERROR_OUTOFMEMORY 14L
#define ERROR_INVALID_DRIVE 15L
#define ERROR_CURRENT_DIRECTORY 16L
#define ERROR_NOT_SAME_DEVICE 17L
#define ERROR_NO_MORE_FILES 18L
#define ERROR_WRITE_PROTECT 19L
#define ERROR_NOT_READY 21L
#define ERROR_BAD_LENGTH 24L
#define ERROR_SEEK 25L
#define ERROR_WRITE_FAULT 29L
#define ERROR_READ_FAULT 30L
#define ERROR_GEN_FAILURE 31L
#define ERROR_SHARING_VIOLATION 32L
#define ERROR_LOCK_VIOLATION 33L
#define ERROR_HANDLE_EOF 38L
#define ERROR_HANDLE_DISK_FULL 39L
#define ERROR_NOT_SUPPORTED 50L
#define ERROR_FILE_EXISTS 80L
#define ERROR_CANNOT_MAKE 82L
#define ERROR_INVALID_PARAMETER 87L
#define ERROR_BROKEN_PIPE 109L
#define ERROR_OPEN_FAILED 110L
#define ERROR_BUFFER_OVERFLOW 111L
#define ERROR_DISK_FULL 112L
#define ERROR_CALL_NOT_IMPLEMENTED 120L
#define ERROR_SEM_TIMEOUT 121L
#define ERROR_INSUFFICIENT_BUFFER 122L
#define ERROR_INVALID_NAME 123L
#define ERROR_MOD_NOT_FOUND 126L
#define ERROR_PROC_NOT_FOUND 127L
#define ERROR_NEGATIVE_SEEK 131L
#define ERROR_DIR_NOT_EMPTY 145L
#define ERROR_BAD_PATHNAME 161L
#define ERROR_BUSY 170L
#define ERROR_ALREADY_EXISTS 183L
#define ERROR_BAD_EXE_FORMAT 193L
#define ERROR_ENVVAR_NOT_FOUND 203L
#define ERROR_FILENAME_EXCED_RANGE 206L
#define ERROR_MORE_DATA 234L
#define ERROR_NO_MORE_ITEMS 259L
#define ERROR_DIRECTORY 267L
#define ERROR_NOT_OWNER 288L
#define ERROR_TOO_MANY_POSTS 298L
#define ERROR_PARTIAL_COPY 299L
#define ERROR_MR_MID_NOT_FOUND 317L
#define ERROR_INVALID_ADDRESS 487L
#define ERROR_UNHANDLED_EXCEPTION 574L
#define ERROR_OPERATION_ABORTED 995L
#define ERROR_IO_INCOMPLETE 996L
#define ERROR_IO_PENDING 997L
#define ERROR_NOACCESS 998L
#define ERROR_INVALID_FLAGS 1004L
#define ERROR_FILE_INVALID 1006L
#define ERROR_NO_UNICODE_TRANSLATION 1113L
#define ERROR_MAPPED_ALIGNMENT 1132L
#define ERROR_NOT_FOUND 1168L
#define ERROR_CANCELLED 1223L
#define ERROR_NOT_ALL_ASSIGNED 1300L
#define ERROR_PRIVILEGE_NOT_HELD 1314L
#define ERROR_INVALID_WINDOW_HANDLE 1400L
#define ERROR_TIMEOUT 1460L
#define ERROR_INVALID_USER_BUFFER 1784L
#define ERROR_RESOURCE_DATA_NOT_FOUND 1812L
#define ERROR_RESOURCE_TYPE_NOT_FOUND 1813L
#define ERROR_RESOURCE_NAME_NOT_FOUND 1814L
#define ERROR_CANT_RESOLVE_FILENAME 1921L

// --- HRESULT helpers -----------------------------------------------------------------------
#define FACILITY_WIN32 7
#define SEVERITY_SUCCESS 0
#define SEVERITY_ERROR 1
#define MAKE_HRESULT(sev, fac, code)                                                        \
  ((HRESULT)(((std::uint32_t)(sev) << 31) | ((std::uint32_t)(fac) << 16) | ((std::uint32_t)(code))))
#define HRESULT_CODE(hr) ((hr) & 0xFFFF)
#define HRESULT_FACILITY(hr) (((hr) >> 16) & 0x1fff)
#define HRESULT_SEVERITY(hr) ((((std::uint32_t)(hr)) >> 31) & 0x1)
#define IS_ERROR(status) (((std::uint32_t)(status)) >> 31 == SEVERITY_ERROR)
constexpr HRESULT HRESULT_FROM_WIN32(std::uint32_t x) noexcept
{
  return static_cast<HRESULT>(x) <= 0 ? static_cast<HRESULT>(x)
                                      : static_cast<HRESULT>((x & 0x0000FFFFu) | (FACILITY_WIN32 << 16) | 0x80000000u);
}
#ifndef E_UNEXPECTED
#define E_UNEXPECTED ((HRESULT)0x8000FFFFL)
#endif
#ifndef E_NOINTERFACE
#define E_NOINTERFACE ((HRESULT)0x80004002L)
#endif
#ifndef E_ABORT
#define E_ABORT ((HRESULT)0x80004004L)
#endif
#ifndef E_ACCESSDENIED
#define E_ACCESSDENIED ((HRESULT)0x80070005L)
#endif
#ifndef E_HANDLE
#define E_HANDLE ((HRESULT)0x80070006L)
#endif

// --- FormatMessage -------------------------------------------------------------------------
#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x00000100
#define FORMAT_MESSAGE_IGNORE_INSERTS 0x00000200
#define FORMAT_MESSAGE_FROM_STRING 0x00000400
#define FORMAT_MESSAGE_FROM_HMODULE 0x00000800
#define FORMAT_MESSAGE_FROM_SYSTEM 0x00001000
#define FORMAT_MESSAGE_ARGUMENT_ARRAY 0x00002000
#define FORMAT_MESSAGE_MAX_WIDTH_MASK 0x000000FF
#define LANG_NEUTRAL 0x00
#define LANG_ENGLISH 0x09
#define SUBLANG_NEUTRAL 0x00
#define SUBLANG_DEFAULT 0x01
#define SUBLANG_ENGLISH_US 0x01
#define MAKELANGID(p, s) ((((WORD)(s)) << 10) | (WORD)(p))
#define PRIMARYLANGID(lgid) ((WORD)(lgid) & 0x3ff)
#define SUBLANGID(lgid) ((WORD)(lgid) >> 10)

// System message text is kernel32's English text (with its trailing "\r\n"); codes with
// no text fail with ERROR_MR_MID_NOT_FOUND as on Windows. FROM_STRING sources support the
// %1..%99 inserts with string arguments (va_list, or an array with ARGUMENT_ARRAY), and
// the %n %r %t %% %0 escapes. FROM_HMODULE (message tables) is not supported.
DWORD FormatMessageA(DWORD flags, LPCVOID source, DWORD messageId, DWORD languageId, LPSTR buffer, DWORD size,
                     va_list* arguments) noexcept;
DWORD FormatMessageW(DWORD flags, LPCVOID source, DWORD messageId, DWORD languageId, LPWSTR buffer, DWORD size,
                     va_list* arguments) noexcept;

// --- CRT errno ---------------------------------------------------------------------------
// The MSVC CRT spells the errno lvalue as `*_errno()`.
inline int* _errno() noexcept { return &errno; }
inline errno_t _get_errno(int* value) noexcept
{
  if (value == nullptr) {
    return EINVAL;
  }
  *value = errno;
  return 0;
}
inline errno_t _set_errno(int value) noexcept
{
  errno = value;
  return 0;
}
#ifndef STRUNCATE
#define STRUNCATE 80
#endif
