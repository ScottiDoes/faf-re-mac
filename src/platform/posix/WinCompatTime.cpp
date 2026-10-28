// Win32 on POSIX: Time (declarations in compat/Time.h).

#include "WinCompatInternal.h"

#include <cstdio>
#include <cstring>

#include <mach/mach_time.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

using namespace faf_posix;

namespace
{
  constexpr std::uint64_t kTicksPerSecond = 10000000ull; // FILETIME units
  constexpr std::uint64_t kMaxFileTime = 0x7FFFFFFFFFFFFFFFull;

  std::int64_t CurrentZoneBiasSeconds() noexcept
  {
    const time_t now = time(nullptr);
    struct tm local{};
    localtime_r(&now, &local);
    return static_cast<std::int64_t>(local.tm_gmtoff);
  }

  FILETIME NowFileTime() noexcept
  {
    timespec now{};
    clock_gettime(CLOCK_REALTIME, &now);
    return FileTimeFromUnix(now.tv_sec, now.tv_nsec);
  }

  void SystemTimeFromTm(const struct tm& time, const WORD milliseconds, LPSYSTEMTIME const out) noexcept
  {
    out->wYear = static_cast<WORD>(time.tm_year + 1900);
    out->wMonth = static_cast<WORD>(time.tm_mon + 1);
    out->wDayOfWeek = static_cast<WORD>(time.tm_wday);
    out->wDay = static_cast<WORD>(time.tm_mday);
    out->wHour = static_cast<WORD>(time.tm_hour);
    out->wMinute = static_cast<WORD>(time.tm_min);
    out->wSecond = static_cast<WORD>(time.tm_sec);
    out->wMilliseconds = milliseconds;
  }

  // Seconds since the Unix epoch of a FILETIME (floor), and the remaining 100 ns ticks.
  void SplitFileTime(const std::uint64_t value, std::int64_t* const seconds, std::uint64_t* const ticks) noexcept
  {
    const std::uint64_t total = value / kTicksPerSecond;
    *ticks = value % kTicksPerSecond;
    *seconds = static_cast<std::int64_t>(total) - static_cast<std::int64_t>(kFileTimeUnixEpoch / kTicksPerSecond);
  }

  bool ValidCalendar(const int year, const int month, const int day, const int hour, const int minute,
                     const int second, const int milliseconds) noexcept
  {
    static constexpr int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (year < 1601 || year > 30827 || month < 1 || month > 12 || day < 1 || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 || second < 0 || second > 59 || milliseconds < 0 || milliseconds > 999) {
      return false;
    }
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    const int days = kDays[month - 1] + ((month == 2 && leap) ? 1 : 0);
    return day <= days;
  }

  // Unix seconds of a broken-down UTC calendar time (proleptic Gregorian, any year).
  std::int64_t UnixFromCalendar(const int year, const int month, const int day, const int hour, const int minute,
                                const int second) noexcept
  {
    // Days from civil (Howard Hinnant's algorithm).
    const int y = year - (month <= 2 ? 1 : 0);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned mp = static_cast<unsigned>((month + 9) % 12);
    const unsigned doy = (153 * mp + 2) / 5 + static_cast<unsigned>(day) - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const std::int64_t days = static_cast<std::int64_t>(era) * 146097 + static_cast<std::int64_t>(doe) - 719468;
    return days * 86400 + hour * 3600 + minute * 60 + second;
  }
} // namespace

namespace faf_posix
{
  FILETIME FileTimeFromUnix(const std::int64_t seconds, const std::int64_t nanoseconds) noexcept
  {
    const std::int64_t ticks = seconds * static_cast<std::int64_t>(kTicksPerSecond) + nanoseconds / 100 +
                               static_cast<std::int64_t>(kFileTimeUnixEpoch);
    return FileTimeFromU64(ticks < 0 ? 0 : static_cast<std::uint64_t>(ticks));
  }

  std::uint64_t FileTimeToU64(const FILETIME& time) noexcept
  {
    return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
  }

  FILETIME FileTimeFromU64(const std::uint64_t value) noexcept
  {
    FILETIME time;
    time.dwLowDateTime = static_cast<DWORD>(value & 0xFFFFFFFFu);
    time.dwHighDateTime = static_cast<DWORD>(value >> 32);
    return time;
  }
} // namespace faf_posix

// --- Clocks ----------------------------------------------------------------------------------

BOOL QueryPerformanceCounter(LARGE_INTEGER* const count) noexcept
{
  if (count == nullptr) {
    ::SetLastError(ERROR_NOACCESS);
    return FALSE;
  }
  count->QuadPart = static_cast<LONGLONG>(mach_absolute_time());
  return TRUE;
}

BOOL QueryPerformanceFrequency(LARGE_INTEGER* const frequency) noexcept
{
  if (frequency == nullptr) {
    ::SetLastError(ERROR_NOACCESS);
    return FALSE;
  }
  static const LONGLONG ticksPerSecond = [] {
    mach_timebase_info_data_t timebase{};
    mach_timebase_info(&timebase);
    if (timebase.numer == 0) {
      return LONGLONG{1000000000};
    }
    // ticks * numer / denom = ns, so one second is 1e9 * denom / numer ticks.
    return static_cast<LONGLONG>(1000000000ull * timebase.denom / timebase.numer);
  }();
  frequency->QuadPart = ticksPerSecond;
  return TRUE;
}

ULONGLONG GetTickCount64() noexcept
{
  return clock_gettime_nsec_np(CLOCK_MONOTONIC) / 1000000ull;
}

DWORD GetTickCount() noexcept
{
  return static_cast<DWORD>(GetTickCount64());
}

DWORD timeGetTime() noexcept
{
  return static_cast<DWORD>(GetTickCount64());
}

MMRESULT timeBeginPeriod(const UINT period) noexcept
{
  return period == 0 ? TIMERR_NOCANDO : TIMERR_NOERROR; // sleeps are already precise on macOS
}

MMRESULT timeEndPeriod(const UINT period) noexcept
{
  return period == 0 ? TIMERR_NOCANDO : TIMERR_NOERROR;
}

MMRESULT timeGetDevCaps(LPTIMECAPS const caps, const UINT size) noexcept
{
  if (caps == nullptr || size < sizeof(TIMECAPS)) {
    return TIMERR_NOCANDO;
  }
  caps->wPeriodMin = 1;
  caps->wPeriodMax = 1000000;
  return TIMERR_NOERROR;
}

// --- FILETIME / SYSTEMTIME ----------------------------------------------------------------------

void GetSystemTimeAsFileTime(LPFILETIME const time) noexcept
{
  if (time != nullptr) {
    *time = NowFileTime();
  }
}

void GetSystemTimePreciseAsFileTime(LPFILETIME const time) noexcept
{
  GetSystemTimeAsFileTime(time);
}

void GetSystemTime(LPSYSTEMTIME const time) noexcept
{
  if (time == nullptr) {
    return;
  }
  const FILETIME now = NowFileTime();
  (void)FileTimeToSystemTime(&now, time);
}

void GetLocalTime(LPSYSTEMTIME const time) noexcept
{
  if (time == nullptr) {
    return;
  }
  timespec now{};
  clock_gettime(CLOCK_REALTIME, &now);
  struct tm local{};
  localtime_r(&now.tv_sec, &local);
  SystemTimeFromTm(local, static_cast<WORD>(now.tv_nsec / 1000000L), time);
}

BOOL FileTimeToSystemTime(const FILETIME* const fileTime, LPSYSTEMTIME const systemTime) noexcept
{
  if (fileTime == nullptr || systemTime == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  const std::uint64_t value = FileTimeToU64(*fileTime);
  if (value > kMaxFileTime) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  std::int64_t seconds = 0;
  std::uint64_t ticks = 0;
  SplitFileTime(value, &seconds, &ticks);
  const time_t unixSeconds = static_cast<time_t>(seconds);
  struct tm utc{};
  if (gmtime_r(&unixSeconds, &utc) == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  SystemTimeFromTm(utc, static_cast<WORD>(ticks / 10000u), systemTime);
  return TRUE;
}

BOOL SystemTimeToFileTime(const SYSTEMTIME* const systemTime, LPFILETIME const fileTime) noexcept
{
  if (systemTime == nullptr || fileTime == nullptr ||
      !ValidCalendar(systemTime->wYear, systemTime->wMonth, systemTime->wDay, systemTime->wHour,
                     systemTime->wMinute, systemTime->wSecond, systemTime->wMilliseconds)) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  const std::int64_t seconds = UnixFromCalendar(systemTime->wYear, systemTime->wMonth, systemTime->wDay,
                                                systemTime->wHour, systemTime->wMinute, systemTime->wSecond);
  *fileTime = FileTimeFromUnix(seconds, static_cast<std::int64_t>(systemTime->wMilliseconds) * 1000000);
  return TRUE;
}

BOOL FileTimeToLocalFileTime(const FILETIME* const fileTime, LPFILETIME const localFileTime) noexcept
{
  if (fileTime == nullptr || localFileTime == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  const std::int64_t value = static_cast<std::int64_t>(FileTimeToU64(*fileTime)) +
                             CurrentZoneBiasSeconds() * static_cast<std::int64_t>(kTicksPerSecond);
  *localFileTime = FileTimeFromU64(value < 0 ? 0 : static_cast<std::uint64_t>(value));
  return TRUE;
}

BOOL LocalFileTimeToFileTime(const FILETIME* const localFileTime, LPFILETIME const fileTime) noexcept
{
  if (fileTime == nullptr || localFileTime == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  const std::int64_t value = static_cast<std::int64_t>(FileTimeToU64(*localFileTime)) -
                             CurrentZoneBiasSeconds() * static_cast<std::int64_t>(kTicksPerSecond);
  *fileTime = FileTimeFromU64(value < 0 ? 0 : static_cast<std::uint64_t>(value));
  return TRUE;
}

LONG CompareFileTime(const FILETIME* const first, const FILETIME* const second) noexcept
{
  const std::uint64_t a = FileTimeToU64(*first);
  const std::uint64_t b = FileTimeToU64(*second);
  return a < b ? -1 : (a > b ? 1 : 0);
}

BOOL DosDateTimeToFileTime(const WORD fatDate, const WORD fatTime, LPFILETIME const fileTime) noexcept
{
  const int day = fatDate & 0x1F;
  const int month = (fatDate >> 5) & 0x0F;
  const int year = 1980 + ((fatDate >> 9) & 0x7F);
  const int second = (fatTime & 0x1F) * 2;
  const int minute = (fatTime >> 5) & 0x3F;
  const int hour = (fatTime >> 11) & 0x1F;
  if (fileTime == nullptr || !ValidCalendar(year, month, day, hour, minute, second, 0)) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  *fileTime = FileTimeFromUnix(UnixFromCalendar(year, month, day, hour, minute, second), 0);
  return TRUE;
}

BOOL FileTimeToDosDateTime(const FILETIME* const fileTime, LPWORD const fatDate, LPWORD const fatTime) noexcept
{
  SYSTEMTIME time{};
  if (fatDate == nullptr || fatTime == nullptr || !FileTimeToSystemTime(fileTime, &time) || time.wYear < 1980 ||
      time.wYear > 2107) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  *fatDate = static_cast<WORD>(((time.wYear - 1980) << 9) | (time.wMonth << 5) | time.wDay);
  *fatTime = static_cast<WORD>((time.wHour << 11) | (time.wMinute << 5) | (time.wSecond / 2));
  return TRUE;
}

// --- CRT time ---------------------------------------------------------------------------------

std::int32_t faf_posixClock() noexcept
{
  // The UCRT samples its start count while the CRT initializes. The kernel's record of when
  // the process started is the closest equivalent; it is wall time, so it is read once and
  // the monotonic clock carries on from there.
  struct Origin
  {
    std::uint64_t machAtFirstCall = 0;
    long long elapsedMsAtFirstCall = 0;
    mach_timebase_info_data_t timebase{};
  };
  static const Origin origin = [] {
    Origin o{};
    o.machAtFirstCall = mach_absolute_time();
    mach_timebase_info(&o.timebase);
    struct kinfo_proc info{};
    std::size_t size = sizeof(info);
    int name[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, static_cast<int>(getpid())};
    struct timeval now{};
    gettimeofday(&now, nullptr);
    if (sysctl(name, 4, &info, &size, nullptr, 0) == 0 && size == sizeof(info)) {
      const struct timeval start = info.kp_proc.p_starttime;
      const long long elapsedUs = (static_cast<long long>(now.tv_sec) - start.tv_sec) * 1000000ll +
                                  (static_cast<long long>(now.tv_usec) - start.tv_usec);
      o.elapsedMsAtFirstCall = elapsedUs > 0 ? elapsedUs / 1000 : 0;
    }
    return o;
  }();
  const std::uint64_t ticks = mach_absolute_time() - origin.machAtFirstCall;
  const unsigned __int128 ns = static_cast<unsigned __int128>(ticks) * origin.timebase.numer / origin.timebase.denom;
  const long long result = origin.elapsedMsAtFirstCall + static_cast<long long>(ns / 1000000u);
  if (result > 0x7FFFFFFFll) {
    return -1;
  }
  return static_cast<std::int32_t>(result);
}

__time64_t _time64(__time64_t* const destination) noexcept
{
  const __time64_t now = static_cast<__time64_t>(time(nullptr));
  if (destination != nullptr) {
    *destination = now;
  }
  return now;
}

__time64_t _mktime64(struct tm* const time) noexcept
{
  if (time == nullptr) {
    errno = EINVAL;
    return -1;
  }
  const time_t result = mktime(time);
  if (result < 0) { // the CRT's range starts at the epoch
    errno = EINVAL;
    return -1;
  }
  return static_cast<__time64_t>(result);
}

__time64_t _mkgmtime64(struct tm* const time) noexcept
{
  if (time == nullptr) {
    errno = EINVAL;
    return -1;
  }
  const time_t result = timegm(time);
  if (result < 0) {
    errno = EINVAL;
    return -1;
  }
  return static_cast<__time64_t>(result);
}

struct tm* _localtime64(const __time64_t* const time) noexcept
{
  static thread_local struct tm result{};
  if (time == nullptr || *time < 0) {
    errno = EINVAL;
    return nullptr;
  }
  const time_t value = static_cast<time_t>(*time);
  return localtime_r(&value, &result);
}

struct tm* _gmtime64(const __time64_t* const time) noexcept
{
  static thread_local struct tm result{};
  if (time == nullptr || *time < 0) {
    errno = EINVAL;
    return nullptr;
  }
  const time_t value = static_cast<time_t>(*time);
  return gmtime_r(&value, &result);
}

errno_t _localtime64_s(struct tm* const destination, const __time64_t* const time) noexcept
{
  if (destination == nullptr) {
    return EINVAL;
  }
  if (time == nullptr || *time < 0) {
    std::memset(destination, 0xFF, sizeof(*destination));
    return EINVAL;
  }
  const time_t value = static_cast<time_t>(*time);
  return localtime_r(&value, destination) != nullptr ? 0 : EINVAL;
}

errno_t _gmtime64_s(struct tm* const destination, const __time64_t* const time) noexcept
{
  if (destination == nullptr) {
    return EINVAL;
  }
  if (time == nullptr || *time < 0) {
    std::memset(destination, 0xFF, sizeof(*destination));
    return EINVAL;
  }
  const time_t value = static_cast<time_t>(*time);
  return gmtime_r(&value, destination) != nullptr ? 0 : EINVAL;
}

errno_t localtime_s(struct tm* const destination, const time_t* const time) noexcept
{
  if (time == nullptr) {
    return _localtime64_s(destination, nullptr);
  }
  const __time64_t value = static_cast<__time64_t>(*time);
  return _localtime64_s(destination, &value);
}

errno_t gmtime_s(struct tm* const destination, const time_t* const time) noexcept
{
  if (time == nullptr) {
    return _gmtime64_s(destination, nullptr);
  }
  const __time64_t value = static_cast<__time64_t>(*time);
  return _gmtime64_s(destination, &value);
}

double _difftime64(const __time64_t end, const __time64_t start) noexcept
{
  return static_cast<double>(end - start);
}

char* _ctime64(const __time64_t* const time) noexcept
{
  static thread_local char buffer[26];
  return _ctime64_s(buffer, sizeof(buffer), time) == 0 ? buffer : nullptr;
}

errno_t _ctime64_s(char* const buffer, const std::size_t size, const __time64_t* const time) noexcept
{
  if (buffer == nullptr || size < 26) {
    return EINVAL;
  }
  struct tm local{};
  if (_localtime64_s(&local, time) != 0) {
    buffer[0] = '\0';
    return EINVAL;
  }
  return asctime_s(buffer, size, &local);
}

errno_t ctime_s(char* const buffer, const std::size_t size, const time_t* const time) noexcept
{
  if (time == nullptr) {
    return EINVAL;
  }
  const __time64_t value = static_cast<__time64_t>(*time);
  return _ctime64_s(buffer, size, &value);
}

errno_t asctime_s(char* const buffer, const std::size_t size, const struct tm* const time) noexcept
{
  if (buffer == nullptr || size < 26 || time == nullptr) {
    return EINVAL;
  }
  char scratch[64];
  if (asctime_r(time, scratch) == nullptr) {
    buffer[0] = '\0';
    return EINVAL;
  }
  std::strncpy(buffer, scratch, size - 1);
  buffer[size - 1] = '\0';
  return 0;
}

void _ftime64(struct __timeb64* const time) noexcept
{
  (void)_ftime64_s(time);
}

errno_t _ftime64_s(struct __timeb64* const time) noexcept
{
  if (time == nullptr) {
    return EINVAL;
  }
  timespec now{};
  clock_gettime(CLOCK_REALTIME, &now);
  time->time = static_cast<__time64_t>(now.tv_sec);
  time->millitm = static_cast<unsigned short>(now.tv_nsec / 1000000L);
  struct tm local{};
  localtime_r(&now.tv_sec, &local);
  // Minutes west of UTC, excluding daylight saving (the CRT's _timezone / 60).
  const long dstAdjust = local.tm_isdst > 0 ? 3600 : 0;
  time->timezone = static_cast<short>(-(local.tm_gmtoff - dstAdjust) / 60);
  time->dstflag = static_cast<short>(local.tm_isdst > 0 ? 1 : 0);
  return 0;
}
