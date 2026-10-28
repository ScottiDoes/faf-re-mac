#pragma once

// Stand-in for the x86 intrinsic headers (<immintrin.h>, <xmmintrin.h>, <mmintrin.h>,
// <intrin.h>) on Apple Silicon: exactly the SSE/MMX intrinsics the engine uses, on
// NEON. Lane-wise add/mul are IEEE-exact on both, so results match bit for bit (the
// build disables FMA contraction). Add an intrinsic here only with x86-exact semantics.

#include <arm_neon.h>
#include <cstdint>
#include <cstring>

typedef float32x4_t __m128;
typedef float64x2_t __m128d;
typedef int32x4_t __m128i;
typedef int64x1_t __m64;

// --- SSE: packed single ----------------------------------------------------------------
static inline __m128 _mm_loadu_ps(const float* p) { return vld1q_f32(p); }
static inline __m128 _mm_load_ps(const float* p) { return vld1q_f32(p); }
static inline void _mm_storeu_ps(float* p, __m128 a) { vst1q_f32(p, a); }
static inline void _mm_store_ps(float* p, __m128 a) { vst1q_f32(p, a); }
static inline __m128 _mm_add_ps(__m128 a, __m128 b) { return vaddq_f32(a, b); }
static inline __m128 _mm_mul_ps(__m128 a, __m128 b) { return vmulq_f32(a, b); }
static inline __m128 _mm_setzero_ps() { return vdupq_n_f32(0.0f); }
static inline __m128 _mm_set1_ps(float v) { return vdupq_n_f32(v); }

// _mm_shuffle_ps(a, b, imm): lanes 0-1 from a, lanes 2-3 from b, two bits each.
#define _MM_SHUFFLE(z, y, x, w) (((z) << 6) | ((y) << 4) | ((x) << 2) | (w))
#define _mm_shuffle_ps(a, b, imm)                                                          \
  __extension__({                                                                        \
    float32x4_t _fa = (a), _fb = (b);                                                    \
    float _r[4] = {vgetq_lane_f32(_fa, 0), vgetq_lane_f32(_fa, 0),                       \
                   vgetq_lane_f32(_fb, 0), vgetq_lane_f32(_fb, 0)};                      \
    float _la[4], _lb[4];                                                                \
    vst1q_f32(_la, _fa);                                                                 \
    vst1q_f32(_lb, _fb);                                                                 \
    _r[0] = _la[(imm) & 3];                                                              \
    _r[1] = _la[((imm) >> 2) & 3];                                                       \
    _r[2] = _lb[((imm) >> 4) & 3];                                                       \
    _r[3] = _lb[((imm) >> 6) & 3];                                                       \
    vld1q_f32(_r);                                                                       \
  })

// --- SSE2: integer and double ----------------------------------------------------------
static inline __m128i _mm_load_si128(const __m128i* p) { return vld1q_s32(reinterpret_cast<const std::int32_t*>(p)); }
static inline __m128i _mm_loadu_si128(const __m128i* p) { return vld1q_s32(reinterpret_cast<const std::int32_t*>(p)); }
static inline void _mm_store_si128(__m128i* p, __m128i a) { vst1q_s32(reinterpret_cast<std::int32_t*>(p), a); }
static inline void _mm_storeu_si128(__m128i* p, __m128i a) { vst1q_s32(reinterpret_cast<std::int32_t*>(p), a); }
static inline __m128i _mm_setzero_si128() { return vdupq_n_s32(0); }
static inline __m128d _mm_setzero_pd() { return vdupq_n_f64(0.0); }

// --- MMX (only what platform/MmxOnX64.h forwards) -------------------------------------
static inline __m64 _mm_setzero_si64() { return vdup_n_s64(0); }
static inline void _mm_empty() {}
static inline int _mm_cvtsi64_si32(__m64 a) { return static_cast<int>(vget_lane_s64(a, 0)); }
static inline __m64 _mm_add_pi16(__m64 a, __m64 b) {
  return vreinterpret_s64_s16(vadd_s16(vreinterpret_s16_s64(a), vreinterpret_s16_s64(b)));
}
static inline __m64 _mm_srai_pi16(__m64 a, int count) {
  const int c = count > 15 ? 15 : count;
  return vreinterpret_s64_s16(vshl_s16(vreinterpret_s16_s64(a), vdup_n_s16(static_cast<std::int16_t>(-c))));
}
static inline __m64 _mm_packs_pu16(__m64 a, __m64 b) {
  // Signed 16-bit lanes saturated to unsigned 8-bit: a's four lanes, then b's.
  const int16x8_t ab = vcombine_s16(vreinterpret_s16_s64(a), vreinterpret_s16_s64(b));
  return vreinterpret_s64_u8(vqmovun_s16(ab));
}

// --- Hints -------------------------------------------------------------------------------
#define _MM_HINT_T0 3
#define _MM_HINT_T1 2
#define _MM_HINT_T2 1
#define _MM_HINT_NTA 0
#define _mm_prefetch(p, hint) __builtin_prefetch(static_cast<const void*>(p), 0, 3)
static inline void _mm_pause() { __asm__ __volatile__("yield"); }

// --- MXCSR on FPCR/FPSR -----------------------------------------------------------------
// The engine reads and writes MXCSR the way the MSVC CRT's _controlfp does: rounding
// control, flush-to-zero and the exception masks/flags. Map rounding and FTZ onto FPCR
// and the sticky flags onto FPSR; the masks are kept in a per-thread shadow (ARM
// traps are off on Apple Silicon, so masked is the only real behaviour).
#define _MM_ROUND_MASK 0x6000u
#define _MM_ROUND_NEAREST 0x0000u
#define _MM_ROUND_DOWN 0x2000u
#define _MM_ROUND_UP 0x4000u
#define _MM_ROUND_TOWARD_ZERO 0x6000u
#define _MM_FLUSH_ZERO_MASK 0x8000u
#define _MM_FLUSH_ZERO_ON 0x8000u
#define _MM_FLUSH_ZERO_OFF 0x0000u
#define _MM_MASK_MASK 0x1f80u
#define _MM_EXCEPT_MASK 0x003fu

namespace faf_posix_detail {
inline thread_local std::uint32_t mxcsrMasks = 0x1f80u;  // all exceptions masked (reset value)
// MXCSR.FTZ as last set. It is recorded, never applied: ARM's FPCR.FZ also flushes
// denormal *inputs* (x86 FTZ without DAZ flushes only results), so applying it would
// change sim results. FPCR.FZ stays 0 (macos/docs/DETERMINISM.md §5 rule 8).
inline thread_local std::uint32_t mxcsrFlushZero = 0;
}

static inline unsigned int _mm_getcsr() {
  std::uint64_t fpcr, fpsr;
  __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
  __asm__ __volatile__("mrs %0, fpsr" : "=r"(fpsr));
  // FPCR.RMode (bits 22-23): 0 nearest, 1 +inf, 2 -inf, 3 zero.
  static const std::uint32_t kRound[4] = {_MM_ROUND_NEAREST, _MM_ROUND_UP, _MM_ROUND_DOWN, _MM_ROUND_TOWARD_ZERO};
  std::uint32_t csr = kRound[(fpcr >> 22) & 3u] | faf_posix_detail::mxcsrMasks;
  csr |= faf_posix_detail::mxcsrFlushZero;
  // FPSR cumulative flags: IOC 0, DZC 1, OFC 2, UFC 3, IXC 4, IDC 7.
  // MXCSR flags: IE 0, DE 1, ZE 2, OE 3, UE 4, PE 5.
  if (fpsr & (1u << 0)) csr |= 0x01u;
  if (fpsr & (1u << 7)) csr |= 0x02u;
  if (fpsr & (1u << 1)) csr |= 0x04u;
  if (fpsr & (1u << 2)) csr |= 0x08u;
  if (fpsr & (1u << 3)) csr |= 0x10u;
  if (fpsr & (1u << 4)) csr |= 0x20u;
  return csr;
}

static inline void _mm_setcsr(unsigned int csr) {
  std::uint64_t fpcr;
  __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
  std::uint64_t rmode = 0;
  switch (csr & _MM_ROUND_MASK) {
    case _MM_ROUND_UP: rmode = 1; break;
    case _MM_ROUND_DOWN: rmode = 2; break;
    case _MM_ROUND_TOWARD_ZERO: rmode = 3; break;
    default: rmode = 0; break;
  }
  fpcr = (fpcr & ~((3ull << 22) | (1ull << 24))) | (rmode << 22);
  faf_posix_detail::mxcsrFlushZero = csr & _MM_FLUSH_ZERO_ON;
  __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
  std::uint64_t fpsr = 0;
  if (csr & 0x01u) fpsr |= 1u << 0;
  if (csr & 0x02u) fpsr |= 1u << 7;
  if (csr & 0x04u) fpsr |= 1u << 1;
  if (csr & 0x08u) fpsr |= 1u << 2;
  if (csr & 0x10u) fpsr |= 1u << 3;
  if (csr & 0x20u) fpsr |= 1u << 4;
  __asm__ __volatile__("msr fpsr, %0" : : "r"(fpsr));
  faf_posix_detail::mxcsrMasks = csr & _MM_MASK_MASK;
}
