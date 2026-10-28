#pragma once

// Win32 on POSIX: Time. Included only by ../WinCompat.h (see its rules).
// Implementations: ../WinCompatTime.cpp.
//
// Clocks: QueryPerformanceCounter is mach_absolute_time (frequency from the timebase,
// 24 MHz on Apple Silicon); GetTickCount/GetTickCount64/timeGetTime are milliseconds of
// CLOCK_MONOTONIC (which, like Windows' tick count, keeps counting through sleep).
// FILETIME is 100 ns since 1601-01-01 UTC; "local" conversions use the current zone bias,
// as FileTimeToLocalFileTime documents. None of these clocks may feed the simulation.

#include <ctime>

BOOL QueryPerformanceCounter(LARGE_INTEGER* count) noexcept;
BOOL QueryPerformanceFrequency(LARGE_INTEGER* frequency) noexcept;
DWORD GetTickCount() noexcept;
ULONGLONG GetTickCount64() noexcept;

// <mmsystem.h>
typedef UINT MMRESULT;
#define TIMERR_NOERROR 0
#define TIMERR_NOCANDO 97
typedef struct timecaps_tag {
  UINT wPeriodMin;
  UINT wPeriodMax;
} TIMECAPS, *PTIMECAPS, *LPTIMECAPS;
DWORD timeGetTime() noexcept;
MMRESULT timeBeginPeriod(UINT period) noexcept;
MMRESULT timeEndPeriod(UINT period) noexcept;
MMRESULT timeGetDevCaps(LPTIMECAPS caps, UINT size) noexcept;

void GetSystemTimeAsFileTime(LPFILETIME time) noexcept;
void GetSystemTimePreciseAsFileTime(LPFILETIME time) noexcept;
void GetSystemTime(LPSYSTEMTIME time) noexcept;
void GetLocalTime(LPSYSTEMTIME time) noexcept;
BOOL FileTimeToSystemTime(const FILETIME* fileTime, LPSYSTEMTIME systemTime) noexcept;
BOOL SystemTimeToFileTime(const SYSTEMTIME* systemTime, LPFILETIME fileTime) noexcept;
BOOL FileTimeToLocalFileTime(const FILETIME* fileTime, LPFILETIME localFileTime) noexcept;
BOOL LocalFileTimeToFileTime(const FILETIME* localFileTime, LPFILETIME fileTime) noexcept;
LONG CompareFileTime(const FILETIME* first, const FILETIME* second) noexcept;
// MS-DOS date/time (FAT, zip): converted without a zone, as Windows does.
BOOL DosDateTimeToFileTime(WORD fatDate, WORD fatTime, LPFILETIME fileTime) noexcept;
BOOL FileTimeToDosDateTime(const FILETIME* fileTime, LPWORD fatDate, LPWORD fatTime) noexcept;

// --- CRT time (<time.h>, <sys/timeb.h>) --------------------------------------------------------
// The MSVC argument order for the *_s variants: (destination, source).
__time64_t _time64(__time64_t* destination) noexcept;
__time64_t _mktime64(struct tm* time) noexcept;
__time64_t _mkgmtime64(struct tm* time) noexcept;
struct tm* _localtime64(const __time64_t* time) noexcept;
struct tm* _gmtime64(const __time64_t* time) noexcept;
errno_t _localtime64_s(struct tm* destination, const __time64_t* time) noexcept;
errno_t _gmtime64_s(struct tm* destination, const __time64_t* time) noexcept;
errno_t localtime_s(struct tm* destination, const time_t* time) noexcept;
errno_t gmtime_s(struct tm* destination, const time_t* time) noexcept;
double _difftime64(__time64_t end, __time64_t start) noexcept;
// The UCRT's clock(): wall-clock milliseconds since the process started (CLOCKS_PER_SEC is
// 1000 there), as a 32-bit clock_t; -1 once that no longer fits. POSIX clock() is CPU time in
// microseconds instead, so engine code that wants the Windows value calls this.
std::int32_t faf_posixClock() noexcept;
char* _ctime64(const __time64_t* time) noexcept;
errno_t _ctime64_s(char* buffer, std::size_t size, const __time64_t* time) noexcept;
errno_t ctime_s(char* buffer, std::size_t size, const time_t* time) noexcept;
errno_t asctime_s(char* buffer, std::size_t size, const struct tm* time) noexcept;

struct __timeb64 {
  __time64_t time;
  unsigned short millitm;
  short timezone;
  short dstflag;
};
#define _timeb __timeb64
void _ftime64(struct __timeb64* time) noexcept;
errno_t _ftime64_s(struct __timeb64* time) noexcept;
#define _ftime _ftime64
#define _ftime_s _ftime64_s
