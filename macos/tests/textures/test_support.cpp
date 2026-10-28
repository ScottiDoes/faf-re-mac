// Stand-ins for the few engine symbols the texture codec uses outside its own files, so the
// textures tests link without the whole engine: gpg logging (to stderr), the shader cache
// directory (FAF_TEST_CACHE_DIR, empty = no decode cache), the window system (none).

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>


namespace gpg
{
  namespace
  {
    void Print(const char* const level, const char* const format, va_list arguments)
    {
      if (std::getenv("FAF_TEST_QUIET") != nullptr && level[0] == 'i') {
        return;
      }
      std::fprintf(stderr, "%s: ", level);
      std::vfprintf(stderr, format, arguments);
      std::fputc('\n', stderr);
    }
  } // namespace

  void Logf(const char* const format, ...)
  {
    va_list arguments;
    va_start(arguments, format);
    Print("info", format, arguments);
    va_end(arguments);
  }

  void Warnf(const char* const format, ...)
  {
    va_list arguments;
    va_start(arguments, format);
    Print("warning", format, arguments);
    va_end(arguments);
  }

  void Debugf(const char* const format, ...)
  {
    va_list arguments;
    va_start(arguments, format);
    Print("debug", format, arguments);
    va_end(arguments);
  }
} // namespace gpg

namespace gpg::gal::effects
{
  const std::string& ShaderCacheDirectory()
  {
    static const std::string directory = [] {
      const char* const configured = std::getenv("FAF_TEST_CACHE_DIR");
      std::string path = (configured != nullptr) ? configured : "";
      if (!path.empty() && path.back() != '/' && path.back() != '\\') {
        path += '/';
      }
      return path;
    }();
    return directory;
  }
} // namespace gpg::gal::effects

// The window system (shell/Shell.cpp, SDL3) as a headless run sees it: the compat Sync area's
// MsgWaitForMultipleObjects asks it. Weak, so a test that links the real shell gets that.
namespace faf_posix::shell
{
  __attribute__((weak)) bool IsActive() noexcept
  {
    return false;
  }

  __attribute__((weak)) bool IsMainThread() noexcept
  {
    return true;
  }

  __attribute__((weak)) bool InputAvailable()
  {
    return false;
  }
} // namespace faf_posix::shell
