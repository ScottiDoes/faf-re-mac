// faf_posixRunSelfTests: see SelfTest.h. This file is built with the engine's (sim) flags,
// which is what the contraction and cast checks are about.
//
// Known answers: macos/tests/crt/kat_oracle.py derives every one of them with exact
// arithmetic (python3 -I kat_oracle.py --check keeps the table there in sync with this file).
// The x87 ones model X87Math64.asm under precision control 24 (DETERMINISM.md §3.1):
// they hold on real x87 hardware and must hold for the software x87 (platform/posix/math).

#include "platform/posix/crt/SelfTest.h"

#include "legacy/math/X87Math.h"
#include "platform/posix/crt/CrtMath.h"


#include <dlfcn.h>
#include <immintrin.h>

#include <cinttypes>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

// The software x87 (another package) defines these with X87Math.cpp's signatures.
extern "C" {
  unsigned int msvc8_x87_control_word();
  float msvc8_x87_sinf(float x);
  float msvc8_x87_cosf(float x);
  float msvc8_x87_tanf(float x);
  float msvc8_x87_atan2f(float y, float x);
  double msvc8_x87_asin(double x);
  double msvc8_x87_pow(double x, double y);
  float msvc8_x87_intrinsic_exp(float x);
  float msvc8_x87_intrinsic_ln(float x);
  std::int32_t msvc8_x87_round_to_int(float value);
  void msvc8_x87_scale_vector(float* destLanes, const float* sourceLanes, float scale);
  void msvc8_x87_scale_vector_to_length(float* destLanes, const float* sourceLanes, float lengthSq, float length);
  float msvc8_x87_vector_length_zyx(const float* lanes);
}

namespace
{
  std::uint32_t Bits(const float x) noexcept
  {
    std::uint32_t bits;
    std::memcpy(&bits, &x, sizeof bits);
    return bits;
  }

  std::uint64_t Bits(const double x) noexcept
  {
    std::uint64_t bits;
    std::memcpy(&bits, &x, sizeof bits);
    return bits;
  }

  float F32(const std::uint32_t bits) noexcept
  {
    float x;
    std::memcpy(&x, &bits, sizeof x);
    return x;
  }

  double F64(const std::uint64_t bits) noexcept
  {
    double x;
    std::memcpy(&x, &bits, sizeof x);
    return x;
  }

  std::uint64_t ReadFpcr() noexcept
  {
#if defined(__aarch64__)
    std::uint64_t fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    return fpcr;
#else
    return 0;
#endif
  }

  class Checker
  {
  public:
    explicit Checker(const bool verbose) noexcept : verbose_(verbose) {}

    // `detail` is printed on failure (and in verbose mode).
    [[gnu::format(printf, 4, 5)]] void Check(const bool ok, const char* const name, const char* const format, ...) noexcept
    {
      ++checks_;
      if (!ok) {
        ++failures_;
      }
      if (ok && !verbose_) {
        return;
      }
      char detail[256];
      va_list args;
      va_start(args, format);
      std::vsnprintf(detail, sizeof detail, format, args);
      va_end(args);
      std::fprintf(stderr, "[faf selftest] %s %s: %s\n", ok ? "ok  " : "FAIL", name, detail);
    }

    void Bits32(const char* const name, const float got, const std::uint32_t expected) noexcept
    {
      Check(Bits(got) == expected, name, "got 0x%08" PRIX32 ", want 0x%08" PRIX32, Bits(got), expected);
    }

    void Bits64(const char* const name, const double got, const std::uint64_t expected) noexcept
    {
      Check(Bits(got) == expected, name, "got 0x%016" PRIX64 ", want 0x%016" PRIX64, Bits(got), expected);
    }

    void Int(const char* const name, const long long got, const long long expected) noexcept
    {
      Check(got == expected, name, "got %lld, want %lld", got, expected);
    }

    [[nodiscard]] int checks() const noexcept { return checks_; }
    [[nodiscard]] int failures() const noexcept { return failures_; }

  private:
    bool verbose_;
    int checks_ = 0;
    int failures_ = 0;
  };

  // --- CPU state and code generation ------------------------------------------------------

  void CheckCodegen(Checker& check)
  {
    check.Check(ReadFpcr() == 0, "FPCR on the calling thread", "0x%" PRIX64 " (want 0: round to nearest, no FZ/DN)",
                ReadFpcr());

    // Saturating float->int (-fno-strict-float-cast-overflow: llvm.fptosi.sat on arm64 and
    // x64 alike, DETERMINISM.md §5 rule 5). Volatile operands keep the casts at run time.
    volatile float nanF = F32(0x7FC00000u);
    volatile float big = 3e9f;
    volatile float minusBig = -3e9f;
    volatile float minusOneAndHalf = -1.5f;
    volatile float huge = 5e9f;
    volatile float shortOverflow = 70000.0f;
    volatile double nanD = F64(0x7FF8000000000000ull);
    check.Int("(int)NaN", static_cast<int>(nanF), 0);
    check.Int("(int)3e9f", static_cast<int>(big), INT_MAX);
    check.Int("(int)-3e9f", static_cast<int>(minusBig), INT_MIN);
    check.Int("(unsigned)-1.5f", static_cast<unsigned>(minusOneAndHalf), 0);
    check.Int("(unsigned)5e9f", static_cast<unsigned>(huge), UINT_MAX);
    check.Int("(short)70000.0f", static_cast<short>(shortOverflow), SHRT_MAX);
    check.Int("(int)(double)NaN", static_cast<int>(nanD), 0);

    // No contraction: a*b+c with a*b = 1 - 2^-54 (double) / 1 - 2^-26 (float), which rounds
    // to 1 alone; unfused gives 0, a fused multiply-add gives -2^-54 / -2^-26.
    volatile double a = 1.0 + 0x1p-27;
    volatile double b = 1.0 - 0x1p-27;
    volatile double c = -1.0;
    const double unfused = a * b + c;
    check.Bits64("double a*b+c is not fused", unfused, 0);
    volatile float af = 1.0f + 0x1p-13f;
    volatile float bf = 1.0f - 0x1p-13f;
    volatile float cf = -1.0f;
    const float unfusedF = af * bf + cf;
    check.Bits32("float a*b+c is not fused", unfusedF, 0);
  }

  // --- A fresh thread: FPCR, MXCSR stand-in, MSVC rand -------------------------------------

  struct ThreadResults
  {
    std::uint64_t fpcr = ~0ull;
    int firstRand[5] = {};
    int reseeded1[5] = {};
    int reseeded12345[5] = {};
    std::uint64_t fpcrAfterFtz = ~0ull;
    unsigned int csrAfterFtz = 0;
  };

  void CheckFreshThread(Checker& check)
  {
    ThreadResults results;
    std::thread worker([&results] {
      results.fpcr = ReadFpcr();
      for (int& value : results.firstRand) {
        value = std::rand();
      }
      std::srand(1);
      for (int& value : results.reseeded1) {
        value = std::rand();
      }
      std::srand(12345);
      for (int& value : results.reseeded12345) {
        value = std::rand();
      }
      // SimWorkerPool copies MXCSR to its workers; FTZ must be recorded, never put in FPCR.
      const unsigned int saved = _mm_getcsr();
      _mm_setcsr(saved | _MM_FLUSH_ZERO_ON);
      results.fpcrAfterFtz = ReadFpcr();
      results.csrAfterFtz = _mm_getcsr();
      _mm_setcsr(saved);
    });
    worker.join();

    check.Check(results.fpcr == 0, "FPCR on a new thread", "0x%" PRIX64, results.fpcr);
    check.Check((results.fpcrAfterFtz & (1ull << 24)) == 0, "_mm_setcsr(FTZ) leaves FPCR.FZ clear", "FPCR 0x%" PRIX64,
                results.fpcrAfterFtz);
    check.Check((results.csrAfterFtz & _MM_FLUSH_ZERO_ON) != 0, "_mm_getcsr reports the recorded FTZ", "MXCSR 0x%X",
                results.csrAfterFtz);

    static constexpr int kFromOne[5] = {41, 18467, 6334, 26500, 19169};
    static constexpr int kFrom12345[5] = {7584, 19164, 25795, 22125, 5828};
    const auto same = [](const int* x, const int* y) { return std::memcmp(x, y, 5 * sizeof(int)) == 0; };
    const auto& r = results;
    check.Check(same(r.firstRand, kFromOne), "rand on a new thread (state 1)", "%d %d %d %d %d (want 41 18467 6334 26500 19169)",
                r.firstRand[0], r.firstRand[1], r.firstRand[2], r.firstRand[3], r.firstRand[4]);
    check.Check(same(r.reseeded1, kFromOne), "srand(1); rand", "%d %d %d %d %d", r.reseeded1[0], r.reseeded1[1],
                r.reseeded1[2], r.reseeded1[3], r.reseeded1[4]);
    check.Check(same(r.reseeded12345, kFrom12345), "srand(12345); rand", "%d %d %d %d %d (want 7584 19164 25795 22125 5828)",
                r.reseeded12345[0], r.reseeded12345[1], r.reseeded12345[2], r.reseeded12345[3], r.reseeded12345[4]);
  }

  // --- faf_crtmath ---------------------------------------------------------------------------

  void CheckInThisImage(Checker& check, const char* const name, const void* const symbol)
  {
    Dl_info symbolInfo{};
    Dl_info selfInfo{};
    const bool ok = dladdr(symbol, &symbolInfo) != 0 &&
                    dladdr(reinterpret_cast<const void*>(&faf_posixRunSelfTests), &selfInfo) != 0 &&
                    symbolInfo.dli_fbase == selfInfo.dli_fbase;
    check.Check(ok, name, "bound to %s", symbolInfo.dli_fname != nullptr ? symbolInfo.dli_fname : "?");
  }

  void CheckCrtMath(Checker& check)
  {
    char firstBad[64] = "";
    const int bad = faf_crtmathCheckBindings(firstBad, sizeof firstBad);
    check.Check(bad == 0, "faf_crtmath entry points call libm, not themselves", "%d bad (first: %s)", bad, firstBad);

    CheckInThisImage(check, "rand is MSVC's (this executable)", reinterpret_cast<const void*>(&::rand));
    CheckInThisImage(check, "srand is MSVC's (this executable)", reinterpret_cast<const void*>(&::srand));
    CheckInThisImage(check, "sin is faf_crtmath's", reinterpret_cast<const void*>(static_cast<double (*)(double)>(&::sin)));
    CheckInThisImage(check, "atan2f is faf_crtmath's", reinterpret_cast<const void*>(&::atan2f));
    CheckInThisImage(check, "pow is faf_crtmath's",
                     reinterpret_cast<const void*>(static_cast<double (*)(double, double)>(&::pow)));

    // Wm3::Math<float>::PI is (float)(4.0*atan(1.0)): atan(1.0) must be RN(pi/4).
    volatile double one = 1.0;
    check.Bits64("atan(1.0)", std::atan(one), 0x3FE921FB54442D18ull);

    // The UCRT's NaN results: the x86 default NaN for a domain error, the operand for NaN in.
    volatile double two = 2.0;
    volatile double minusOne = -1.0;
    volatile double minusTwo = -2.0;
    volatile double zero = 0.0;
    volatile double inf = F64(0x7FF0000000000000ull);
    volatile double nanPayload = F64(0xFFF0000000001234ull); // signalling, negative, payload
    volatile float minusOneF = -1.0f;
    check.Bits64("asin(2) is -nan(ind)", std::asin(two), 0xFFF8000000000000ull);
    check.Bits64("log(-1) is -nan(ind)", std::log(minusOne), 0xFFF8000000000000ull);
    check.Bits64("sin(inf) is -nan(ind)", std::sin(inf), 0xFFF8000000000000ull);
    check.Bits64("pow(-2, 0.5) is -nan(ind)", std::pow(minusTwo, 0.5), 0xFFF8000000000000ull);
    check.Bits64("fmod(1, 0) is -nan(ind)", std::fmod(1.0, zero), 0xFFF8000000000000ull);
    check.Bits64("sqrt(-1) is -nan(ind)", std::sqrt(minusOne), 0xFFF8000000000000ull);
    check.Bits32("sqrtf(-1) is -nan(ind)", std::sqrt(minusOneF), 0xFFC00000u);
    check.Bits32("logf(-1) is -nan(ind)", std::log(minusOneF), 0xFFC00000u);
    check.Bits64("sin(sNaN) quiets the operand", std::sin(nanPayload), 0xFFF8000000001234ull);
    check.Bits64("pow(NaN, 0) is 1 (Annex F)", std::pow(nanPayload, zero), 0x3FF0000000000000ull);
    check.Bits64("log(0) is -inf", std::log(zero), 0xFFF0000000000000ull);

    // The header quaternion constants (moho/math/QuaternionMath.h, kQuatYaw*) are built in
    // static initializers from (float)sin/cos of these (double)(float) half angles. Every
    // value is >= 0.037 float ulp from a rounding tie (kat_oracle.py), so any faithful double
    // libm, the UCRT's included, gives these floats.
    struct Trig
    {
      std::uint64_t angle;
      std::uint32_t sine;
      std::uint32_t cosine;
    };
    static constexpr Trig kTrig[] = {
      {0x3FE921FB60000000ull, 0x3F3504F3u, 0x3F3504F3u},
      {0xBFE921FB60000000ull, 0xBF3504F3u, 0x3F3504F3u},
      {0x3FD921FB60000000ull, 0x3EC3EF16u, 0x3F6C835Eu},
      {0xBFD921FB60000000ull, 0xBEC3EF16u, 0x3F6C835Eu},
    };
    for (const Trig& t : kTrig) {
      volatile double angle = F64(t.angle);
      const float s = static_cast<float>(std::sin(angle));
      const float c = static_cast<float>(std::cos(angle));
      check.Check(Bits(s) == t.sine && Bits(c) == t.cosine, "(float)sin/cos of a quaternion half angle",
                  "angle 0x%016" PRIX64 ": sin 0x%08" PRIX32 " cos 0x%08" PRIX32 ", want 0x%08" PRIX32 " 0x%08" PRIX32,
                  t.angle, Bits(s), Bits(c), t.sine, t.cosine);
    }
  }

  // --- x87 known answers (DETERMINISM.md §3, §6.1.4) ------------------------------------------

  void CheckX87(Checker& check)
  {
    check.Int("msvc8_x87_control_word", msvc8_x87_control_word(), 0x027F);

    // _CIasin's x87 body under PC24 (X87Math.h): not the correctly rounded 0x3F0C1117.
    check.Bits32("x87 asin(0x3F052EAA)", static_cast<float>(msvc8_x87_asin(F32(0x3F052EAAu))), 0x3F0C1118u);
    // _CIpow's x87 body under PC24: not the exact square 0x43E554AE.
    check.Bits32("x87 pow(0x41AB54C2, 2)", static_cast<float>(msvc8_x87_pow(F32(0x41AB54C2u), 2.0)), 0x43E554AFu);

    // A lane of 1e-25: the x87 square keeps its exponent, float would flush it to 0.
    const float tinyLane[3] = {F32(0x15F79688u), 0.0f, 0.0f};
    check.Bits32("x87 VectorLengthZYX(1e-25)", msvc8_x87_vector_length_zyx(tinyLane), 0x15F79688u);

    // fistp: round to nearest even; NaN and out of range give the integer indefinite.
    check.Int("x87 round_to_int(2.5)", msvc8_x87_round_to_int(2.5f), 2);
    check.Int("x87 round_to_int(3.5)", msvc8_x87_round_to_int(3.5f), 4);
    check.Int("x87 round_to_int(-2.5)", msvc8_x87_round_to_int(-2.5f), -2);
    check.Int("x87 round_to_int(-2^31)", msvc8_x87_round_to_int(-2147483648.0f), INT32_MIN);
    check.Int("x87 round_to_int(2147483520)", msvc8_x87_round_to_int(2147483520.0f), 2147483520);
    check.Int("x87 round_to_int(3e9)", msvc8_x87_round_to_int(3e9f), INT32_MIN);
    check.Int("x87 round_to_int(NaN)", msvc8_x87_round_to_int(F32(0x7FC00000u)), INT32_MIN);

    // fsin/fcos leave |x| >= 2^63 unchanged (C2 set); fptan pushes nothing, and the second
    // fstp of msvc8_x87_tanf stores the empty register's indefinite (DETERMINISM.md §3.2 #5).
    const float twoTo63 = F32(0x5F000000u);
    check.Bits32("x87 sinf(2^63) is 2^63", msvc8_x87_sinf(twoTo63), 0x5F000000u);
    check.Bits32("x87 cosf(2^63) is 2^63", msvc8_x87_cosf(twoTo63), 0x5F000000u);
    check.Bits32("x87 tanf(2^63) is the indefinite", msvc8_x87_tanf(twoTo63), 0xFFC00000u);
    check.Bits32("x87 sinf(0)", msvc8_x87_sinf(0.0f), 0x00000000u);
    check.Bits32("x87 cosf(0)", msvc8_x87_cosf(0.0f), 0x3F800000u);
    check.Bits32("x87 atan2f(1, 1)", msvc8_x87_atan2f(1.0f, 1.0f), 0x3F490FDBu);
    check.Bits32("x87 intrinsic_exp(0)", msvc8_x87_intrinsic_exp(0.0f), 0x3F800000u);
    check.Bits32("x87 intrinsic_ln(1)", msvc8_x87_intrinsic_ln(1.0f), 0x00000000u);

    // fild; fmul under PC24 rounds the exact 55-bit product once (math.random(2028178766)
    // with rand() % 0x7FFF == 279): a double multiply would give 0x4B83C106.
    check.Bits32("MultiplyIntegerX87(2028178766, 279 * scale)", msvc8::MultiplyIntegerX87(2028178766, F32(0x3C0B8117u)),
                 0x4B83C107u);

    // Denormal products: PC24 rounds to 24 bits with the extended exponent, then the store
    // rounds onto the denormal grid; one float multiply rounds once and gives the last value.
    float out[3] = {};
    const float scaleSource[3] = {F32(0x1C9CCD65u), 0.0f, 0.0f};
    msvc8_x87_scale_vector(out, scaleSource, F32(0x1CE928EAu));
    check.Check(Bits(out[0]) == 0x00000476u, "x87 ScaleVector denormal double rounding",
                "got 0x%08" PRIX32 ", want 0x00000476 (single rounding: 0x00000477)", Bits(out[0]));

    const float lengthSource[3] = {F32(0x21E2F5E4u), 0.0f, 0.0f};
    msvc8_x87_scale_vector_to_length(out, lengthSource, 1.0f, F32(0x17D20296u));
    check.Check(Bits(out[0]) == 0x000005D2u, "x87 ScaleVectorToLength denormal double rounding",
                "got 0x%08" PRIX32 ", want 0x000005D2 (single rounding: 0x000005D1)", Bits(out[0]));

    // length / sqrt(lengthSq) = 1e30 / 2^-74.5 exceeds FLT_MAX on the x87 stack and comes back
    // into range times 1e-30; float arithmetic would give +inf.
    const float overflowSource[3] = {F32(0x0DA24260u), 0.0f, 0.0f};
    msvc8_x87_scale_vector_to_length(out, overflowSource, F32(0x00000001u), F32(0x7149F2CAu));
    check.Bits32("x87 ScaleVectorToLength extended exponent", out[0], 0x64B504F4u);
  }
} // namespace

bool faf_posixRunSelfTests(const bool verbose)
{
  Checker check(verbose);
  CheckCodegen(check);
  CheckFreshThread(check);
  CheckCrtMath(check);
  CheckX87(check);
  if (verbose || check.failures() != 0) {
    std::fprintf(stderr, "[faf selftest] %d of %d checks passed\n", check.checks() - check.failures(), check.checks());
  }
  return check.failures() == 0;
}
