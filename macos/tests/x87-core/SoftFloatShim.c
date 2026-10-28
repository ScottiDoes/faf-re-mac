// The test oracle's entry points: Berkeley SoftFloat 3e extF80 with the "8086"
// specialization (x87 NaN rules), round-to-nearest-even, extF80_roundingPrecision 32/64/80
// for precision control 24/53/64. Plain C so the C++ tests never parse SoftFloat's
// THREAD_LOCAL (_Thread_local) declarations. Ext and SfExt share extFloat80_t's
// LITTLEENDIAN layout: { uint64_t signif; uint16_t signExp; }.

#include <stdint.h>
#include <string.h>

#include "softfloat.h"

typedef struct SfExt
{
  uint64_t signif;
  uint16_t signExp;
} SfExt;

static extFloat80_t In(const SfExt* a)
{
  extFloat80_t r;
  r.signif = a->signif;
  r.signExp = a->signExp;
  return r;
}

static void Out(extFloat80_t r, SfExt* out)
{
  out->signif = r.signif;
  out->signExp = r.signExp;
}

void x87t_sf_set_precision(unsigned bits)
{
  extF80_roundingPrecision = (uint_fast8_t)bits;
  softfloat_roundingMode = softfloat_round_near_even;
  softfloat_exceptionFlags = 0;
}

void x87t_sf_add(const SfExt* a, const SfExt* b, SfExt* out) { Out(extF80_add(In(a), In(b)), out); }
void x87t_sf_sub(const SfExt* a, const SfExt* b, SfExt* out) { Out(extF80_sub(In(a), In(b)), out); }
void x87t_sf_mul(const SfExt* a, const SfExt* b, SfExt* out) { Out(extF80_mul(In(a), In(b)), out); }
void x87t_sf_div(const SfExt* a, const SfExt* b, SfExt* out) { Out(extF80_div(In(a), In(b)), out); }
void x87t_sf_sqrt(const SfExt* a, SfExt* out) { Out(extF80_sqrt(In(a)), out); }

void x87t_sf_from_f32(uint32_t bits, SfExt* out)
{
  float32_t f;
  f.v = bits;
  Out(f32_to_extF80(f), out);
}

void x87t_sf_from_f64(uint64_t bits, SfExt* out)
{
  float64_t f;
  f.v = bits;
  Out(f64_to_extF80(f), out);
}

void x87t_sf_from_i32(int32_t value, SfExt* out) { Out(i32_to_extF80(value), out); }

uint32_t x87t_sf_to_f32(const SfExt* a) { return extF80_to_f32(In(a)).v; }
uint64_t x87t_sf_to_f64(const SfExt* a) { return extF80_to_f64(In(a)).v; }
int32_t x87t_sf_to_i32(const SfExt* a) { return (int32_t)extF80_to_i32(In(a), softfloat_round_near_even, false); }
int32_t x87t_sf_to_i32_trunc(const SfExt* a) { return (int32_t)extF80_to_i32(In(a), softfloat_round_minMag, false); }
void x87t_sf_round_to_int(const SfExt* a, SfExt* out) { Out(extF80_roundToInt(In(a), softfloat_round_near_even, true), out); }
