// The headless replay runner's result: counts the sim's checksum mismatches as they are
// logged, tracks the last beat the user side applied, and prints the one-line result.
//
// The sim compares its own beat checksum with each player's recorded one in
// Sim::VerifyChecksum (moho/sim/Sim.cpp) and logs every difference as a warning,
//   "Checksum for beat %d mismatched: %s (sim) != %s (%s)."
// on every platform (docs/PR_REPLAY_CHECK.md). A gpg log target here sees each such line on
// whichever thread logs it (dispatch is synchronous, under the log context's lock), so the
// count needs no change to the sim. A match leaves no trace, so how many recorded checksums
// were compared (checked=) comes from the replay itself (ReplayChecks.cpp), and a run that
// compared none cannot pass.

#include "platform/posix/runner/Runner.h"

#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

#include "gpg/core/utils/Logging.h"
#include "platform/posix/crt/CrtMath.h"

namespace
{
  constexpr const char kMismatchPrefix[] = "Checksum for beat ";

  // Read by the signal handler: lock-free atomics and a fixed buffer only.
  char gReplayId[128] = "unknown";
  std::atomic<int> gMismatchCount{0};
  std::atomic<int> gFirstMismatchBeat{-1};
  std::atomic<std::int64_t> gAppliedBeats{0};
  std::atomic<std::int32_t> gGameTick{0};
  std::atomic<const char*> gExitReason{nullptr};
  std::atomic<bool> gResultPrinted{false};

  class ChecksumMismatchWatch final : public gpg::LogTarget
  {
  public:
    ChecksumMismatchWatch()
      : gpg::LogTarget(true)
    {}

    void OnMessage(
      const gpg::LogSeverity level,
      const msvc8::string& message,
      const msvc8::vector<msvc8::string>& context,
      const int previousDepth
    ) override
    {
      (void)context;
      (void)previousDepth;
      if (level != gpg::LogSeverity::Warn) {
        return;
      }
      const char* const text = message.c_str();
      if (text == nullptr || std::strncmp(text, kMismatchPrefix, sizeof(kMismatchPrefix) - 1) != 0) {
        return;
      }
      int beat = 0;
      if (std::sscanf(text, "Checksum for beat %d mismatched:", &beat) != 1) {
        return;
      }
      if (gMismatchCount.fetch_add(1) == 0) {
        gFirstMismatchBeat.store(beat);
      }
      std::fprintf(stderr, "faf_replay: %s\n", text);
      std::fflush(stderr);
    }
  };

  // Every line to stderr (/echolog), as the Windows build's log window shows it.
  class StderrEcho final : public gpg::LogTarget
  {
  public:
    StderrEcho()
      : gpg::LogTarget(true)
    {}

    void OnMessage(
      const gpg::LogSeverity level,
      const msvc8::string& message,
      const msvc8::vector<msvc8::string>& context,
      const int previousDepth
    ) override
    {
      (void)context;
      (void)previousDepth;
      const char* const severity =
        level == gpg::LogSeverity::Warn ? "warning" : (level == gpg::LogSeverity::Debug ? "debug" : "info");
      std::fprintf(stderr, "%s: %s\n", severity, message.c_str());
    }
  };

  [[nodiscard]] std::int64_t LastBeat()
  {
    const std::int64_t applied = gAppliedBeats.load();
    return applied > 0 ? applied - 1 : 0;
  }

  [[nodiscard]] bool IsPassingReason(const char* const reason)
  {
    return reason != nullptr &&
           (std::strcmp(reason, faf_runner::kExitGameOver) == 0 ||
            std::strcmp(reason, faf_runner::kExitEndOfReplay) == 0);
  }

  [[nodiscard]] std::int64_t Checked()
  {
    return faf_runner::ReplayChecksCompared(static_cast<std::uint64_t>(gAppliedBeats.load()));
  }

  // The verdict and its exit status (Runner.h). Async-signal-safe.
  [[nodiscard]] const char* Verdict(const char* const reason, int* const exitCode)
  {
    if (gMismatchCount.load() > 0) {
      *exitCode = 1;
      return faf_runner::kVerdictMismatch;
    }
    *exitCode = 2;
    if (!IsPassingReason(reason)) {
      return faf_runner::kVerdictIncomplete;
    }
    if (faf_runner::FpcrWatchBadReads() > 0) {
      return faf_runner::kVerdictFpcr;
    }
    if (Checked() <= 0) {
      return faf_runner::kVerdictUnverified;
    }
    *exitCode = 0;
    return faf_runner::kVerdictPass;
  }

  // --- Async-signal-safe formatting ----------------------------------------------------------
  void AppendText(char* const buffer, std::size_t& used, const std::size_t capacity, const char* text)
  {
    while (text != nullptr && *text != '\0' && used + 1 < capacity) {
      buffer[used++] = *text++;
    }
  }

  void AppendInteger(char* const buffer, std::size_t& used, const std::size_t capacity, std::int64_t value)
  {
    char digits[24];
    std::size_t count = 0;
    const bool negative = value < 0;
    std::uint64_t magnitude = negative ? static_cast<std::uint64_t>(-(value + 1)) + 1u : static_cast<std::uint64_t>(value);
    do {
      digits[count++] = static_cast<char>('0' + magnitude % 10u);
      magnitude /= 10u;
    } while (magnitude != 0u && count < sizeof(digits));
    if (negative && used + 1 < capacity) {
      buffer[used++] = '-';
    }
    while (count > 0 && used + 1 < capacity) {
      buffer[used++] = digits[--count];
    }
  }

  [[nodiscard]] const char* SignalName(const int signalNumber)
  {
    switch (signalNumber) {
      case SIGSEGV:
        return "SIGSEGV";
      case SIGBUS:
        return "SIGBUS";
      case SIGILL:
        return "SIGILL";
      case SIGFPE:
        return "SIGFPE";
      case SIGABRT:
        return "SIGABRT";
      case SIGTRAP:
        return "SIGTRAP";
      default:
        return "signal";
    }
  }

  std::size_t FormatResult(
    char* const buffer,
    const std::size_t capacity,
    const char* const reason,
    const char* const reasonSuffix,
    const char* const verdict
  )
  {
    std::size_t used = 0;
    AppendText(buffer, used, capacity, "FAF_REPLAY_RESULT replay=");
    AppendText(buffer, used, capacity, gReplayId);
    AppendText(buffer, used, capacity, " beats=");
    AppendInteger(buffer, used, capacity, LastBeat());
    AppendText(buffer, used, capacity, " tick=");
    AppendInteger(buffer, used, capacity, gGameTick.load());
    AppendText(buffer, used, capacity, " recorded=");
    AppendInteger(buffer, used, capacity, faf_runner::ReplayChecksRecorded());
    AppendText(buffer, used, capacity, " checked=");
    AppendInteger(buffer, used, capacity, Checked());
    AppendText(buffer, used, capacity, " mismatches=");
    AppendInteger(buffer, used, capacity, gMismatchCount.load());
    AppendText(buffer, used, capacity, " exit=");
    AppendText(buffer, used, capacity, reason);
    AppendText(buffer, used, capacity, reasonSuffix);
    AppendText(buffer, used, capacity, " verdict=");
    AppendText(buffer, used, capacity, verdict);
    buffer[used] = '\0';
    return used;
  }
} // namespace

void faf_runner::ReportInit(const char* const replayId)
{
  std::string id = (replayId != nullptr && replayId[0] != '\0') ? replayId : "unknown";
  for (char& c : id) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      c = '_';
    }
  }
  std::snprintf(gReplayId, sizeof(gReplayId), "%s", id.c_str());

  // Never destroyed: the log context outlives every static, and so must its targets.
  static ChecksumMismatchWatch* const sWatch = new ChecksumMismatchWatch();
  (void)sWatch;
}

void faf_runner::ReportEchoLogToStderr()
{
  static StderrEcho* const sEcho = new StderrEcho();
  (void)sEcho;
}

void faf_runner::ReportSetExitReason(const char* const reason)
{
  const char* expected = nullptr;
  (void)gExitReason.compare_exchange_strong(expected, reason);
}

const char* faf_runner::ReportGetExitReason()
{
  return gExitReason.load();
}

void faf_runner::ReportNoteProgress(const std::uint64_t appliedBeats, const std::int32_t gameTick)
{
  gAppliedBeats.store(static_cast<std::int64_t>(appliedBeats));
  gGameTick.store(gameTick);
}

int faf_runner::ReportMismatchCount()
{
  return gMismatchCount.load();
}

int faf_runner::ReportFinish(const char* const fallbackReason, const bool writeToLog)
{
  ReportSetExitReason(fallbackReason != nullptr ? fallbackReason : kExitRequested);
  const char* const reason = gExitReason.load();
  int exitCode = 2;
  const char* const verdict = Verdict(reason, &exitCode);

  if (gResultPrinted.exchange(true)) {
    return exitCode;
  }

  char line[384];
  (void)FormatResult(line, sizeof(line), reason, "", verdict);
  if (writeToLog) {
    gpg::Logf("%s", line);
    gpg::Logf(
      "faf_replay: last beat %lld, game tick %d, first mismatch at beat %d",
      static_cast<long long>(LastBeat()),
      static_cast<int>(gGameTick.load()),
      gFirstMismatchBeat.load()
    );
    gpg::Logf(
      "faf_replay: recorded checksums %lld, compared %lld (%lld when the whole stream plays), stream beats %lld",
      static_cast<long long>(ReplayChecksRecorded()),
      static_cast<long long>(Checked()),
      static_cast<long long>(ReplayChecksComparable()),
      static_cast<long long>(ReplayChecksStreamBeats())
    );
  }
  std::fflush(stderr);
  std::fprintf(stdout, "%s\n", line);
  std::fflush(stdout);
  // Every exit path after this is std::_Exit, which skips the atexit trace dump
  // (FAF_CRTMATH_TRACE=1); returns false at once when tracing is off.
  (void)faf_crtmathTraceDump();
  return exitCode;
}

void faf_runner::ReportFinishFromSignal(const int signalNumber)
{
  if (gResultPrinted.exchange(true)) {
    return;
  }
  int exitCode = 2;
  const char* const verdict = gMismatchCount.load() > 0 ? Verdict(nullptr, &exitCode) : kVerdictIncomplete;
  char line[384];
  std::size_t used = FormatResult(line, sizeof(line) - 1, "crash-", SignalName(signalNumber), verdict);
  line[used++] = '\n';
  (void)::write(STDOUT_FILENO, line, used);
}
