// ns per call for the hot routines (extern "C" entry = Layer 2 fast path) next to their
// Layer 1 transcriptions, and the Layer 1 operations. Single thread, inputs cycled from a
// pre-generated table of realistic values, results folded into a checksum so nothing is
// optimised away.

#include <chrono>
#include <cstring>

#include "EmuApi.h"
#include "Gen.h"

namespace x87t
{
  namespace emu = faf_posix::x87::emu;

  namespace
  {
    constexpr int kTable = 4096;
    constexpr std::uint64_t kCalls = 20'000'000;

    struct Inputs
    {
      float box[kTable][15];
      float axis[kTable][3];
      float vec[kTable][3];
      float target[kTable][3];
      float h[kTable][6];
      float lenSq[kTable];
      float scalar[kTable];
      Ext ext[kTable][2];
    };

    template <class Fn>
    double Time(Fn&& fn)
    {
      // Warm up, then take the best of three runs.
      fn(kCalls / 10);
      double best = 1e30;
      for (int run = 0; run < 3; ++run) {
        const auto start = std::chrono::steady_clock::now();
        fn(kCalls);
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / static_cast<double>(kCalls);
        best = ns < best ? ns : best;
      }
      return best;
    }

    volatile std::uint32_t gSink;

    void Line(const char* name, const double fast, const double ref)
    {
      char note[64];
      std::snprintf(note, sizeof note, "fast %.1f ns/call, transcription %.1f ns/call", fast, ref);
      std::printf("bench %-34s %s\n", name, note);
      std::fflush(stdout);
    }
  } // namespace

  void RunBench()
  {
    static Inputs in;
    Rng r(0xBE7C4u);
    for (int i = 0; i < kTable; ++i) {
      difffuzz::GenVec3(r, in.box[i]);
      difffuzz::GenUnitVec3(r, in.box[i] + 3);
      difffuzz::GenUnitVec3(r, in.box[i] + 6);
      difffuzz::GenUnitVec3(r, in.box[i] + 9);
      for (int k = 12; k < 15; ++k) {
        in.box[i][k] = static_cast<float>(r.Range(0.5, 20.0));
      }
      difffuzz::GenUnitVec3(r, in.axis[i]);
      for (float& f : in.vec[i]) {
        f = static_cast<float>(r.Range(-100.0, 100.0));
      }
      for (float& f : in.target[i]) {
        f = static_cast<float>(r.Range(-100.0, 100.0));
      }
      for (int k = 0; k < 4; ++k) {
        in.h[i][k] = static_cast<float>(r.Range(0.0, 200.0));
      }
      in.h[i][4] = static_cast<float>(r.Unit());
      in.h[i][5] = static_cast<float>(r.Unit());
      const float* v = in.vec[i];
      in.lenSq[i] = (v[0] * v[0] + v[1] * v[1]) + v[2] * v[2];
      in.scalar[i] = static_cast<float>(r.Range(0.1, 10.0));
      in.ext[i][0] = FromFloat(static_cast<float>(r.Range(-100.0, 100.0)));
      in.ext[i][1] = FromFloat(static_cast<float>(r.Range(0.5, 100.0)));
    }

    Line("box_radius_on_axis",
      Time([&](std::uint64_t n) { std::uint32_t s = 0; for (std::uint64_t i = 0; i < n; ++i) { const int k = static_cast<int>(i & (kTable - 1)); s += FBits(msvc8_x87_box_radius_on_axis(in.box[k], in.axis[k])); } gSink = s; }),
      Time([&](std::uint64_t n) { std::uint32_t s = 0; for (std::uint64_t i = 0; i < n; ++i) { const int k = static_cast<int>(i & (kTable - 1)); s += FBits(emu::ref::BoxRadiusOnAxis(in.box[k], in.axis[k])); } gSink = s; }));

    Line("bilinear_height",
      Time([&](std::uint64_t n) { std::uint32_t s = 0; for (std::uint64_t i = 0; i < n; ++i) { const float* h = in.h[i & (kTable - 1)]; s += FBits(msvc8_x87_bilinear_height(h[0], h[1], h[2], h[3], h[4], h[5])); } gSink = s; }),
      Time([&](std::uint64_t n) { std::uint32_t s = 0; for (std::uint64_t i = 0; i < n; ++i) { const float* h = in.h[i & (kTable - 1)]; s += FBits(emu::ref::BilinearHeight(h[0], h[1], h[2], h[3], h[4], h[5])); } gSink = s; }));

    Line("vector_length_zyx",
      Time([&](std::uint64_t n) { std::uint32_t s = 0; for (std::uint64_t i = 0; i < n; ++i) { s += FBits(msvc8_x87_vector_length_zyx(in.vec[i & (kTable - 1)])); } gSink = s; }),
      Time([&](std::uint64_t n) { std::uint32_t s = 0; for (std::uint64_t i = 0; i < n; ++i) { s += FBits(emu::ref::VectorLengthZYX(in.vec[i & (kTable - 1)])); } gSink = s; }));

    Line("scale_vector",
      Time([&](std::uint64_t n) { std::uint32_t s = 0; float out[3]; for (std::uint64_t i = 0; i < n; ++i) { const int k = static_cast<int>(i & (kTable - 1)); msvc8_x87_scale_vector(out, in.vec[k], in.scalar[k]); s += FBits(out[0]) ^ FBits(out[2]); } gSink = s; }),
      Time([&](std::uint64_t n) { std::uint32_t s = 0; float out[3]; for (std::uint64_t i = 0; i < n; ++i) { const int k = static_cast<int>(i & (kTable - 1)); emu::ref::ScaleVector(out, in.vec[k], in.scalar[k]); s += FBits(out[0]) ^ FBits(out[2]); } gSink = s; }));

    Line("scale_vector_to_length",
      Time([&](std::uint64_t n) { std::uint32_t s = 0; float out[3]; for (std::uint64_t i = 0; i < n; ++i) { const int k = static_cast<int>(i & (kTable - 1)); msvc8_x87_scale_vector_to_length(out, in.vec[k], in.lenSq[k], in.scalar[k]); s += FBits(out[0]) ^ FBits(out[2]); } gSink = s; }),
      Time([&](std::uint64_t n) { std::uint32_t s = 0; float out[3]; for (std::uint64_t i = 0; i < n; ++i) { const int k = static_cast<int>(i & (kTable - 1)); emu::ref::ScaleVectorToLength(out, in.vec[k], in.lenSq[k], in.scalar[k]); s += FBits(out[0]) ^ FBits(out[2]); } gSink = s; }));

    Line("scale_vector_max_length",
      Time([&](std::uint64_t n) { std::uint32_t s = 0; float v[3]; for (std::uint64_t i = 0; i < n; ++i) { const int k = static_cast<int>(i & (kTable - 1)); std::memcpy(v, in.vec[k], sizeof v); msvc8_x87_scale_vector_max_length(v, v + 1, v + 2, in.scalar[k], in.lenSq[k]); s += FBits(v[0]) ^ FBits(v[2]); } gSink = s; }),
      Time([&](std::uint64_t n) { std::uint32_t s = 0; float v[3]; for (std::uint64_t i = 0; i < n; ++i) { const int k = static_cast<int>(i & (kTable - 1)); std::memcpy(v, in.vec[k], sizeof v); emu::ref::ScaleVectorMaxLength(v, v + 1, v + 2, in.scalar[k], in.lenSq[k]); s += FBits(v[0]) ^ FBits(v[2]); } gSink = s; }));

    Line("scale_vector_to_length_of",
      Time([&](std::uint64_t n) { std::uint32_t s = 0; float out[3]; for (std::uint64_t i = 0; i < n; ++i) { const int k = static_cast<int>(i & (kTable - 1)); msvc8_x87_scale_vector_to_length_of(out, in.vec[k], in.target[k], in.lenSq[k]); s += FBits(out[0]) ^ FBits(out[2]); } gSink = s; }),
      Time([&](std::uint64_t n) { std::uint32_t s = 0; float out[3]; for (std::uint64_t i = 0; i < n; ++i) { const int k = static_cast<int>(i & (kTable - 1)); emu::ref::ScaleVectorToLengthOf(out, in.vec[k], in.target[k], in.lenSq[k]); s += FBits(out[0]) ^ FBits(out[2]); } gSink = s; }));

    Line("round_to_int",
      Time([&](std::uint64_t n) { std::uint32_t s = 0; for (std::uint64_t i = 0; i < n; ++i) { s += static_cast<std::uint32_t>(msvc8_x87_round_to_int(in.h[i & (kTable - 1)][0])); } gSink = s; }),
      Time([&](std::uint64_t n) { std::uint32_t s = 0; for (std::uint64_t i = 0; i < n; ++i) { s += static_cast<std::uint32_t>(emu::ref::RoundToInt(in.h[i & (kTable - 1)][0])); } gSink = s; }));

    // The routines built on the transcendental instructions (Layer 1 around the kernels).
#if defined(X87T_REAL_KERNELS)
    const char* kernels = "real kernels";
#else
    const char* kernels = "TEST stand-in kernels";
#endif
    auto routine = [&](const char* name, auto&& fn) {
      const double ns = Time([&](std::uint64_t n) {
        std::uint32_t s = 0;
        for (std::uint64_t i = 0; i < n; ++i) {
          s += fn(static_cast<int>(i & (kTable - 1)));
        }
        gSink = s;
      });
      std::printf("bench %-34s %.1f ns/call (%s)\n", name, ns, kernels);
    };
    routine("sinf", [&](int k) { return FBits(msvc8_x87_sinf(in.vec[k][0] * 0.05f)); });
    routine("atan2f", [&](int k) { return FBits(msvc8_x87_atan2f(in.vec[k][0], in.vec[k][1])); });
    routine("sin_times_cos", [&](int k) { return FBits(msvc8_x87_sin_times_cos(in.h[k][4], in.h[k][5])); });
    routine("atan2_plus", [&](int k) { return FBits(msvc8_x87_atan2_plus(in.vec[k][0], in.vec[k][1], in.scalar[k])); });
    routine("intrinsic_exp", [&](int k) { return FBits(msvc8_x87_intrinsic_exp(in.vec[k][0] * 0.1f)); });
    routine("intrinsic_ln", [&](int k) { return FBits(msvc8_x87_intrinsic_ln(in.scalar[k])); });
    routine("scaled_ln_quotient", [&](int k) { return FBits(msvc8_x87_scaled_ln_quotient(in.scalar[k], in.h[k][4] + 0.5f, 3.0f)); });
    routine("acos", [&](int k) { return static_cast<std::uint32_t>(DBits(msvc8_x87_acos(in.h[k][4] * 2.0 - 1.0))); });
    routine("pow", [&](int k) { return static_cast<std::uint32_t>(DBits(msvc8_x87_pow(in.scalar[k], in.h[k][4] * 4.0))); });
    routine("slerp_spherical_blend", [&](int k) {
      float out[4];
      msvc8_x87_slerp_spherical_blend(out, in.box[k], in.box[k] + 4, 0.25f + in.h[k][4], 1.0f - in.h[k][5], in.h[k][5]);
      return FBits(out[0]) ^ FBits(out[3]);
    });

    // Layer 1 operations alone (the slow path, and the arithmetic around the kernels).
    auto op = [&](const char* name, auto&& fn) {
      const double ns = Time([&](std::uint64_t n) {
        std::uint64_t s = 0;
        for (std::uint64_t i = 0; i < n; ++i) {
          const Ext* e = in.ext[i & (kTable - 1)];
          s += fn(e[0], e[1]).signif;
        }
        gSink = static_cast<std::uint32_t>(s);
      });
      std::printf("bench %-34s layer 1 %.1f ns/op\n", name, ns);
    };
    op("Add P24", [](const Ext a, const Ext b) { return Add(a, b, Precision::P24); });
    op("Mul P24", [](const Ext a, const Ext b) { return Mul(a, b, Precision::P24); });
    op("Div P24", [](const Ext a, const Ext b) { return Div(a, b, Precision::P24); });
    op("Sqrt P24", [](const Ext a, const Ext b) { return Sqrt(b, Precision::P24); });
    op("Mul P64", [](const Ext a, const Ext b) { return Mul(a, b, Precision::P64); });
    op("FromFloat+ToFloat", [](const Ext a, const Ext b) { return FromFloat(ToFloat(a)); });
  }
} // namespace x87t
