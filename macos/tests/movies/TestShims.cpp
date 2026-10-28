// The few engine symbols the movie tests' TUs reference outside the SFD player, the
// FFmpeg decoder, the DirectSound sink and the DirectSound stand-in: logging and two
// Win32 calls, printed to stderr. (The tests link those TUs, not the whole engine.)

#include "gpg/core/utils/Logging.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace
{
  void Print(const char* tag, const char* fmt, va_list args)
  {
    std::fprintf(stderr, "%s", tag);
    std::vfprintf(stderr, fmt, args);
    std::fprintf(stderr, "\n");
  }
} // namespace

void gpg::Logf(const char* fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  Print("info: ", fmt, args);
  va_end(args);
}

void gpg::Warnf(const char* fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  Print("warning: ", fmt, args);
  va_end(args);
}

void gpg::Debugf(const char* fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  Print("debug: ", fmt, args);
  va_end(args);
}

void OutputDebugStringA(LPCSTR text) noexcept
{
  std::fprintf(stderr, "%s", text ? text : "");
}

HWND GetDesktopWindow() noexcept
{
  return nullptr;
}

extern "C" void faf_posixReportStub(const char* what, const char* file, int line) noexcept
{
  std::fprintf(stderr, "FAF_POSIX_STUB: %s (%s:%d)\n", what, file, line);
}

// The tests pass host paths already.
std::size_t faf_posixHostPathA(const char* windowsPath, char* buffer, std::size_t capacity) noexcept
{
  if (windowsPath == nullptr) {
    return 0;
  }
  const std::size_t length = std::strlen(windowsPath);
  if (length + 1 > capacity) {
    return length + 1;
  }
  std::memcpy(buffer, windowsPath, length + 1);
  return length;
}
