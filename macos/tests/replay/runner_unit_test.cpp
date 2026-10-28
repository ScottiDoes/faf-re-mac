// Unit test of the headless runner's result (src/platform/posix/runner): ReplayReport.cpp
// (mismatch counting, the FAF_REPLAY_RESULT line, verdicts and exit codes, the signal-safe
// form), ReplayChecks.cpp (the replay's checksum records and how many the sim compares) and
// FpcrWatch.cpp (every thread's FPCR). Promoted from build/runner-tests/report_test.cpp
// (package runner, wave 2) and extended for tick=, recorded=, checked= and verdict=.
//
// Each scenario runs in a child so the print-once state starts fresh; the parent compares
// the child's stdout and exit status.
//
//   runner_unit_test                     every scenario; exit 0 when all pass
//   runner_unit_test --count <file>...   per replay: recorded, comparable, stream beats (for
//                                        verify_replays.sh to compare with replay_tool.py)
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "gpg/core/utils/Logging.h"
#include "platform/posix/runner/Runner.h"

extern std::vector<gpg::LogTarget*> gTestTargets;
extern std::vector<std::string> gTestLogLines;
extern std::vector<std::string> gTestWarnLines;

namespace
{
  void Send(const gpg::LogSeverity level, const char* const text)
  {
    const msvc8::string message(text);
    const msvc8::vector<msvc8::string> context;
    for (gpg::LogTarget* const target : gTestTargets) {
      target->OnMessage(level, message, context, 0);
    }
  }

  int failures = 0;

  template <class F>
  void Scenario(const char* const name, F body, const char* const expectedOut, const int expectedRc)
  {
    int fds[2];
    if (pipe(fds) != 0) {
      std::perror("pipe");
      std::exit(2);
    }
    std::fflush(stdout); // the child must not inherit unflushed parent output
    const pid_t pid = fork();
    if (pid == 0) {
      dup2(fds[1], STDOUT_FILENO);
      close(fds[0]);
      (void)freopen("/dev/null", "w", stderr);
      std::_Exit(body());
    }
    close(fds[1]);
    std::string out;
    char buf[512];
    ssize_t n = 0;
    while ((n = read(fds[0], buf, sizeof(buf))) > 0) {
      out.append(buf, static_cast<size_t>(n));
    }
    close(fds[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    const int rc = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    const bool ok = out == expectedOut && rc == expectedRc;
    std::printf("%-48s %s\n", name, ok ? "ok" : "FAIL");
    if (!ok) {
      ++failures;
      std::printf("   got  rc=%d out=[%s]\n   want rc=%d out=[%s]\n", rc, out.c_str(), expectedRc, expectedOut);
    }
  }

  void Check(const char* const name, const bool ok)
  {
    std::printf("%-48s %s\n", name, ok ? "ok" : "FAIL");
    if (!ok) {
      ++failures;
    }
  }

  // A .scfareplay image: the header VCR_SetupReplaySession reads, then commands.
  class ReplayBuilder
  {
  public:
    explicit ReplayBuilder(const int sources, const char* const signature = "Replay v1.9\r\n")
    {
      Text("Supreme Commander v1.50.3839");
      Text("\r\n");
      mData.insert(mData.end(), signature, signature + std::strlen(signature));
      Text("/maps/test/test.scmap");
      Text("\r\n\x1a");
      U32(3);
      Bytes("abc", 3); // game mods
      U32(0);          // scenario info
      mData.push_back(static_cast<std::uint8_t>(sources));
      for (int i = 0; i < sources; ++i) {
        const std::string name = "player" + std::to_string(i);
        Text(name.c_str());
        U32(0xFFFFFFFFu);
      }
      mData.push_back(0); // cheats
      mData.push_back(1); // one army
      U32(4);
      Bytes("army", 4);
      mData.push_back(0);
      mData.push_back(0xFF);
      U32(12345); // seed
    }

    ReplayBuilder& Source(const int source)
    {
      Header(1, 4);
      mData.push_back(static_cast<std::uint8_t>(source));
      return *this;
    }

    ReplayBuilder& Advance(const int count = 1)
    {
      for (int i = 0; i < count; ++i) {
        Header(0, 7);
        U32(1);
      }
      return *this;
    }

    ReplayBuilder& Verify(const std::int32_t beat, const int size = 23)
    {
      Header(3, size);
      for (int i = 0; i < 16; ++i) {
        mData.push_back(static_cast<std::uint8_t>(0xA0 + i));
      }
      if (size >= 23) {
        U32(static_cast<std::uint32_t>(beat));
        for (int i = 23; i < size; ++i) {
          mData.push_back(0);
        }
      } else {
        mData.resize(mData.size() + static_cast<std::size_t>(size - 19), 0);
      }
      return *this;
    }

    ReplayBuilder& Raw(const std::vector<std::uint8_t>& bytes)
    {
      mData.insert(mData.end(), bytes.begin(), bytes.end());
      return *this;
    }

    [[nodiscard]] const std::vector<std::uint8_t>& Data() const
    {
      return mData;
    }

    [[nodiscard]] std::string WriteTemp() const
    {
      char path[] = "/tmp/faf_runner_unit_XXXXXX";
      const int fd = mkstemp(path);
      if (fd < 0) {
        std::perror("mkstemp");
        std::exit(2);
      }
      (void)write(fd, mData.data(), mData.size());
      close(fd);
      return path;
    }

  private:
    void Text(const char* const text)
    {
      mData.insert(mData.end(), text, text + std::strlen(text) + 1);
    }
    void Bytes(const char* const bytes, const std::size_t count)
    {
      mData.insert(mData.end(), bytes, bytes + count);
    }
    void U32(const std::uint32_t value)
    {
      for (int i = 0; i < 4; ++i) {
        mData.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
      }
    }
    void Header(const int op, const int size)
    {
      mData.push_back(static_cast<std::uint8_t>(op));
      mData.push_back(static_cast<std::uint8_t>(size & 0xFF));
      mData.push_back(static_cast<std::uint8_t>(size >> 8));
    }

    std::vector<std::uint8_t> mData;
  };

  // The engine's rules on one stream (see ReplayChecks.cpp), two command sources.
  ReplayBuilder RulesReplay()
  {
    ReplayBuilder r(2);
    r.Verify(0);              // before any SetCommandSource: CReplayClient drops it
    r.Source(0).Verify(0);    // curBeat 1: compared
    r.Advance();              // curBeat 2 from here
    r.Verify(1);              // compared
    r.Verify(2);              // beat 2 is not < curBeat 2: "in the future"
    r.Source(5).Verify(0);    // source 5 >= 2 sources: dropped, the sim keeps source 0
    r.Source(1).Verify(1);    // compared at 2
    r.Verify(1, 22);          // too short for a digest and a beat: not a record
    r.Advance(200);           // curBeat 202
    r.Verify(0);              // 0 < 202 - 128: older than the sim's 128-beat ring
    r.Verify(100);            // compared at 202
    r.Verify(201, 24);        // a longer record still decodes: compared at 202
    return r;
  }

  bool gReplayLoaded = false;
  void LoadReplay(const ReplayBuilder& replay)
  {
    const std::string path = replay.WriteTemp();
    std::string error;
    gReplayLoaded = faf_runner::ReplayChecksLoad(path.c_str(), &error);
    std::remove(path.c_str());
  }

  // A replay with `n` compared records (one source, one per beat, beats 0..n-1).
  ReplayBuilder SimpleReplay(const int n)
  {
    ReplayBuilder r(1);
    r.Source(0);
    for (int i = 0; i < n; ++i) {
      r.Verify(i).Advance();
    }
    return r;
  }

  int CountMode(const int argc, char** const argv)
  {
    int status = 0;
    for (int i = 2; i < argc; ++i) {
      std::ifstream in(argv[i], std::ios::binary);
      const std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      faf_runner::ReplayChecksSummary summary{};
      std::string error;
      if (!in.good() && !in.eof()) {
        std::printf("%s error=cannot-read\n", argv[i]);
        status = 2;
        continue;
      }
      if (!faf_runner::ReplayChecksParse(data.data(), data.size(), &summary, &error)) {
        std::printf("%s error=%s\n", argv[i], error.c_str());
        status = 2;
        continue;
      }
      std::printf(
        "%s recorded=%lld comparable=%lld stream_beats=%lld truncated=%d\n",
        argv[i],
        static_cast<long long>(summary.recorded),
        static_cast<long long>(summary.comparedAt.size()),
        static_cast<long long>(summary.streamBeats),
        summary.truncated ? 1 : 0
      );
    }
    return status;
  }

  void SetFpcr(const std::uint64_t value)
  {
    __asm__ volatile("msr fpcr, %0" : : "r"(value));
  }
} // namespace

int main(int argc, char** argv)
{
  using namespace faf_runner;
  if (argc >= 2 && std::strcmp(argv[1], "--count") == 0) {
    return CountMode(argc, argv);
  }

  // --- ReplayChecks: the engine's rules --------------------------------------------------
  {
    ReplayChecksSummary s{};
    std::string error;
    const ReplayBuilder replay = RulesReplay();
    const bool ok = ReplayChecksParse(replay.Data().data(), replay.Data().size(), &s, &error);
    const std::vector<std::int64_t> want{1, 2, 2, 202, 202};
    Check("parse: header and stream", ok && !s.truncated && s.streamBeats == 201);
    Check("parse: 9 records (the 22-byte one is not)", s.recorded == 9);
    Check("parse: compared at curBeat 1,2,2,202,202", s.comparedAt == want);
  }
  {
    ReplayChecksSummary s{};
    std::string error;
    ReplayBuilder replay(1);
    replay.Source(0).Verify(0).Advance().Raw({3, 2, 0}).Verify(1).Advance();
    const bool ok = ReplayChecksParse(replay.Data().data(), replay.Data().size(), &s, &error);
    Check("parse: a size-2 message stops the stream", ok && s.truncated && s.recorded == 1 && s.streamBeats == 1);
    ReplayBuilder cut(1);
    cut.Source(0).Verify(0).Advance().Raw({3, 23, 0, 1, 2});
    const bool okCut = ReplayChecksParse(cut.Data().data(), cut.Data().size(), &s, &error);
    Check("parse: a cut-off last message is not read", okCut && s.truncated && s.recorded == 1);
  }
  {
    ReplayChecksSummary s{};
    std::string error;
    const ReplayBuilder bad(1, "Replay v1.8\r\n");
    Check("parse: wrong signature refused", !ReplayChecksParse(bad.Data().data(), bad.Data().size(), &s, &error));
    const ReplayBuilder good = SimpleReplay(3);
    Check(
      "parse: header cut short refused", !ReplayChecksParse(good.Data().data(), 40, &s, &error) && !error.empty()
    );
    std::string loadError;
    Check("load: missing file refused", !ReplayChecksLoad("/nonexistent/x.scfareplay", &loadError));
    Check("load: unloaded counts are -1", ReplayChecksRecorded() == -1 && ReplayChecksCompared(100) == -1);
  }

  // --- The result line, verdicts and exit codes ------------------------------------------
  Scenario("mismatches counted, sanitized id", [] {
    LoadReplay(SimpleReplay(80));
    ReportInit("27890640 x");
    Send(gpg::LogSeverity::Info, "Checksum for beat 50 mismatched: a (sim) != b (p)."); // not a warning
    Send(gpg::LogSeverity::Warn, "Checksum for beat 3400 mismatched: a (sim) != b (p).");
    Send(gpg::LogSeverity::Warn, "Checksum for beat 3450 mismatched: c (sim) != d (q).");
    Send(gpg::LogSeverity::Warn, "Checksum for beat x mismatched"); // malformed
    Send(gpg::LogSeverity::Warn, "Ignoring verify of beat 7 because it is in the future.");
    ReportNoteProgress(3501, 3400);
    ReportSetExitReason(kExitEndOfReplay);
    ReportSetExitReason(kExitStalled); // first reason wins
    const int rc = ReportFinish(kExitRequested);
    (void)ReportFinish(kExitRequested); // prints once
    bool logged = false;
    bool counts = false;
    for (const auto& l : gTestLogLines) {
      logged |= l.find("first mismatch at beat 3400") != std::string::npos;
      counts |= l.find("recorded checksums 80, compared 80 (80 when the whole stream plays), stream beats 80") !=
                std::string::npos;
    }
    return gReplayLoaded && logged && counts && ReportMismatchCount() == 2 ? rc : 99;
  }, "FAF_REPLAY_RESULT replay=27890640_x beats=3500 tick=3400 recorded=80 checked=80 mismatches=2 exit=end-of-replay verdict=mismatch\n", 1);

  Scenario("pass: game over, every checksum compared", [] {
    LoadReplay(SimpleReplay(5));
    ReportInit("27890640");
    ReportNoteProgress(24001, 24000);
    ReportSetExitReason(kExitGameOver);
    return ReportFinish(kExitRequested);
  }, "FAF_REPLAY_RESULT replay=27890640 beats=24000 tick=24000 recorded=5 checked=5 mismatches=0 exit=gameover verdict=pass\n", 0);

  Scenario("checked counts what the sim had decoded", [] {
    LoadReplay(RulesReplay());
    ReportInit("r");
    ReportNoteProgress(3, 2); // 3 sync packets: records decoded at curBeat 1 and 2
    return ReportFinish(kExitEndOfReplay);
  }, "FAF_REPLAY_RESULT replay=r beats=2 tick=2 recorded=9 checked=3 mismatches=0 exit=end-of-replay verdict=pass\n", 0);

  Scenario("unverified: nothing compared yet", [] {
    LoadReplay(RulesReplay());
    ReportInit("r");
    ReportNoteProgress(1, 0); // only the opening sync
    return ReportFinish(kExitEndOfReplay);
  }, "FAF_REPLAY_RESULT replay=r beats=0 tick=0 recorded=9 checked=0 mismatches=0 exit=end-of-replay verdict=unverified\n", 2);

  Scenario("unverified: a replay without checksums", [] {
    ReplayBuilder replay(1);
    replay.Source(0).Advance(100);
    LoadReplay(replay);
    ReportInit("r");
    ReportNoteProgress(500, 100);
    return ReportFinish(kExitEndOfReplay);
  }, "FAF_REPLAY_RESULT replay=r beats=499 tick=100 recorded=0 checked=0 mismatches=0 exit=end-of-replay verdict=unverified\n", 2);

  Scenario("unverified: the replay could not be read", [] {
    std::string error;
    (void)ReplayChecksLoad("/nonexistent/x.scfareplay", &error);
    ReportInit("r");
    ReportNoteProgress(500, 100);
    return ReportFinish(kExitGameOver);
  }, "FAF_REPLAY_RESULT replay=r beats=499 tick=100 recorded=-1 checked=-1 mismatches=0 exit=gameover verdict=unverified\n", 2);

  Scenario("fallback reason, no beats", [] {
    ReportInit(nullptr);
    return ReportFinish(nullptr);
  }, "FAF_REPLAY_RESULT replay=unknown beats=0 tick=0 recorded=-1 checked=-1 mismatches=0 exit=exit-requested verdict=incomplete\n", 2);

  Scenario("stalled is not a pass", [] {
    LoadReplay(SimpleReplay(5));
    ReportInit("r");
    ReportNoteProgress(2, 1);
    return ReportFinish(kExitStalled);
  }, "FAF_REPLAY_RESULT replay=r beats=1 tick=1 recorded=5 checked=1 mismatches=0 exit=stalled verdict=incomplete\n", 2);

  Scenario("die: not logged", [] {
    ReportInit("r");
    const int rc = ReportFinish(kExitDie, false);
    return gTestLogLines.empty() ? rc : 99;
  }, "FAF_REPLAY_RESULT replay=r beats=0 tick=0 recorded=-1 checked=-1 mismatches=0 exit=die verdict=incomplete\n", 2);

  Scenario("signal form, then print-once", [] {
    LoadReplay(SimpleReplay(200));
    ReportInit("27890640");
    Send(gpg::LogSeverity::Warn, "Checksum for beat 100 mismatched: a (sim) != b (p).");
    ReportNoteProgress(123457, 0);
    ReportFinishFromSignal(SIGSEGV);
    (void)ReportFinish(kExitRequested);
    return 0;
  }, "FAF_REPLAY_RESULT replay=27890640 beats=123456 tick=0 recorded=200 checked=200 mismatches=1 exit=crash-SIGSEGV verdict=mismatch\n", 0);

  Scenario("large beat count, crash without mismatch", [] {
    ReportInit("r");
    ReportNoteProgress(10000000001ull, 7);
    ReportFinishFromSignal(SIGBUS);
    return 0;
  }, "FAF_REPLAY_RESULT replay=r beats=10000000000 tick=7 recorded=-1 checked=-1 mismatches=0 exit=crash-SIGBUS verdict=incomplete\n", 0);

  // --- FPCR: every thread at 0, and a thread that is not ----------------------------------
  Scenario("fpcr: every thread 0 passes", [] {
    LoadReplay(SimpleReplay(5));
    ReportInit("r");
    std::atomic<bool> stop{false};
    std::thread worker([&stop] {
      while (!stop.load()) {
        std::this_thread::yield();
      }
    });
    const bool clean = FpcrWatchSample();
    stop.store(true);
    worker.join();
    ReportNoteProgress(10, 9);
    const int rc = ReportFinish(kExitEndOfReplay);
    return clean && FpcrWatchSamples() == 1 && FpcrWatchBadReads() == 0 ? rc : 99;
  }, "FAF_REPLAY_RESULT replay=r beats=9 tick=9 recorded=5 checked=5 mismatches=0 exit=end-of-replay verdict=pass\n", 0);

  Scenario("fpcr: a worker with flush-to-zero fails the run", [] {
    LoadReplay(SimpleReplay(5));
    ReportInit("r");
    std::atomic<bool> ready{false};
    std::atomic<bool> stop{false};
    std::thread worker([&] {
      SetFpcr(1u << 24); // FZ
      ready.store(true);
      while (!stop.load()) {
        std::this_thread::yield();
      }
      SetFpcr(0);
    });
    while (!ready.load()) {
      std::this_thread::yield();
    }
    const bool clean = FpcrWatchSample();
    stop.store(true);
    worker.join();
    bool warned = false;
    for (const auto& l : gTestWarnLines) {
      warned |= l.find("runs with FPCR 0x01000000") != std::string::npos;
    }
    ReportNoteProgress(10, 9);
    const int rc = ReportFinish(kExitEndOfReplay);
    return !clean && warned && FpcrWatchBadReads() == 1 ? rc : 99;
  }, "FAF_REPLAY_RESULT replay=r beats=9 tick=9 recorded=5 checked=5 mismatches=0 exit=end-of-replay verdict=fpcr\n", 2);

  std::printf("%s\n", failures == 0 ? "ALL PASS" : "FAILURES");
  return failures == 0 ? 0 : 1;
}
