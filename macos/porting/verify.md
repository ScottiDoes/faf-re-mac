# Porting notes: wave-2 verifier (key: verify)

The result is in `macos/porting/WAVE2_REPORT.md`. This file records what I ran and the tools I
left behind, so the pass can be repeated. I changed no source file and committed nothing.

## What I did

1. **Full rebuild.**
   - Backed up the link run's `faf_replay` and map to `build/verify-tests/faf_replay.pre-wave2*`.
   - Ran `ninja -C build/macos -t clean faf_replay`, then `ninja -C build/macos -k 0 -j 10 faf_engine faf_replay > build/macos/census-wave2.log`.
   - Result: 1,365 steps, 0 failed, 0 errors, 34 warnings, linked. 2 min 05 s wall.
2. **Replay runs** (`macos/run_replay.sh`, output under `build/verify-tests/`):
   - `run50/`: `SPEED=50`. 0 mismatches, 82.4x real time.
   - `run50-w8/`: `SPEED=50`, `/simworkers 8`. 0 mismatches, 84.8x real time.
   - `negctl/`: `SPEED=50` on `negctl/replay_corrupt.scfareplay`, which has a digest byte flipped at beat 5000 for both players. `mismatches=2`, exit 1.
3. **Test suites re-run:**
   - crt: `run_tests.sh`, PASS;
   - x87-core: full suite 0 failing, and `x87core_tests_kernels --quick kat integration` 0 failing;
   - x87-kernels: kat, internal_test, and the full corpus, all 0 mismatches.

   Logs: `build/verify-tests/{crt,x87-core,x87-kernels}.log`.
4. **Post-link checks on the new binary:** `check_symbols.sh`, `check_fma.sh` (965 objects), an `otool` FMA scan and `check_map_order.py`. All pass.
5. **Rule audit:**
   - `unifdef -UFAF_POSIX` over HEAD and the working copy of all 60 modified files (the Windows view);
   - `git -C dependencies/boost_1_34_1 diff` compared with the boost macOS patch;
   - a WildMagic tree rebuild from `git archive` plus both patches;
   - the zlib diff compared with its required patch;
   - mtimes of the lead-owned files;
   - stub logging;
   - a grep for 64-bit atomics.
6. **Size probe** (`build/verify-tests/probe/SizeProbe.cpp`, compiled with `tu.sh`).
   - `LayoutAsserts.h` turns `static_assert` into a no-op in every engine TU, so the probe `#undef`s it after its includes.
   - `SizeProbeNeg.cpp` is the negative control and must fail.
7. **Determinism checks:**
   - X87Emu.cpp read line by line against X87Math64.asm (14 routines);
   - the x87 constants recomputed;
   - the fmin/fmax replacement checked against DETERMINISM.md §5 rule 6 and LLVM's x86 minnum lowering;
   - the Lua NaN code reviewed;
   - the A1 guards and compat Sync.h reviewed;
   - two independent oracles, below.

## Tools (all in `build/verify-tests/`, untracked)

| File | Use |
|---|---|
| `corrupt_replay.py <in> <out> <beat>` | Finds the `CMDST_VerifyChecksum` records (op 3, length 23: 16-byte digest + int32 beat) and flips one digest byte at `<beat>` in a copy. A run on the copy must report `mismatches=2`. That proves verification runs without an instrumented binary |
| `oracle/harness.cpp` | Links build/macos's `X87Emu.cpp.o` + `X87Kernels.cpp.o`; reads `op n hex...` lines on stdin |
| `oracle/x87_oracle.py <harness> [n] [seed]` | Exact-rational (fractions) model of the PC24 routines: round_to_int, vector_length_zyx, the 4 scale_vector*, bilinear_height, box_radius_on_axis. 50k each: 0 mismatches. A 25-bit mutant fails thousands |
| `oracle/kernel_oracle.py <harness> [n]` | 60-digit decimal model of sinf/cosf (\|x\| < pi/4) and intrinsic_ln/log10 (fldln2/fldlg2 constant x exact log2, RN64, store). 30k each: 0 mismatches. A swapped-constant mutant fails every input |
| `probe/SizeProbe.cpp` | Win32 sizes, the boost control block, `msvc8::vector`, gdi and WAVEFORMAT structs, atomics |

Rebuild the harness after an engine build:

```
O=build/macos/CMakeFiles/faf_engine.dir/Volumes/SSD_Programs/Gaming/faf-re/src/platform/posix/math
clang++ -std=c++20 -O1 -arch arm64 build/verify-tests/oracle/harness.cpp "$O/X87Emu.cpp.o" "$O/X87Kernels.cpp.o" -o build/verify-tests/oracle/harness
```

## Findings worth remembering

- `tu.sh` on a file containing `static_assert(false-condition)` exits 0, because
  `LayoutAsserts.h` neutralises the keyword. Any "it compiles, so the size is right" claim from
  an engine TU proves nothing unless the file re-enables `static_assert` (as WinCompatGdi.cpp
  does).
- The default replay run is serial (`sim_Workers` 0). Run `/simworkers 8` to exercise the A1
  atomics and the worker pool.
- Platform.h's POSIX `HIDWORD` differs from the Windows macro text for operands that are not 4
  bytes. There are no callers today (W2-1).

## Open items

None of my own. Requests and next steps are in WAVE2_REPORT.md sections 7 and 8.
