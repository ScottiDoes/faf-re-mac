// Win32 on POSIX: Errors (declarations in compat/Errors.h).

#include "WinCompatInternal.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace
{
  thread_local DWORD tLastError = ERROR_SUCCESS;

  struct SystemMessage
  {
    DWORD code;
    const char* text;
  };

  // kernel32's English message text for the codes this layer and the engine report.
  constexpr SystemMessage kSystemMessages[] = {
    {0, "The operation completed successfully.\r\n"},
    {1, "Incorrect function.\r\n"},
    {2, "The system cannot find the file specified.\r\n"},
    {3, "The system cannot find the path specified.\r\n"},
    {4, "The system cannot open the file.\r\n"},
    {5, "Access is denied.\r\n"},
    {6, "The handle is invalid.\r\n"},
    {8, "Not enough memory resources are available to process this command.\r\n"},
    {12, "The access code is invalid.\r\n"},
    {13, "The data is invalid.\r\n"},
    {14, "Not enough memory resources are available to complete this operation.\r\n"},
    {15, "The system cannot find the drive specified.\r\n"},
    {17, "The system cannot move the file to a different disk drive.\r\n"},
    {18, "There are no more files.\r\n"},
    {19, "The media is write protected.\r\n"},
    {21, "The device is not ready.\r\n"},
    {25, "The drive cannot locate a specific area or track on the disk.\r\n"},
    {29, "The system cannot write to the specified device.\r\n"},
    {30, "The system cannot read from the specified device.\r\n"},
    {31, "A device attached to the system is not functioning.\r\n"},
    {32, "The process cannot access the file because it is being used by another process.\r\n"},
    {33, "The process cannot access the file because another process has locked a portion of the file.\r\n"},
    {38, "Reached the end of the file.\r\n"},
    {39, "The disk is full.\r\n"},
    {50, "The request is not supported.\r\n"},
    {80, "The file exists.\r\n"},
    {82, "The directory or file cannot be created.\r\n"},
    {87, "The parameter is incorrect.\r\n"},
    {109, "The pipe has been ended.\r\n"},
    {110, "The system cannot open the device or file specified.\r\n"},
    {112, "There is not enough space on the disk.\r\n"},
    {120, "This function is not supported on this system.\r\n"},
    {121, "The semaphore timeout period has expired.\r\n"},
    {122, "The data area passed to a system call is too small.\r\n"},
    {123, "The filename, directory name, or volume label syntax is incorrect.\r\n"},
    {126, "The specified module could not be found.\r\n"},
    {127, "The specified procedure could not be found.\r\n"},
    {131, "An attempt was made to move the file pointer before the beginning of the file.\r\n"},
    {145, "The directory is not empty.\r\n"},
    {161, "The specified path is invalid.\r\n"},
    {170, "The requested resource is in use.\r\n"},
    {183, "Cannot create a file when that file already exists.\r\n"},
    {193, "%1 is not a valid Win32 application.\r\n"},
    {203, "The system could not find the environment option that was entered.\r\n"},
    {206, "The filename or extension is too long.\r\n"},
    {234, "More data is available.\r\n"},
    {258, "The wait operation timed out.\r\n"},
    {259, "No more data is available.\r\n"},
    {267, "The directory name is invalid.\r\n"},
    {288, "Attempt to release mutex not owned by caller.\r\n"},
    {298, "Too many posts were made to a semaphore.\r\n"},
    {299, "Only part of a ReadProcessMemory or WriteProcessMemory request was completed.\r\n"},
    {317, "The system cannot find message text for message number 0x%1 in the message file for %2.\r\n"},
    {487, "Attempt to access invalid address.\r\n"},
    {995, "The I/O operation has been aborted because of either a thread exit or an application request.\r\n"},
    {996, "Overlapped I/O event is not in a signaled state.\r\n"},
    {997, "Overlapped I/O operation is in progress.\r\n"},
    {998, "Invalid access to memory location.\r\n"},
    {1004, "Invalid flags.\r\n"},
    {1006, "The volume for a file has been externally altered so that the opened file is no longer valid.\r\n"},
    {1113, "No mapping for the Unicode character exists in the target multi-byte code page.\r\n"},
    {1168, "Element not found.\r\n"},
    {1223, "The operation was canceled by the user.\r\n"},
    {1300, "Not all privileges or groups referenced are assigned to the caller.\r\n"},
    {1314, "A required privilege is not held by the client.\r\n"},
    {1400, "Invalid window handle.\r\n"},
    {1460, "This operation returned because the timeout period expired.\r\n"},
    // Winsock (the Sockets area reports these through GetLastError/WSAGetLastError).
    {10004, "A blocking operation was interrupted by a call to WSACancelBlockingCall.\r\n"},
    {10009, "The file handle supplied is not valid.\r\n"},
    {10013, "An attempt was made to access a socket in a way forbidden by its access permissions.\r\n"},
    {10014, "The system detected an invalid pointer address in attempting to use a pointer argument in a call.\r\n"},
    {10022, "An invalid argument was supplied.\r\n"},
    {10024, "Too many open sockets.\r\n"},
    {10035, "A non-blocking socket operation could not be completed immediately.\r\n"},
    {10036, "A blocking operation is currently executing.\r\n"},
    {10037, "An operation was attempted on a non-blocking socket that already had an operation in progress.\r\n"},
    {10038, "An operation was attempted on something that is not a socket.\r\n"},
    {10039, "A required address was omitted from an operation on a socket.\r\n"},
    {10040, "A message sent on a datagram socket was larger than the internal message buffer or some other network "
            "limit, or the buffer used to receive a datagram into was smaller than the datagram itself.\r\n"},
    {10047, "An address incompatible with the requested protocol was used.\r\n"},
    {10048, "Only one usage of each socket address (protocol/network address/port) is normally permitted.\r\n"},
    {10049, "The requested address is not valid in its context.\r\n"},
    {10050, "A socket operation encountered a dead network.\r\n"},
    {10051, "A socket operation was attempted to an unreachable network.\r\n"},
    {10052, "The connection has been broken due to keep-alive activity detecting a failure while the operation was "
            "in progress.\r\n"},
    {10053, "An established connection was aborted by the software in your host machine.\r\n"},
    {10054, "An existing connection was forcibly closed by the remote host.\r\n"},
    {10055, "An operation on a socket could not be performed because the system lacked sufficient buffer space or "
            "because a queue was full.\r\n"},
    {10056, "A connect request was made on an already connected socket.\r\n"},
    {10057, "A request to send or receive data was disallowed because the socket is not connected and (when "
            "sending on a datagram socket using a sendto call) no address was supplied.\r\n"},
    {10058, "A request to send or receive data was disallowed because the socket had already been shut down in that "
            "direction with a previous shutdown call.\r\n"},
    {10060, "A connection attempt failed because the connected party did not properly respond after a period of "
            "time, or established connection failed because connected host has failed to respond.\r\n"},
    {10061, "No connection could be made because the target machine actively refused it.\r\n"},
    {10064, "A socket operation failed because the destination host was down.\r\n"},
    {10065, "A socket operation was attempted to an unreachable host.\r\n"},
    {10093, "Either the application has not called WSAStartup, or WSAStartup failed.\r\n"},
    {11001, "No such host is known.\r\n"},
    {11002, "This is usually a temporary error during hostname resolution and means that the local server did not "
            "receive a response from an authoritative server.\r\n"},
    {11004, "The requested name is valid, but no data of the requested type was found.\r\n"},
  };

  const char* FindSystemMessage(const DWORD code) noexcept
  {
    for (const SystemMessage& message : kSystemMessages) {
      if (message.code == code) {
        return message.text;
      }
    }
    return nullptr;
  }

  // One insert argument as text: string inserts only (%n or %n!s!/!S!); numeric formats
  // are rendered with the given printf spec from a pointer-sized argument.
  template <class Char>
  std::basic_string<Char> InsertText(const char* spec, const void* value)
  {
    std::basic_string<Char> out;
    if (spec[0] == 's' || spec[0] == 'S' || spec[0] == '\0') {
      if (value == nullptr) {
        return out;
      }
      if constexpr (sizeof(Char) == 1) {
        if (spec[0] == 'S') {
          return faf_posix::Utf8FromWide(static_cast<const wchar_t*>(value));
        }
        return std::string(static_cast<const char*>(value));
      } else {
        if (spec[0] == 'S') {
          return faf_posix::WideFromUtf8(static_cast<const char*>(value));
        }
        return std::wstring(static_cast<const wchar_t*>(value));
      }
    }
    char format[16] = "%";
    std::strncat(format, spec, sizeof(format) - 2);
    char text[64];
    std::snprintf(text, sizeof(text), format, static_cast<long long>(reinterpret_cast<std::intptr_t>(value)));
    for (const char* p = text; *p != '\0'; ++p) {
      out.push_back(static_cast<Char>(*p));
    }
    return out;
  }

  template <class Char>
  std::basic_string<Char> Widen(const char* text)
  {
    if constexpr (sizeof(Char) == 1) {
      return std::string(text);
    } else {
      return faf_posix::WideFromUtf8(text);
    }
  }

  // The FormatMessage engine for both character widths.
  template <class Char>
  DWORD FormatMessageImpl(const DWORD flags, const void* const source, const DWORD messageId, Char* const buffer,
                          const DWORD size, va_list* const arguments)
  {
    std::basic_string<Char> pattern;
    if ((flags & FORMAT_MESSAGE_FROM_STRING) != 0) {
      if (source == nullptr) {
        ::SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
      }
      pattern = static_cast<const Char*>(source);
    } else if ((flags & FORMAT_MESSAGE_FROM_SYSTEM) != 0) {
      const char* const text = FindSystemMessage(messageId);
      if (text == nullptr) {
        ::SetLastError(ERROR_MR_MID_NOT_FOUND);
        return 0;
      }
      pattern = Widen<Char>(text);
    } else {
      FAF_POSIX_STUB("FormatMessage(FORMAT_MESSAGE_FROM_HMODULE): no message tables on POSIX");
      ::SetLastError(ERROR_RESOURCE_TYPE_NOT_FOUND);
      return 0;
    }

    const DWORD maxWidth = flags & FORMAT_MESSAGE_MAX_WIDTH_MASK;
    const bool ignoreInserts = (flags & FORMAT_MESSAGE_IGNORE_INSERTS) != 0 || arguments == nullptr;
    const bool argumentArray = (flags & FORMAT_MESSAGE_ARGUMENT_ARRAY) != 0;

    // Arguments are pulled in insert-number order (Windows walks the va_list the same way).
    std::vector<const void*> fetched;
    auto argument = [&](const unsigned index) -> const void* {
      if (argumentArray) {
        return reinterpret_cast<const void*>(reinterpret_cast<const DWORD_PTR*>(arguments)[index - 1]);
      }
      while (fetched.size() < index) {
        fetched.push_back(va_arg(*arguments, const void*));
      }
      return fetched[index - 1];
    };

    std::basic_string<Char> out;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
      const Char c = pattern[i];
      if (maxWidth != 0 && (c == '\r' || c == '\n')) {
        // With a width limit the definition's own line breaks become spaces.
        if (c == '\r' && i + 1 < pattern.size() && pattern[i + 1] == '\n') {
          ++i;
        }
        out.push_back(static_cast<Char>(' '));
        continue;
      }
      if (c != '%' || i + 1 >= pattern.size()) {
        out.push_back(c);
        continue;
      }
      const Char next = pattern[i + 1];
      if (next == '0') {
        break; // end of message, no trailing newline
      }
      if (next == 'n') {
        out.push_back(static_cast<Char>('\r'));
        out.push_back(static_cast<Char>('\n'));
        ++i;
        continue;
      }
      if (next == 'r') {
        out.push_back(static_cast<Char>('\r'));
        ++i;
        continue;
      }
      if (next == 't') {
        out.push_back(static_cast<Char>('\t'));
        ++i;
        continue;
      }
      if (next >= '1' && next <= '9') {
        std::size_t j = i + 1;
        unsigned index = 0;
        while (j < pattern.size() && pattern[j] >= '0' && pattern[j] <= '9' && index < 10) {
          index = index * 10 + static_cast<unsigned>(pattern[j] - '0');
          ++j;
        }
        char spec[16] = "s";
        std::size_t specEnd = j;
        if (j < pattern.size() && pattern[j] == '!') {
          std::size_t k = j + 1;
          std::size_t n = 0;
          while (k < pattern.size() && pattern[k] != '!' && n + 1 < sizeof(spec)) {
            spec[n++] = static_cast<char>(pattern[k++]);
          }
          spec[n] = '\0';
          specEnd = (k < pattern.size()) ? k + 1 : k;
        }
        if (ignoreInserts) {
          out.append(pattern, i, specEnd - i);
        } else {
          out += InsertText<Char>(spec, argument(index));
        }
        i = specEnd - 1;
        continue;
      }
      // "%%" and any other escaped character: the character itself.
      out.push_back(next);
      ++i;
    }

    const std::size_t length = out.size();
    if ((flags & FORMAT_MESSAGE_ALLOCATE_BUFFER) != 0) {
      const std::size_t bytes = (length + 1 > size ? length + 1 : size) * sizeof(Char);
      auto* const allocated = static_cast<Char*>(::LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, bytes));
      if (allocated == nullptr) {
        ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
      }
      std::memcpy(allocated, out.c_str(), (length + 1) * sizeof(Char));
      *reinterpret_cast<Char**>(buffer) = allocated;
      return static_cast<DWORD>(length);
    }
    if (buffer == nullptr || length + 1 > size) {
      ::SetLastError(ERROR_INSUFFICIENT_BUFFER);
      return 0;
    }
    std::memcpy(buffer, out.c_str(), (length + 1) * sizeof(Char));
    return static_cast<DWORD>(length);
  }
} // namespace

extern "C" void faf_posixReportStub(const char* const what, const char* const file, const int line) noexcept
{
  const char* const base = (file != nullptr && std::strrchr(file, '/') != nullptr) ? std::strrchr(file, '/') + 1 : file;
  std::fprintf(stderr, "[faf-posix] stub: %s (%s:%d)\n", what != nullptr ? what : "?", base != nullptr ? base : "?", line);
}

DWORD GetLastError() noexcept
{
  return tLastError;
}

void SetLastError(const DWORD code) noexcept
{
  tLastError = code;
}

void SetLastErrorEx(const DWORD code, const DWORD type) noexcept
{
  (void)type;
  tLastError = code;
}

DWORD FormatMessageA(const DWORD flags, LPCVOID const source, const DWORD messageId, const DWORD languageId,
                     LPSTR const buffer, const DWORD size, va_list* const arguments) noexcept
{
  (void)languageId;
  try {
    return FormatMessageImpl<char>(flags, source, messageId, buffer, size, arguments);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
}

DWORD FormatMessageW(const DWORD flags, LPCVOID const source, const DWORD messageId, const DWORD languageId,
                     LPWSTR const buffer, const DWORD size, va_list* const arguments) noexcept
{
  (void)languageId;
  try {
    return FormatMessageImpl<wchar_t>(flags, source, messageId, buffer, size, arguments);
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
}

namespace faf_posix
{
  DWORD Win32ErrorFromErrno(const int error) noexcept
  {
    switch (error) {
      case 0:
        return ERROR_SUCCESS;
      case ENOENT:
        return ERROR_FILE_NOT_FOUND;
      case ENOTDIR:
        return ERROR_PATH_NOT_FOUND;
      case EACCES:
      case EPERM:
      case EROFS:
        return ERROR_ACCESS_DENIED;
      case EISDIR:
        return ERROR_ACCESS_DENIED; // CreateFile on a directory without BACKUP_SEMANTICS
      case EEXIST:
        return ERROR_ALREADY_EXISTS;
      case ENOTEMPTY:
        return ERROR_DIR_NOT_EMPTY;
      case EBADF:
        return ERROR_INVALID_HANDLE;
      case ENOMEM:
        return ERROR_NOT_ENOUGH_MEMORY;
      case EINVAL:
        return ERROR_INVALID_PARAMETER;
      case EMFILE:
      case ENFILE:
        return ERROR_TOO_MANY_OPEN_FILES;
      case ENOSPC:
        return ERROR_DISK_FULL;
      case ENAMETOOLONG:
        return ERROR_FILENAME_EXCED_RANGE;
      case EXDEV:
        return ERROR_NOT_SAME_DEVICE;
      case EBUSY:
        return ERROR_BUSY;
      case ETXTBSY:
        return ERROR_SHARING_VIOLATION;
      case EAGAIN:
        return ERROR_LOCK_VIOLATION;
      case EPIPE:
        return ERROR_BROKEN_PIPE;
      case ESPIPE:
        return ERROR_SEEK;
      case EIO:
        return ERROR_GEN_FAILURE;
      case ETIMEDOUT:
        return ERROR_TIMEOUT;
      case ENOSYS:
      case ENOTSUP:
        return ERROR_NOT_SUPPORTED;
      case EFAULT:
        return ERROR_NOACCESS;
      case ELOOP:
        return ERROR_CANT_RESOLVE_FILENAME;
      case EILSEQ:
        return ERROR_NO_UNICODE_TRANSLATION;
      case ERANGE:
        return ERROR_INSUFFICIENT_BUFFER;
      default:
        return ERROR_GEN_FAILURE;
    }
  }

  void SetLastErrorFromErrno(const int error) noexcept
  {
    ::SetLastError(Win32ErrorFromErrno(error));
  }
} // namespace faf_posix
