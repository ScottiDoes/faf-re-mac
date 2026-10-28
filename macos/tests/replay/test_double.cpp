// Test double for what the runner's ReplayReport.cpp, ReplayChecks.cpp and FpcrWatch.cpp take
// from the engine: the gpg log (targets, Logf, Warnf), the crt trace dump, and the CRT's
// memcpy_s that msvc8::string (legacy/containers/String.cpp) copies with. The compat layer's
// definitions live in objects the unit test does not link.
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "gpg/core/utils/Logging.h"

std::vector<gpg::LogTarget*> gTestTargets;
std::vector<std::string> gTestLogLines;
std::vector<std::string> gTestWarnLines;

gpg::LogTarget::LogTarget(const bool autoRegister)
  : mNode(nullptr)
{
  if (autoRegister) {
    gTestTargets.push_back(this);
  }
}

gpg::LogTarget::~LogTarget() {}

void gpg::LogTarget::OnLog(int, const msvc8::string&, std::span<const msvc8::string>, int) {}

void gpg::Logf(const char* fmt, ...)
{
  char buffer[1024];
  va_list va;
  va_start(va, fmt);
  std::vsnprintf(buffer, sizeof(buffer), fmt, va);
  va_end(va);
  gTestLogLines.emplace_back(buffer);
}

void gpg::Warnf(const char* fmt, ...)
{
  char buffer[1024];
  va_list va;
  va_start(va, fmt);
  std::vsnprintf(buffer, sizeof(buffer), fmt, va);
  va_end(va);
  gTestWarnLines.emplace_back(buffer);
}

extern "C" bool faf_crtmathTraceDump() noexcept
{
  return false;
}

errno_t memcpy_s(void* dst, std::size_t dstSize, const void* src, std::size_t count) noexcept
{
  if (count > dstSize) {
    return 34;
  }
  std::memcpy(dst, src, count);
  return 0;
}
