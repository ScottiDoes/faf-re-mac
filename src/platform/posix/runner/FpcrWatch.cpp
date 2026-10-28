// The per-thread FPCR check (WAVE2_REPORT.md section 7, the x87-core request): every thread of
// the process, sim workers included, must run with the arm64 floating-point control register
// at 0 (round to nearest, no flush-to-zero, no default-NaN, no alternate handling), as the
// platform self-test already checks for the main thread and one fresh thread.
//
// The sim's threads (CSimDriver's "Sim" and "Issue" threads, SimWorkerPool's workers) are
// started inside the engine, and gpg::SetThreadName is a no-op, so the runner cannot run code
// on them or tell them apart. Instead it reads every thread's register file from outside:
// task_threads() lists the process's threads and thread_get_state(ARM_NEON_STATE64) returns a
// thread's FPCR (the kernel stops the thread for the read, then lets it go on; timing cannot
// change what the sim computes). The headless runner starts no audio or window threads
// (/nosound, no device), so it expects 0 on every thread; any other value is reported with
// the thread's id and name and fails the run (verdict=fpcr).

#include "platform/posix/runner/Runner.h"

#include <mach/mach.h>
#include <pthread.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "gpg/core/utils/Logging.h"

namespace
{
  std::atomic<int> gSamples{0};
  std::atomic<int> gMaxThreads{0};
  std::atomic<int> gReadFailures{0};
  std::atomic<int> gBadReads{0};
  std::atomic<std::uint64_t> gFirstBadFpcr{0};
  std::atomic<std::uint64_t> gFirstBadThread{0};

  [[nodiscard]] std::uint64_t OwnFpcr()
  {
    std::uint64_t value = 0;
    __asm__ volatile("mrs %0, fpcr" : "=r"(value));
    return value;
  }

  [[nodiscard]] std::uint64_t ThreadIdOf(const thread_act_t thread)
  {
    thread_identifier_info_data_t info{};
    mach_msg_type_number_t count = THREAD_IDENTIFIER_INFO_COUNT;
    if (::thread_info(thread, THREAD_IDENTIFIER_INFO, reinterpret_cast<thread_info_t>(&info), &count) != KERN_SUCCESS) {
      return 0;
    }
    return info.thread_id;
  }

  void NameOf(const thread_act_t thread, char* const name, const std::size_t capacity)
  {
    name[0] = '\0';
    if (const pthread_t pthread = ::pthread_from_mach_thread_np(thread); pthread != nullptr) {
      (void)::pthread_getname_np(pthread, name, capacity);
    }
  }
} // namespace

bool faf_runner::FpcrWatchSample()
{
  thread_act_array_t threads = nullptr;
  mach_msg_type_number_t threadCount = 0;
  if (::task_threads(::mach_task_self(), &threads, &threadCount) != KERN_SUCCESS) {
    gReadFailures.fetch_add(1);
    return false;
  }

  const thread_act_t self = ::mach_thread_self();
  bool allZero = true;
  for (mach_msg_type_number_t i = 0; i < threadCount; ++i) {
    std::uint64_t fpcr = 0;
    bool read = false;
    if (threads[i] == self) {
      fpcr = OwnFpcr();
      read = true;
    } else {
      arm_neon_state64_t state{};
      mach_msg_type_number_t stateCount = ARM_NEON_STATE64_COUNT;
      if (::thread_get_state(threads[i], ARM_NEON_STATE64, reinterpret_cast<thread_state_t>(&state), &stateCount) ==
          KERN_SUCCESS) {
        fpcr = state.__fpcr;
        read = true;
      }
    }
    if (!read) {
      // A thread that ended between the listing and the read.
      gReadFailures.fetch_add(1);
    } else if (fpcr != 0) {
      allZero = false;
      const std::uint64_t threadId = ThreadIdOf(threads[i]);
      char name[64];
      NameOf(threads[i], name, sizeof(name));
      if (gBadReads.fetch_add(1) == 0) {
        gFirstBadFpcr.store(fpcr);
        gFirstBadThread.store(threadId);
      }
      gpg::Warnf(
        "faf_replay: thread %llu \"%s\" runs with FPCR 0x%08llx (expected 0)",
        static_cast<unsigned long long>(threadId),
        name,
        static_cast<unsigned long long>(fpcr)
      );
      std::fprintf(
        stderr,
        "faf_replay: thread %llu \"%s\" runs with FPCR 0x%08llx (expected 0)\n",
        static_cast<unsigned long long>(threadId),
        name,
        static_cast<unsigned long long>(fpcr)
      );
    }
    (void)::mach_port_deallocate(::mach_task_self(), threads[i]);
  }
  (void)::mach_port_deallocate(::mach_task_self(), self);
  (void)::vm_deallocate(
    ::mach_task_self(), reinterpret_cast<vm_address_t>(threads), static_cast<vm_size_t>(threadCount) * sizeof(thread_act_t)
  );

  gSamples.fetch_add(1);
  int previous = gMaxThreads.load();
  while (static_cast<int>(threadCount) > previous &&
         !gMaxThreads.compare_exchange_weak(previous, static_cast<int>(threadCount))) {
  }
  return allZero;
}

int faf_runner::FpcrWatchSamples()
{
  return gSamples.load();
}

int faf_runner::FpcrWatchBadReads()
{
  return gBadReads.load();
}

void faf_runner::FpcrWatchLog()
{
  if (gSamples.load() == 0) {
    gpg::Logf("faf_replay: FPCR check: no sample taken");
    return;
  }
  if (gBadReads.load() == 0) {
    gpg::Logf(
      "faf_replay: FPCR check: %d samples of every thread (up to %d threads), all 0",
      gSamples.load(),
      gMaxThreads.load()
    );
    return;
  }
  gpg::Warnf(
    "faf_replay: FPCR check: %d nonzero reads in %d samples (first: thread %llu, FPCR 0x%08llx)",
    gBadReads.load(),
    gSamples.load(),
    static_cast<unsigned long long>(gFirstBadThread.load()),
    static_cast<unsigned long long>(gFirstBadFpcr.load())
  );
}
