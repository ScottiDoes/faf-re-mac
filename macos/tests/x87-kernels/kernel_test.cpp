// Test driver for src/platform/posix/math/X87Kernels.cpp against the Python oracle vectors
// (oracle.py). Build with -DFAF_X87_KERNELS_TESTING (see build.sh).
//
//   kernel_test check <dir> [slowEvery]   compare all vector files in <dir> (trig, atan, yl2x,
//                                          f2xm1 .txt) in three modes: Normal (production),
//                                          FastOnly (fast path alone, plus its error bound
//                                          against the oracle's 160-bit truth) and ForceSlow
//                                          (the 256-bit path; every slowEvery-th vector)
//   kernel_test bench                      ns/call on game-like inputs (thread CPU time)
//   kernel_test kat                        known answers (hardware value, retail findings)

#include "X87KernelsTesting.h"
#include "X87Types.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <random>
#include <string>
#include <vector>

using namespace faf_posix::x87;
namespace kt = faf_posix::x87::kernels_testing;
using u64 = std::uint64_t;

namespace
{
  struct Truth
  {
    bool valid = false;
    bool neg = false;
    int E = 0;
    u64 t[3] = {0, 0, 0}; // 160-bit T, little-endian limbs (t[2] holds the top 32 bits)
  };

  bool ParseHex(const char* s, int n, u64* out)
  {
    u64 v = 0;
    for (int i = 0; i < n; ++i)
    {
      const char c = s[i];
      int d;
      if (c >= '0' && c <= '9')
        d = c - '0';
      else if (c >= 'A' && c <= 'F')
        d = c - 'A' + 10;
      else if (c >= 'a' && c <= 'f')
        d = c - 'a' + 10;
      else
        return false;
      v = (v << 4) | static_cast<u64>(d);
    }
    *out = v;
    return true;
  }

  bool ParseExt(const std::string& tok, Ext* e)
  {
    if (tok.size() != 20)
      return false;
    u64 se, sig;
    if (!ParseHex(tok.c_str(), 4, &se) || !ParseHex(tok.c_str() + 4, 16, &sig))
      return false;
    *e = Ext{sig, static_cast<std::uint16_t>(se)};
    return true;
  }

  bool ParseTruth(const std::string& tok, Truth* t)
  {
    *t = Truth{};
    if (tok == "-")
      return true;
    const size_t a = tok.find(':'), b = tok.find(':', a + 1);
    if (a == std::string::npos || b == std::string::npos)
      return false;
    t->neg = tok[0] == '1';
    t->E = std::atoi(tok.substr(a + 1, b - a - 1).c_str());
    const std::string h = tok.substr(b + 1);
    if (h.size() != 40)
      return false;
    ParseHex(h.c_str(), 8, &t->t[2]);
    ParseHex(h.c_str() + 8, 16, &t->t[1]);
    ParseHex(h.c_str() + 24, 16, &t->t[0]);
    t->valid = true;
    return true;
  }

  std::vector<std::string> Split(const std::string& line)
  {
    std::vector<std::string> v;
    size_t i = 0;
    while (i < line.size())
    {
      while (i < line.size() && line[i] == ' ')
        ++i;
      size_t j = i;
      while (j < line.size() && line[j] != ' ')
        ++j;
      if (j > i)
        v.push_back(line.substr(i, j - i));
      i = j;
    }
    return v;
  }

  std::string Hex(const Ext& e)
  {
    char b[32];
    std::snprintf(b, sizeof b, "%04X%016llX", e.signExp, static_cast<unsigned long long>(e.signif));
    return b;
  }

  // 192-bit helpers for the error measurement.
  struct U192
  {
    u64 w[3];
  };

  U192 Shl(u64 hi, u64 lo, int sh) // (hi:lo) << sh, 0 <= sh < 64
  {
    U192 r;
    r.w[0] = lo << sh;
    r.w[1] = sh ? (hi << sh) | (lo >> (64 - sh)) : hi;
    r.w[2] = sh ? (hi >> (64 - sh)) : 0;
    return r;
  }

  bool Less(const U192& a, const U192& b)
  {
    for (int i = 2; i >= 0; --i)
      if (a.w[i] != b.w[i])
        return a.w[i] < b.w[i];
    return false;
  }

  U192 Sub(const U192& a, const U192& b) // a >= b
  {
    U192 r;
    u64 borrow = 0;
    for (int i = 0; i < 3; ++i)
    {
      const u64 t = a.w[i] - b.w[i];
      const u64 b1 = a.w[i] < b.w[i];
      r.w[i] = t - borrow;
      borrow = b1 | (t < borrow);
    }
    return r;
  }

  double ToDouble(const U192& a)
  {
    return std::ldexp(static_cast<double>(a.w[2]), 128) + std::ldexp(static_cast<double>(a.w[1]), 64) +
           static_cast<double>(a.w[0]);
  }

  struct ErrStats
  {
    u64 measured = 0;
    u64 boundViolations = 0;
    u64 signErrors = 0;
    double maxErrLsb = 0;   // |approx - truth| in units of the approximation's last bit
    double maxRatio = 0;    // that error / the claimed bound
    double maxBoundLsb = 0; // largest claimed bound seen
  };

  // Compares the fast path's last approximation with the oracle's truth.
  void MeasureFast(const Truth& tr, ErrStats* st, const std::string& what)
  {
    const kt::FastApprox a = kt::LastFast();
    if (!a.valid || !tr.valid)
      return;
    ++st->measured;
    if (a.neg != tr.neg)
    {
      if (++st->signErrors <= 5)
        std::printf("  SIGN ERROR fast approx %s\n", what.c_str());
      return;
    }
    const int d = a.e - tr.E; // approx in units of 2^(E-159): A * 2^(32 + d)
    if (d < -1 || d > 1)
    {
      if (++st->boundViolations <= 5)
        std::printf("  EXPONENT MISMATCH fast approx e=%d truth E=%d %s\n", a.e, tr.E, what.c_str());
      return;
    }
    const int sh = 32 + d;
    const U192 A = Shl(a.hi, a.lo, sh);
    const U192 T{{tr.t[0], tr.t[1], tr.t[2]}};
    const U192 diff = Less(A, T) ? Sub(T, A) : Sub(A, T);
    const double errLsb = ToDouble(diff) / std::ldexp(1.0, sh);
    const double bound = static_cast<double>(a.err);
    if (errLsb > st->maxErrLsb)
      st->maxErrLsb = errLsb;
    if (bound > st->maxBoundLsb)
      st->maxBoundLsb = bound;
    const double ratio = errLsb / (bound > 0 ? bound : 1e-300);
    if (ratio > st->maxRatio)
      st->maxRatio = ratio;
    // The truth itself is floor()ed: allow 2^-31 lsb of slack.
    if (errLsb > bound + std::ldexp(1.0, -30))
    {
      if (++st->boundViolations <= 10)
        std::printf("  BOUND VIOLATION err=%.3f lsb > bound %.0f: %s\n", errLsb, bound, what.c_str());
    }
  }

  struct Check
  {
    explicit Check(const char* n) : name(n) {}
    const char* name;
    u64 total = 0;
    u64 mismatches = 0;
    ErrStats err;
  };

  void Report(const char* mode, const Check& c)
  {
    std::printf("  [%s] %-6s vectors %llu  mismatches %llu", mode, c.name, static_cast<unsigned long long>(c.total),
                static_cast<unsigned long long>(c.mismatches));
    if (c.err.measured)
      std::printf("  | fast approx: %llu measured, max err %.4g lsb128, max bound %.4g, max err/bound %.3g, violations %llu",
                  static_cast<unsigned long long>(c.err.measured), c.err.maxErrLsb, c.err.maxBoundLsb,
                  c.err.maxRatio, static_cast<unsigned long long>(c.err.boundViolations + c.err.signErrors));
    std::printf("\n");
  }

  int g_undecidedLogged = 0;
  void LogUndecided(std::uint64_t before, const std::string& what)
  {
    if (kt::FastUndecided() != before && g_undecidedLogged++ < 60)
      std::printf("  fast path undecided (slow path used): %s\n", what.c_str());
  }

  void Mismatch(Check* c, const std::string& msg)
  {
    if (++c->mismatches <= 15)
      std::printf("  MISMATCH %s: %s\n", c->name, msg.c_str());
  }

  std::vector<std::vector<std::string>> Load(const std::string& path)
  {
    std::vector<std::vector<std::string>> rows;
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f)
    {
      std::printf("cannot open %s\n", path.c_str());
      return rows;
    }
    std::string line;
    char buf[4096];
    while (std::fgets(buf, sizeof buf, f))
    {
      line = buf;
      while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
        line.pop_back();
      if (!line.empty())
        rows.push_back(Split(line));
    }
    std::fclose(f);
    return rows;
  }

  void RunTrig(const std::vector<std::vector<std::string>>& rows, kt::Mode mode, const char* mname, u64 every)
  {
    kt::SetMode(mode);
    Check cs{"fsin"}, cc{"fcos"}, ct{"fptan"};
    u64 i = 0;
    for (const auto& r : rows)
    {
      if (mode == kt::Mode::ForceSlow && (i++ % every) != 0)
        continue;
      Ext x, es, ec, et;
      Truth ts, tc, tt;
      if (r.size() != 8 || !ParseExt(r[0], &x) || !ParseExt(r[1], &es) || !ParseExt(r[2], &ec) ||
          !ParseExt(r[3], &et) || !ParseTruth(r[5], &ts) || !ParseTruth(r[6], &tc) || !ParseTruth(r[7], &tt))
      {
        std::printf("bad trig row\n");
        std::exit(2);
      }
      const bool oor = r[4] == "1";
      bool o1 = false, o2 = false;
      kt::ClearLastFast();
      std::uint64_t und = kt::FastUndecided();
      const Ext gs = Fsin(x, &o1);
      if (mode == kt::Mode::Normal)
        LogUndecided(und, "fsin " + r[0]);
      if (mode == kt::Mode::FastOnly)
        MeasureFast(ts, &cs.err, "fsin " + r[0]);
      kt::ClearLastFast();
      und = kt::FastUndecided();
      const Ext gc = Fcos(x, &o2);
      if (mode == kt::Mode::Normal)
        LogUndecided(und, "fcos " + r[0]);
      if (mode == kt::Mode::FastOnly)
        MeasureFast(tc, &cc.err, "fcos " + r[0]);
      kt::ClearLastFast();
      Ext gt{0, 0};
      und = kt::FastUndecided();
      const bool ok = Fptan(x, &gt);
      if (mode == kt::Mode::Normal)
        LogUndecided(und, "fptan " + r[0]);
      if (mode == kt::Mode::FastOnly)
        MeasureFast(tt, &ct.err, "fptan " + r[0]);
      ++cs.total;
      ++cc.total;
      ++ct.total;
      if (!(gs == es) || o1 != oor)
        Mismatch(&cs, r[0] + " got " + Hex(gs) + (o1 ? " oor" : "") + " want " + Hex(es) + (oor ? " oor" : ""));
      if (!(gc == ec) || o2 != oor)
        Mismatch(&cc, r[0] + " got " + Hex(gc) + (o2 ? " oor" : "") + " want " + Hex(ec) + (oor ? " oor" : ""));
      if (ok == oor || (ok && !(gt == et)))
        Mismatch(&ct, r[0] + " got " + Hex(gt) + (ok ? "" : " (not pushed)") + " want " + Hex(et) + (oor ? " (not pushed)" : ""));
    }
    Report(mname, cs);
    Report(mname, cc);
    Report(mname, ct);
  }

  template <class Fn>
  void RunBinary(const char* name, const std::vector<std::vector<std::string>>& rows, kt::Mode mode, const char* mname,
                 u64 every, Fn fn)
  {
    kt::SetMode(mode);
    Check c{name};
    u64 i = 0;
    for (const auto& r : rows)
    {
      if (mode == kt::Mode::ForceSlow && (i++ % every) != 0)
        continue;
      Ext y, x, want;
      Truth t;
      if (r.size() != 4 || !ParseExt(r[0], &y) || !ParseExt(r[1], &x) || !ParseExt(r[2], &want) || !ParseTruth(r[3], &t))
      {
        std::printf("bad %s row\n", name);
        std::exit(2);
      }
      kt::ClearLastFast();
      const std::uint64_t und = kt::FastUndecided();
      const Ext got = fn(y, x);
      if (mode == kt::Mode::Normal)
        LogUndecided(und, std::string(name) + " " + r[0] + " " + r[1] + " -> " + r[2]);
      if (mode == kt::Mode::FastOnly)
        MeasureFast(t, &c.err, std::string(name) + " " + r[0] + " " + r[1]);
      ++c.total;
      if (!(got == want))
        Mismatch(&c, r[0] + " " + r[1] + " got " + Hex(got) + " want " + Hex(want));
    }
    Report(mname, c);
  }

  void RunF2xm1(const std::vector<std::vector<std::string>>& rows, kt::Mode mode, const char* mname, u64 every)
  {
    kt::SetMode(mode);
    Check c{"f2xm1"};
    u64 i = 0;
    for (const auto& r : rows)
    {
      if (mode == kt::Mode::ForceSlow && (i++ % every) != 0)
        continue;
      Ext x, want;
      Truth t;
      if (r.size() != 3 || !ParseExt(r[0], &x) || !ParseExt(r[1], &want) || !ParseTruth(r[2], &t))
      {
        std::printf("bad f2xm1 row\n");
        std::exit(2);
      }
      kt::ClearLastFast();
      const std::uint64_t und = kt::FastUndecided();
      const Ext got = F2xm1(x);
      if (mode == kt::Mode::Normal)
        LogUndecided(und, "f2xm1 " + r[0]);
      if (mode == kt::Mode::FastOnly)
        MeasureFast(t, &c.err, "f2xm1 " + r[0]);
      ++c.total;
      if (!(got == want))
        Mismatch(&c, r[0] + " got " + Hex(got) + " want " + Hex(want));
    }
    Report(mname, c);
  }

  int RunCheck(const std::string& dir, u64 every)
  {
    struct ModeDef
    {
      kt::Mode m;
      const char* n;
    };
    const ModeDef modes[] = {{kt::Mode::Normal, "normal"}, {kt::Mode::FastOnly, "fast-only"}, {kt::Mode::ForceSlow, "slow"}};
    const char* files[] = {"trig", "atan", "yl2x", "f2xm1"};
    for (const char* f : files)
    {
      const std::string path = dir + "/" + f + ".txt";
      const auto rows = Load(path);
      if (rows.empty())
        continue;
      std::printf("%s: %zu vectors\n", path.c_str(), rows.size());
      for (const ModeDef& md : modes)
      {
        kt::ResetCounters();
        const std::string fs(f);
        if (fs == "trig")
          RunTrig(rows, md.m, md.n, every);
        else if (fs == "atan")
          RunBinary("fpatan", rows, md.m, md.n, every, [](Ext y, Ext x) { return Fpatan(y, x); });
        else if (fs == "yl2x")
          RunBinary("fyl2x", rows, md.m, md.n, every, [](Ext y, Ext x) { return Fyl2x(y, x); });
        else
          RunF2xm1(rows, md.m, md.n, every);
        std::printf("  [%s] fast undecided %llu, slow undecided (near-ties) %llu\n", md.n,
                    static_cast<unsigned long long>(kt::FastUndecided()),
                    static_cast<unsigned long long>(kt::SlowUndecided()));
        std::fflush(stdout);
      }
    }
    kt::SetMode(kt::Mode::Normal);
    return 0;
  }

  // ------------------------------------------------------------------------------- bench

  Ext FromFloat(float f)
  {
    u64 b32;
    std::uint32_t b;
    std::memcpy(&b, &f, 4);
    b32 = b;
    const bool neg = b32 >> 31;
    const int ex = static_cast<int>((b32 >> 23) & 0xFF);
    const u64 fr = b32 & 0x7FFFFF;
    if (ex == 0) // the bench never passes denormals or zero
      return Ext{0, static_cast<std::uint16_t>(neg ? 0x8000 : 0)};
    return Ext{((1ull << 23) | fr) << 40, static_cast<std::uint16_t>((neg ? 0x8000 : 0) | (ex - 127 + 16383))};
  }

  Ext FromDouble(double d)
  {
    u64 b;
    std::memcpy(&b, &d, 8);
    const bool neg = b >> 63;
    const int ex = static_cast<int>((b >> 52) & 0x7FF);
    const u64 fr = b & ((1ull << 52) - 1);
    if (ex == 0)
      return Ext{0, static_cast<std::uint16_t>(neg ? 0x8000 : 0)};
    return Ext{((1ull << 52) | fr) << 11, static_cast<std::uint16_t>((neg ? 0x8000 : 0) | (ex - 1023 + 16383))};
  }

  double ThreadSeconds()
  {
    timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
  }

  volatile u64 g_sink;

  template <class Fn>
  void Bench(const char* name, int n, int reps, Fn fn)
  {
    u64 acc = 0;
    double best = 1e30;
    for (int rep = 0; rep < reps; ++rep)
    {
      const double t0 = ThreadSeconds();
      for (int i = 0; i < n; ++i)
        acc += fn(i).signif;
      const double dt = ThreadSeconds() - t0;
      if (dt < best)
        best = dt;
    }
    g_sink = acc;
    std::printf("  %-34s %8.1f ns/call (best of %d x %d, thread CPU time)\n", name, best / n * 1e9, reps, n);
  }

  int RunBench()
  {
    constexpr int n = 200000;
    std::mt19937_64 rng(12345);
    std::uniform_real_distribution<float> angle(-4 * 3.14159265f, 4 * 3.14159265f);
    std::uniform_real_distribution<float> coord(-1000.f, 1000.f);
    std::uniform_real_distribution<float> pos(1e-3f, 1e4f);
    std::uniform_real_distribution<float> half(-0.5f, 0.5f);
    std::uniform_real_distribution<double> unit(-1.0, 1.0);
    std::uniform_real_distribution<double> base(0.01, 100.0);
    std::uniform_real_distribution<double> expo(-4.0, 4.0);
    std::vector<Ext> a(n), b(n), c(n), d(n), e(n), f(n), g(n);
    for (int i = 0; i < n; ++i)
    {
      a[i] = FromFloat(angle(rng));
      b[i] = FromFloat(coord(rng));
      c[i] = FromFloat(coord(rng));
      d[i] = FromFloat(pos(rng));
      e[i] = FromFloat(half(rng));
      f[i] = FromDouble(base(rng));
      g[i] = FromDouble(expo(rng));
    }
    std::vector<Ext> s(n), x(n); // asin shape: fpatan(x, sqrt((1+x)(1-x)))
    for (int i = 0; i < n; ++i)
    {
      const double xv = unit(rng);
      x[i] = FromDouble(xv);
      s[i] = FromFloat(static_cast<float>(std::sqrt((1 + xv) * (1 - xv))));
    }
    const Ext ln2c = kLN2;
    for (int pass = 0; pass < 2; ++pass)
    {
      const bool slow = pass == 1;
      kt::SetMode(slow ? kt::Mode::ForceSlow : kt::Mode::Normal);
      const int nn = slow ? 2000 : n;
      const int reps = slow ? 2 : 5;
      std::printf("%s:\n", slow ? "slow path (ForceSlow; the fallback)" : "production (fast path)");
      Bench("fsin   float in [-4pi, 4pi]", nn, reps, [&](int i) { return Fsin(a[i], nullptr); });
      Bench("fcos   float in [-4pi, 4pi]", nn, reps, [&](int i) { return Fcos(a[i], nullptr); });
      Bench("fptan  float in [-4pi, 4pi]", nn, reps, [&](int i) { Ext t; Fptan(a[i], &t); return t; });
      Bench("fpatan float pairs in [-1000,1000]", nn, reps, [&](int i) { return Fpatan(b[i], c[i]); });
      Bench("fpatan asin shape (double, float)", nn, reps, [&](int i) { return Fpatan(x[i], s[i]); });
      Bench("fyl2x  ln2 * log2(float)", nn, reps, [&](int i) { return Fyl2x(ln2c, d[i]); });
      Bench("fyl2x  pow shape (double, double)", nn, reps, [&](int i) { return Fyl2x(g[i], f[i]); });
      Bench("f2xm1  float in [-0.5, 0.5]", nn, reps, [&](int i) { return F2xm1(e[i]); });
    }
    kt::SetMode(kt::Mode::Normal);
    return 0;
  }

  // ------------------------------------------------------------------------- known answers
  // Exact helpers for the x87 routine sequences around the kernels (values here stay small).

  struct Fix // value = m * 2^e (exact)
  {
    __int128 m;
    int e;
  };

  Fix FixFromExt(Ext x)
  {
    const int be = x.signExp & 0x7FFF;
    const __int128 m = static_cast<__int128>(x.signif);
    return Fix{(x.signExp & 0x8000) ? -m : m, be - 16383 - 63};
  }

  Fix FixAdd(Fix a, Fix b)
  {
    const int e = a.e < b.e ? a.e : b.e;
    return Fix{(a.m << (a.e - e)) + (b.m << (b.e - e)), e};
  }

  // RN to `bits` significant bits (ties to even), as a double (exact for bits <= 53).
  double RoundBits(Fix v, int bits)
  {
    if (v.m == 0)
      return 0.0;
    const bool neg = v.m < 0;
    unsigned __int128 a = neg ? -static_cast<unsigned __int128>(v.m) : static_cast<unsigned __int128>(v.m);
    int len = 0;
    for (unsigned __int128 t = a; t; t >>= 1)
      ++len;
    int e = v.e;
    if (len > bits)
    {
      const int drop = len - bits;
      const unsigned __int128 rem = a & ((static_cast<unsigned __int128>(1) << drop) - 1);
      const unsigned __int128 half = static_cast<unsigned __int128>(1) << (drop - 1);
      a >>= drop;
      e += drop;
      if (rem > half || (rem == half && (a & 1)))
        ++a;
    }
    const double d = std::ldexp(static_cast<double>(static_cast<std::uint64_t>(a)), e);
    return neg ? -d : d;
  }

  std::uint32_t FloatBits(float f)
  {
    std::uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
  }

  float FloatFromBits(std::uint32_t b)
  {
    float f;
    std::memcpy(&f, &b, 4);
    return f;
  }

  int RunKat()
  {
    int fails = 0;
    auto check = [&](const char* what, std::uint64_t got, std::uint64_t want) {
      std::printf("  %-60s got 0x%llX want 0x%llX %s\n", what, static_cast<unsigned long long>(got),
                  static_cast<unsigned long long>(want), got == want ? "ok" : "FAIL");
      fails += got != want;
    };
    // 1. Intel FSIN on double(pi): the 66-bit pi reduction leaves r = -0x234C * 2^-66, so the
    //    stored double is 1.2246063538223773e-16 (true sin: 1.2246467991473532e-16). Value
    //    measured on Intel hardware and published by Bruce Dawson (2014).
    {
      const double pi = 3.141592653589793;
      const Ext s = Fsin(FromDouble(pi), nullptr);
      const double d = RoundBits(FixFromExt(s), 53);
      std::uint64_t db;
      std::memcpy(&db, &d, 8);
      check("fsin(double pi) stored as double (Intel hardware value)", db, 0x3CA1A60000000000ull);
      check("fsin(double pi) as Ext significand", s.signif, 0x8D30000000000000ull);
      check("fsin(double pi) as Ext exponent", s.signExp, 0x3FCA);
    }
    // 2. msvc8_x87_asin(0x3F052EAA) -> 0x3F0C1118 (X87Math.h / DETERMINISM.md 6.1): PC24
    //    (1+x)(1-x), PC24 fsqrt, fpatan(x, s), store64, then the caller's float cast.
    {
      const float xf = FloatFromBits(0x3F052EAAu);
      const double x = xf;
      const float a = static_cast<float>(1.0 + x), b = static_cast<float>(1.0 - x);
      const float p = static_cast<float>(static_cast<double>(a) * static_cast<double>(b));
      const float sq = static_cast<float>(std::sqrt(static_cast<double>(p)));
      const Ext r = Fpatan(FromDouble(x), FromFloat(sq));
      const float res = static_cast<float>(RoundBits(FixFromExt(r), 53));
      check("msvc8_x87_asin(0x3F052EAA) (fpatan)", FloatBits(res), 0x3F0C1118u);
    }
    // 3. msvc8_x87_pow(0x41AB54C2, 2) -> 0x43E554AF: fyl2x; frndint; n - t at PC24; fchs;
    //    f2xm1; 1 + . at PC24; fscale; store64; the caller's float cast.
    {
      const double x = FloatFromBits(0x41AB54C2u), y = 2.0;
      const Ext t = Fyl2x(FromDouble(y), FromDouble(x));
      const Fix tf = FixFromExt(t);
      const double tn = std::nearbyint(RoundBits(tf, 53)); // frndint (t is ~8.8, far from .5)
      const Fix nf{static_cast<__int128>(tn), 0};
      const double diff = RoundBits(FixAdd(nf, Fix{-tf.m, tf.e}), 24); // n - t, PC24
      const Ext e = F2xm1(FromDouble(-diff));
      const double onePlus = RoundBits(FixAdd(Fix{1, 0}, FixFromExt(e)), 24);
      const float res = static_cast<float>(std::ldexp(onePlus, static_cast<int>(tn)));
      check("msvc8_x87_pow(0x41AB54C2, 2) (fyl2x, f2xm1)", FloatBits(res), 0x43E554AFu);
    }
    // 4. Exact identities of the special-value tables and exact cases.
    {
      check("fpatan(1, 1) = RN64(pi/4)", Fpatan(kOne, kOne).signif, kPi.signif);
      check("fyl2x(ln2, 2) = ln2 exactly", Fyl2x(kLN2, Ext{1ull << 63, 0x4000}).signif, kLN2.signif);
      check("fyl2x(lg2, 1) = +0", Fyl2x(kLG2, kOne).signExp, 0);
      check("f2xm1(-1) = -0.5", F2xm1(Ext{1ull << 63, 0xBFFF}).signExp, 0xBFFE);
      bool oor = false;
      const Ext big{1ull << 63, 0x403E}; // 2^63
      check("fsin(2^63) leaves the operand (C2 set)", Fsin(big, &oor).signExp + (oor ? 0x10000 : 0), 0x1403E);
      Ext tan{0, 0};
      check("fptan(2^63) pushes nothing", Fptan(big, &tan) ? 1 : 0, 0);
      check("fcos(+inf) = indefinite", Fcos(Ext{1ull << 63, 0x7FFF}, nullptr).signif, kIndefinite.signif);
    }
    std::printf("%s (%d failures)\n", fails ? "KAT FAILED" : "KAT OK", fails);
    return fails ? 1 : 0;
  }
} // namespace

int main(int argc, char** argv)
{
  if (argc >= 3 && std::string(argv[1]) == "check")
    return RunCheck(argv[2], argc >= 4 ? std::strtoull(argv[3], nullptr, 10) : 1);
  if (argc >= 2 && std::string(argv[1]) == "bench")
    return RunBench();
  if (argc >= 2 && std::string(argv[1]) == "kat")
    return RunKat();
  std::printf("usage: kernel_test check <dir> [slowEvery] | bench\n");
  return 2;
}
